#ifndef IPC_HPP
#define IPC_HPP

#include <common/json_manager.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <sys/stat.h>

/// Ring buffer sizes (must be power of 2 for masking).
inline constexpr std::uint32_t REQUEST_QUEUE_SIZE = 8192;
inline constexpr std::uint32_t RESPONSE_QUEUE_SIZE = 8192;

/// Maximum length of a session identifier string.
inline constexpr std::size_t SESSION_ID_SIZE = 64;

/// POSIX shared-memory segment name.
inline constexpr const char* SHM_NAME = "/dhl_server_shm";

/// Cache-line size used for false-sharing avoidance in shm_header_t.
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

/// Permission mode for shm_open (owner read/write only).
inline constexpr mode_t SHM_PERMISSIONS = 0600;

/**
 * Command types that workers post back to the reactor.
 */
enum class response_command : std::uint8_t
{
    SEND = 0,            // Send data to a session
    START_ACK_TIMER = 1, // Start an ACK timer for a session/message
    CANCEL_ACK_TIMER = 2,
    CLEAR_TIMERS = 3,
    BLACKLIST = 4,
    MARK_AUTHENTICATED = 5, // Mark a session as authenticated
    START_KEEPALIVE_TIMER = 6,
    RESET_KEEPALIVE_TIMER = 7,
    DISCONNECT = 8, // Force-disconnect a session (e.g. keepalive timeout)
    BROADCAST = 9,   // Send payload to all authenticated sessions
    MQTT_PUBLISH_ROUTE = 10 // Publish a route to a courier via MQTT
};

/**
 * Protocol type for the session.
 */
enum class protocol_type : std::uint8_t
{
    TCP = 0,
    UDP = 1
};

/**
 * A request slot: reactor pushes one per incoming message for worker processing.
 * Pure POD — safe for shared memory.
 */
struct request_slot_t
{
    char session_id[SESSION_ID_SIZE];
    std::uint8_t protocol; // protocol_type
    char raw_json[BUFFER_SIZE];
    std::uint32_t payload_len;
    bool is_authenticated;
    bool is_blacklisted;
    bool is_disconnect; // true = client disconnected, worker should mark inactive
    char client_type[ROLE_SIZE];
    char username[CREDENTIALS_SIZE];
};

/**
 * A response slot: workers push commands back to the reactor.
 * Pure POD — safe for shared memory.
 */
struct response_slot_t
{
    std::uint8_t command; // response_command
    char session_id[SESSION_ID_SIZE];
    char payload[BUFFER_SIZE]; // JSON serialized data (for SEND)
    std::uint32_t payload_len;

    // Timer-related fields (for START_ACK_TIMER)
    std::uint32_t timer_timeout;
    char timer_key[TIMESTAMP_SIZE]; // msg timestamp
    std::uint32_t retry_count;
    std::uint32_t max_retries;

    // Auth-related fields (for MARK_AUTHENTICATED)
    char client_type[ROLE_SIZE];
    char username[CREDENTIALS_SIZE];

    // For SEND by username (when worker doesn't know the session_id)
    // If session_id is empty and target_username is set, reactor resolves session via find_session_by_username
    char target_username[CREDENTIALS_SIZE];

    // When true on a SEND command, reactor will also start an ACK timer after sending
    bool start_ack_timer;

    // For MQTT_PUBLISH_ROUTE: transaction id to assign to a courier route
    int mqtt_transaction_id;
};

/**
 * Shared memory header — contains ring buffer metadata and synchronization primitives.
 * Lives at the beginning of the mmap'd region.
 */
struct shm_header_t
{
    // Request ring (reactor → workers)
    alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> req_head; // reactor writes here (producer)
    alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> req_tail; // workers read from here (consumers)
    std::uint32_t req_capacity;

    // Response ring (workers → reactor)
    alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> resp_head; // workers write here (producers)
    alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> resp_tail; // reactor reads from here (consumer)
    std::uint32_t resp_capacity;

    // Synchronization — request ring
    sem_t sem_requests;       // signaled when a request is available
    sem_t sem_free_req_slots; // signaled when a request slot is freed (backpressure)

    // Synchronization — response ring
    sem_t sem_responses;        // signaled when a response is available
    sem_t sem_free_resp_slots;  // signaled when a response slot is freed (backpressure)
    pthread_mutex_t resp_mutex; // protects resp_head for multiple worker threads

    // Shutdown flag
    std::atomic<int> shutdown_flag;
};

/**
 * shared_queue: wraps POSIX shared memory with two ring buffers and sync primitives.
 * Used by both reactor and worker processes.
 */
class shared_queue
{
  public:
    /**
     * Create and initialize the shared memory segment (called by reactor before fork).
     */
    static shared_queue create();

    /**
     * Open an existing shared memory segment (called by worker process after fork).
     */
    static shared_queue open();

    ~shared_queue();

    // Move-only
    shared_queue(shared_queue&& other) noexcept;
    shared_queue& operator=(shared_queue&& other) noexcept;
    shared_queue(const shared_queue&) = delete;
    shared_queue& operator=(const shared_queue&) = delete;

    // === Reactor-side methods ===

    /**
     * Push a request into the ring buffer for workers to consume.
     * Blocks if the ring is full (via sem_free_req_slots).
     * @return true on success, false if shutdown was signaled.
     */
    bool push_request(const request_slot_t& slot);

    /**
     * Pop a response from the response ring buffer (non-blocking).
     * @return true if a response was available, false if ring is empty.
     */
    bool pop_response(response_slot_t& slot);

    // === Worker-side methods ===

    /**
     * Wait for and consume a request from the ring buffer.
     * Blocks until a request is available (via sem_requests).
     * @return true on success, false if shutdown was signaled.
     */
    bool wait_request(request_slot_t& slot);

    /**
     * Push a response into the response ring buffer.
     * Called by worker threads (protected by resp_mutex).
     * @param efd The eventfd to notify the reactor.
     */
    void push_response(const response_slot_t& slot, int efd);

    // === Common ===

    /** Signal shutdown to all processes/threads. */
    void signal_shutdown();

    /** Check if shutdown has been signaled. */
    bool is_shutdown() const;

    /** Get total shared memory size. */
    static std::size_t total_shm_size();

    /** Unlink the shared memory segment (cleanup). */
    static void unlink();

  private:
    shared_queue() = default;
    void init_header();

    shm_header_t* m_header = nullptr;
    request_slot_t* m_req_ring = nullptr;
    response_slot_t* m_resp_ring = nullptr;
    void* m_base = nullptr;
    std::size_t m_size = 0;
    int m_shm_fd = -1;
    bool m_owner = false; // true for creator (reactor)
};

#endif // IPC_HPP
