#define _POSIX_C_SOURCE 200809L
#include "logic.h"
#include "connection.h"
#include "ipc.h"
#include "json_manager.h"
#include "logger.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static client_context* logic_ctx = NULL;

static void* ack_timeout_checker_thread(void* arg)
{
    shared_data_t* shared_data = get_shared_data();
    sem_t* message_available_sem = get_message_sem();

    LOG_INFO_MSG("ACK checker thread started");

    while (!shared_data->should_exit)
    {
        int result = check_ack_timeouts();

        if (result < 0)
        {
            LOG_ERROR_MSG("Max retries exceeded, triggering shutdown");
            shared_data->should_exit = 1;
            sem_post(message_available_sem);
            break;
        }
        else if (result > 0)
        {
            char messages_to_retry[MAX_PENDING_ACKS][BUFFER_SIZE];
            int retry_count = 0;

            sem_t* inventory_sem = get_inventory_sem();
            sem_wait(inventory_sem);
            for (int i = 0; i < MAX_PENDING_ACKS; i++)
            {
                if (shared_data->pending_acks[i].active && shared_data->pending_acks[i].retry_count > 0)
                {
                    LOG_DEBUG_MSG("Re-enqueueing msg_id: %s (attempt %d)", 
                                  shared_data->pending_acks[i].msg_id, shared_data->pending_acks[i].retry_count);

                    strncpy(messages_to_retry[retry_count], shared_data->pending_acks[i].message_json, BUFFER_SIZE - 1);
                    retry_count++;
                }
            }
            sem_post(inventory_sem);

            for (int i = 0; i < retry_count; i++)
            {
                if (enqueue_pending_message_json(messages_to_retry[i]) != 0)
                {
                    LOG_ERROR_MSG("Failed to re-enqueue message");
                }
            }
        }

        sleep(1);
    }

    LOG_INFO_MSG("ACK checker thread exiting");
    return NULL;
}

static int handle_server_message(const message_t* msg)
{
    shared_data_t* shared_data = get_shared_data();

    printf("[RECEIVER] Got message type: %s\n", msg->msg_type);

    if (strstr(msg->msg_type, "ACK") != NULL)
    {
        remove_pending_ack(msg->timestamp);
        return 0;
    }

    if (strstr(msg->msg_type, "AUTH_RESPONSE") == NULL)
    {
        message_t ack_msg;

        if (create_acknowledgment_message(&ack_msg, shared_data->client_role, shared_data->client_id, SERVER, SERVER,
                                         msg->timestamp, 200) == 0)
        {
            if (enqueue_pending_message(&ack_msg) == 0)
            {
                printf("[RECEIVER] ACK enqueued for message type: %s\n", msg->msg_type);
            }
            else
            {
                fprintf(stderr, "[RECEIVER] Failed to enqueue ACK\n");
            }
        }
        else
        {
            fprintf(stderr, "[RECEIVER] Failed to create ACK message\n");
        }
    }

    return 0;
}

static void* receiver_thread(void* arg)
{
    client_context* ctx = (client_context*)arg;
    char buffer[BUFFER_SIZE];
    shared_data_t* shared_data = get_shared_data();

    printf("[RECEIVER] Thread started\n");

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(ctx->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        LOG_ERROR_MSG("Failed to set socket receive timeout: %s", strerror(errno));
    }

    while (!shared_data->should_exit)
    {
        int bytes = client_receive(ctx, buffer, BUFFER_SIZE);
        if (bytes > 0)
        {
            message_t msg;
            if (deserialize_message_from_json(buffer, &msg) == 0)
            {
                handle_server_message(&msg);
            }
            else
            {
                LOG_ERROR_MSG("Failed to deserialize received message");
            }
        }
        else if (bytes == -1)
        {
            // Check if it's a timeout (EAGAIN/EWOULDBLOCK)
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Timeout - check should_exit and continue
                continue;
            }
            // Real error
            LOG_ERROR_MSG("Receive error: %s", strerror(errno));
            break;
        }
        else if (bytes == -2)
        {
            LOG_WARNING_MSG("Connection closed by server");
            shared_data->should_exit = 1;
            break;
        }
    }

    LOG_INFO_MSG("Receiver thread exiting");
    return NULL;
}

