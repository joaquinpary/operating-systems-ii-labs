#define _POSIX_C_SOURCE 200809L
#include "logic.h"
#include "connection.h"
#include "ipc.h"
#include "json_manager.h"
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

    printf("[ACK_CHECKER] Thread started\n");

    while (!shared_data->should_exit)
    {
        int result = check_ack_timeouts();

        if (result < 0)
        {
            fprintf(stderr, "[ACK_CHECKER] Max retries exceeded, triggering shutdown...\n");
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
                    printf("[ACK_CHECKER] Preparing to re-enqueue msg_id: %s (attempt %d)\n",
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
                    fprintf(stderr, "[ACK_CHECKER] Failed to re-enqueue message\n");
                }
            }
        }

        sleep(1);
    }

    printf("[ACK_CHECKER] Thread exiting\n");
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
        perror("[RECEIVER] setsockopt SO_RCVTIMEO");
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
                fprintf(stderr, "[RECEIVER] Failed to deserialize message\n");
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
            fprintf(stderr, "[RECEIVER] Receive error: %s\n", strerror(errno));
            break;
        }
        else if (bytes == -2)
        {
            fprintf(stderr, "[RECEIVER] Connection closed by server\n");
            shared_data->should_exit = 1;
            break;
        }
    }

    printf("[RECEIVER] Thread exiting\n");
    return NULL;
}

static void* sender_thread(void* arg)
{
    client_context* ctx = (client_context*)arg;
    message_t msg;
    static int test_counter = 0; // Counter for test messages
    shared_data_t* shared_data = get_shared_data();
    sem_t* message_available_sem = get_message_sem();

    printf("[SENDER] Thread started\n");

    while (!shared_data->should_exit)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        // De momento queda asi, cuando implemente el resto de mensaje capaz que lo cambio
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
                        printf("[SENDER] Message sent successfully (type: %s, timestamp: %s)\n", msg.msg_type,
                               msg.timestamp);

                        if (strstr(msg.msg_type, "ACK") == NULL && strstr(msg.msg_type, "AUTH_REQUEST") == NULL &&
                            strstr(msg.msg_type, "KEEPALIVE") == NULL)
                        {
                            add_pending_ack(msg.timestamp, msg.msg_type, json_buffer);
                        }
                    }
                    else
                    {
                        fprintf(stderr, "[SENDER] Failed to send message\n");
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
                printf("[SENDER] === SENDING TEST INVENTORY_UPDATE ===\n");

                message_t test_msg;
                inventory_item_t items[1];
                items[0].item_id = 42;
                items[0].item_name[0] = 't';
                items[0].item_name[1] = 'e';
                items[0].item_name[2] = 's';
                items[0].item_name[3] = 't';
                items[0].item_name[4] = '\0';
                items[0].quantity = 100;
                printf("[SENDER] Test item: ID=%d, Qty=%d\n", items[0].item_id, items[0].quantity);
                if (create_items_message(&test_msg, shared_data->client_role, shared_data->client_id, SERVER, SERVER,
                                         INVENTORY_UPDATE, items,
                                         1) == 0) // quantity (hardcoded)
                {
                    printf("[SENDER] Created test INVENTORY_UPDATE message\n");
                    if (enqueue_pending_message(&test_msg) == 0)
                    {
                        printf("[SENDER] Test INVENTORY_UPDATE enqueued (item: 42, qty: 100)\n");
                    }
                    else
                    {
                        fprintf(stderr, "[SENDER] Failed to enqueue test message\n");
                    }
                }
                else
                {
                    fprintf(stderr, "[SENDER] Failed to create test message\n");
                }
            }

            continue;
        }
        else if (errno == EINTR)
        {
            // Interrupted by signal - continue
            continue;
        }
        else
        {
            perror("[SENDER] sem_timedwait");
            break;
        }
    }

    printf("[SENDER] Thread exiting\n");
    return NULL;
}

static int child_logic_process(void)
{
    shared_data_t* shared_data = get_shared_data();

    printf("[CHILD] Logic process started (PID: %d)\n", getpid());

    int counter = 0;
    while (!shared_data->should_exit)
    {
        counter++;

        sleep(60);
    }

    printf("[CHILD] Logic process exiting\n");
    return 0;
}

// ==================== MAIN LOGIC ENTRY ====================

int logic_init(client_context* ctx, const char* client_role, const char* client_id)
{
    logic_ctx = ctx;

    // Initialize IPC
    if (ipc_init() != 0)
    {
        fprintf(stderr, "[LOGIC] Failed to initialize IPC\n");
        return -1;
    }

    // Get shared data and store client identity
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, client_role, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, client_id, sizeof(shared_data->client_id) - 1);
    printf("[LOGIC] Client identity: %s / %s\n", shared_data->client_role, shared_data->client_id);

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("[LOGIC] fork");
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
        printf("[PARENT] Network handler started (PID: %d, Child PID: %d)\n", getpid(), pid);

        pthread_t receiver, sender, ack_checker;

        // Create threads
        if (pthread_create(&receiver, NULL, receiver_thread, ctx) != 0)
        {
            perror("[LOGIC] pthread_create receiver");
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            ipc_cleanup();
            return -1;
        }

        if (pthread_create(&sender, NULL, sender_thread, ctx) != 0)
        {
            perror("[LOGIC] pthread_create sender");
            shared_data->should_exit = 1;
            pthread_join(receiver, NULL);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            ipc_cleanup();
            return -1;
        }

        if (pthread_create(&ack_checker, NULL, ack_timeout_checker_thread, NULL) != 0)
        {
            perror("[LOGIC] pthread_create ack_checker");
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
        printf("[PARENT] Child process exited with status %d\n", WEXITSTATUS(status));

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
            fprintf(stderr, "[PARENT] Disconnection due to ACK timeout\n");
        }

        ipc_cleanup();
    }

    return 0;
}