static void* sender_thread(void* arg)
{
    client_context* ctx = (client_context*)arg;
    message_t msg;
    static int test_counter = 0; // Counter for test messages
    shared_data_t* shared_data = get_shared_data();
    sem_t* message_available_sem = get_message_sem();

    LOG_INFO_MSG("Sender thread started");

    while (!shared_data->should_exit)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        int sem_result = sem_timedwait(message_available_sem, &ts);

        if (sem_result == 0)
        {
            if (pop_pending_message(&msg) == 0)
            {
                char json_buffer[BUFFER_SIZE];
                if (serialize_message_to_json(&msg, json_buffer) == 0)
                {
                    if (client_send(ctx, json_buffer) == 0)
                    {
                        LOG_DEBUG_MSG("Message sent: type=%s, timestamp=%s", msg.msg_type, msg.timestamp);

                        if (strstr(msg.msg_type, "ACK") == NULL && strstr(msg.msg_type, "AUTH_REQUEST") == NULL &&
                            strstr(msg.msg_type, "KEEPALIVE") == NULL)
                        {
                            add_pending_ack(msg.timestamp, msg.msg_type, json_buffer);
                        }
                    }
                    else
                    {
                        LOG_ERROR_MSG("Failed to send message");
                    }
                }
            }
        }
        else if (errno == ETIMEDOUT)
        {
            test_counter++;
            if (test_counter >= 30)
            {
                test_counter = 0;
                LOG_DEBUG_MSG("Sending test INVENTORY_UPDATE message");

                message_t test_msg;
                inventory_item_t items[1];
                items[0].item_id = 42;
                items[0].item_name[0] = 't';
                items[0].item_name[1] = 'e';
                items[0].item_name[2] = 's';
                items[0].item_name[3] = 't';
                items[0].item_name[4] = '\0';
                items[0].quantity = 100;
                
                if (create_items_message(&test_msg, shared_data->client_role, shared_data->client_id, SERVER, SERVER,
                                         INVENTORY_UPDATE, items, 1) == 0)
                {
                    if (enqueue_pending_message(&test_msg) == 0)
                    {
                        LOG_DEBUG_MSG("Test INVENTORY_UPDATE enqueued");
                    }
                    else
                    {
                        LOG_ERROR_MSG("Failed to enqueue test message");
                    }
                }
                else
                {
                    LOG_ERROR_MSG("Failed to create test message");
                }
            }

        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            LOG_ERROR_MSG("sem_timedwait error: %s", strerror(errno));
            break;
        }
    }

    LOG_INFO_MSG("Sender thread exiting");
    return NULL;
}

static int child_logic_process(void)
{
    shared_data_t* shared_data = get_shared_data();

    LOG_INFO_MSG("Business logic process started (PID: %d)", getpid());

    int counter = 0;
    while (!shared_data->should_exit)
    {
        counter++;
        sleep(60);
    }

    LOG_INFO_MSG("Business logic process exiting");
    return 0;
}

// ==================== MAIN LOGIC ENTRY ====================

int logic_init(client_context* ctx, const char* client_role, const char* client_id)
{
    logic_ctx = ctx;

    LOG_INFO_MSG("Initializing client logic for %s/%s", client_role, client_id);

    // Initialize IPC
    if (ipc_init() != 0)
    {
        LOG_ERROR_MSG("Failed to initialize IPC");
        return -1;
    }

    // Get shared data and store client identity
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, client_role, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, client_id, sizeof(shared_data->client_id) - 1);
    LOG_INFO_MSG("Client identity set: %s / %s", shared_data->client_role, shared_data->client_id);

    pid_t pid = fork();

    if (pid < 0)
    {
        LOG_ERROR_MSG("Fork failed: %s", strerror(errno));
        ipc_cleanup();
        return -1;
    }
    else if (pid == 0)
    {
        // ============ CHILD PROCESS ============
        int ret = child_logic_process();
        ipc_cleanup();
        exit(ret);
    }
    else
    {
        // ============ PARENT PROCESS ============
        LOG_INFO_MSG("Network handler started (PID: %d, Child PID: %d)", getpid(), pid);

        pthread_t receiver, sender, ack_checker;

        // Create threads
        if (pthread_create(&receiver, NULL, receiver_thread, ctx) != 0)
        {
            LOG_ERROR_MSG("Failed to create receiver thread");
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            ipc_cleanup();
            return -1;
        }

        if (pthread_create(&sender, NULL, sender_thread, ctx) != 0)
        {
            LOG_ERROR_MSG("Failed to create sender thread");
            shared_data->should_exit = 1;
            pthread_join(receiver, NULL);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            ipc_cleanup();
            return -1;
        }

        if (pthread_create(&ack_checker, NULL, ack_timeout_checker_thread, NULL) != 0)
        {
            LOG_ERROR_MSG("Failed to create ACK checker thread");
            shared_data->should_exit = 1;
            sem_post(get_message_sem());
            pthread_join(receiver, NULL);
            pthread_join(sender, NULL);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            ipc_cleanup();
            return -1;
        }

        // Wait for child process
        int status;
        waitpid(pid, &status, 0);
        LOG_INFO_MSG("Child process exited with status %d", WEXITSTATUS(status));

        // Signal threads to exit
        shared_data->should_exit = 1;

        // Wake up sender thread if it's blocked waiting for messages
        sem_post(get_message_sem());

        // Wait for threads
        pthread_join(receiver, NULL);
        pthread_join(sender, NULL);
        pthread_join(ack_checker, NULL);

        // Check if exit was due to ACK timeout
        if (shared_data->ack_timeout_occurred)
        {
            LOG_WARNING_MSG("Disconnection due to ACK timeout");
        }

        ipc_cleanup();
    }

    LOG_INFO_MSG("Client logic shutdown complete");
    return 0;
}
