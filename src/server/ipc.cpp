#include "ipc.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// ==================== Size calculation ====================

std::size_t shared_queue::total_shm_size()
{
    return sizeof(shm_header_t) + REQUEST_QUEUE_SIZE * sizeof(request_slot_t) +
           RESPONSE_QUEUE_SIZE * sizeof(response_slot_t);
}

// ==================== Create (reactor side) ====================

shared_queue shared_queue::create()
{
    shared_queue q;
    q.m_owner = true;
    q.m_size = total_shm_size();

    // Remove any stale segment
    shm_unlink(SHM_NAME);

    q.m_shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, SHM_PERMISSIONS);
    if (q.m_shm_fd == -1)
    {
        throw std::runtime_error(std::string("shm_open create failed: ") + strerror(errno));
    }

    if (ftruncate(q.m_shm_fd, static_cast<off_t>(q.m_size)) == -1)
    {
        ::close(q.m_shm_fd);
        shm_unlink(SHM_NAME);
        throw std::runtime_error(std::string("ftruncate failed: ") + strerror(errno));
    }

    q.m_base = mmap(nullptr, q.m_size, PROT_READ | PROT_WRITE, MAP_SHARED, q.m_shm_fd, 0);
    if (q.m_base == MAP_FAILED)
    {
        ::close(q.m_shm_fd);
        shm_unlink(SHM_NAME);
        throw std::runtime_error(std::string("mmap failed: ") + strerror(errno));
    }

    // Zero out entire region
    std::memset(q.m_base, 0, q.m_size);

    // Set up pointers
    q.m_header = static_cast<shm_header_t*>(q.m_base);
    auto* after_header = reinterpret_cast<char*>(q.m_base) + sizeof(shm_header_t);
    q.m_req_ring = reinterpret_cast<request_slot_t*>(after_header);
    q.m_resp_ring = reinterpret_cast<response_slot_t*>(after_header + REQUEST_QUEUE_SIZE * sizeof(request_slot_t));

    q.init_header();
    return q;
}

// ==================== Open (worker side) ====================

shared_queue shared_queue::open()
{
    shared_queue q;
    q.m_owner = false;
    q.m_size = total_shm_size();

    q.m_shm_fd = shm_open(SHM_NAME, O_RDWR, SHM_PERMISSIONS);
    if (q.m_shm_fd == -1)
    {
        throw std::runtime_error(std::string("shm_open open failed: ") + strerror(errno));
    }

    q.m_base = mmap(nullptr, q.m_size, PROT_READ | PROT_WRITE, MAP_SHARED, q.m_shm_fd, 0);
    if (q.m_base == MAP_FAILED)
    {
        ::close(q.m_shm_fd);
        throw std::runtime_error(std::string("mmap failed: ") + strerror(errno));
    }

    q.m_header = static_cast<shm_header_t*>(q.m_base);
    auto* after_header = reinterpret_cast<char*>(q.m_base) + sizeof(shm_header_t);
    q.m_req_ring = reinterpret_cast<request_slot_t*>(after_header);
    q.m_resp_ring = reinterpret_cast<response_slot_t*>(after_header + REQUEST_QUEUE_SIZE * sizeof(request_slot_t));

    return q;
}

// ==================== Initialize header ====================

void shared_queue::init_header()
{
    m_header->req_head.store(0, std::memory_order_relaxed);
    m_header->req_tail.store(0, std::memory_order_relaxed);
    m_header->req_capacity = REQUEST_QUEUE_SIZE;

    m_header->resp_head.store(0, std::memory_order_relaxed);
    m_header->resp_tail.store(0, std::memory_order_relaxed);
    m_header->resp_capacity = RESPONSE_QUEUE_SIZE;

    m_header->shutdown_flag.store(0, std::memory_order_relaxed);

    // Initialize process-shared semaphores — request ring
    if (sem_init(&m_header->sem_requests, 1, 0) == -1)
    {
        throw std::runtime_error(std::string("sem_init sem_requests failed: ") + strerror(errno));
    }
    if (sem_init(&m_header->sem_free_req_slots, 1, REQUEST_QUEUE_SIZE) == -1)
    {
        throw std::runtime_error(std::string("sem_init sem_free_req_slots failed: ") + strerror(errno));
    }

    // Initialize process-shared semaphores — response ring
    if (sem_init(&m_header->sem_responses, 1, 0) == -1)
    {
        throw std::runtime_error(std::string("sem_init sem_responses failed: ") + strerror(errno));
    }
    if (sem_init(&m_header->sem_free_resp_slots, 1, RESPONSE_QUEUE_SIZE) == -1)
    {
        throw std::runtime_error(std::string("sem_init sem_free_resp_slots failed: ") + strerror(errno));
    }

    // Initialize process-shared mutex for response ring
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (pthread_mutex_init(&m_header->resp_mutex, &attr) != 0)
    {
        pthread_mutexattr_destroy(&attr);
        throw std::runtime_error("pthread_mutex_init failed");
    }
    pthread_mutexattr_destroy(&attr);
}

// ==================== Destructor ====================

shared_queue::~shared_queue()
{
    if (m_base && m_base != MAP_FAILED)
    {
        if (m_owner && m_header)
        {
            sem_destroy(&m_header->sem_requests);
            sem_destroy(&m_header->sem_free_req_slots);
            sem_destroy(&m_header->sem_responses);
            sem_destroy(&m_header->sem_free_resp_slots);
            pthread_mutex_destroy(&m_header->resp_mutex);
        }
        munmap(m_base, m_size);
    }
    if (m_shm_fd != -1)
    {
        ::close(m_shm_fd);
    }
}

// ==================== Move operations ====================

shared_queue::shared_queue(shared_queue&& other) noexcept
    : m_header(other.m_header), m_req_ring(other.m_req_ring), m_resp_ring(other.m_resp_ring), m_base(other.m_base),
      m_size(other.m_size), m_shm_fd(other.m_shm_fd), m_owner(other.m_owner)
{
    other.m_header = nullptr;
    other.m_req_ring = nullptr;
    other.m_resp_ring = nullptr;
    other.m_base = nullptr;
    other.m_size = 0;
    other.m_shm_fd = -1;
    other.m_owner = false;
}

shared_queue& shared_queue::operator=(shared_queue&& other) noexcept
{
    if (this != &other)
    {
        // Clean up current resources
        if (m_base && m_base != MAP_FAILED)
        {
            munmap(m_base, m_size);
        }
        if (m_shm_fd != -1)
        {
            ::close(m_shm_fd);
        }

        m_header = other.m_header;
        m_req_ring = other.m_req_ring;
        m_resp_ring = other.m_resp_ring;
        m_base = other.m_base;
        m_size = other.m_size;
        m_shm_fd = other.m_shm_fd;
        m_owner = other.m_owner;

        other.m_header = nullptr;
        other.m_req_ring = nullptr;
        other.m_resp_ring = nullptr;
        other.m_base = nullptr;
        other.m_size = 0;
        other.m_shm_fd = -1;
        other.m_owner = false;
    }
    return *this;
}

// ==================== Reactor-side: push_request ====================

bool shared_queue::push_request(const request_slot_t& slot)
{
    // Non-blocking try: never freeze the reactor event loop
    if (sem_trywait(&m_header->sem_free_req_slots) == -1)
    {
        if (errno == EAGAIN)
        {
            // Queue full — load shedding (caller drops the message)
            return false;
        }
        // Any other error (e.g. EINVAL)
        return false;
    }

    if (m_header->shutdown_flag.load(std::memory_order_acquire))
    {
        return false;
    }

    // Single producer (reactor) — no lock needed for req_head
    std::uint64_t head = m_header->req_head.load(std::memory_order_relaxed);
    std::uint32_t idx = static_cast<std::uint32_t>(head & (REQUEST_QUEUE_SIZE - 1));

    std::memcpy(&m_req_ring[idx], &slot, sizeof(request_slot_t));

    m_header->req_head.store(head + 1, std::memory_order_release);

    // Signal workers that a request is available
    sem_post(&m_header->sem_requests);
    return true;
}

// ==================== Reactor-side: pop_response ====================

bool shared_queue::pop_response(response_slot_t& slot)
{
    std::uint64_t tail = m_header->resp_tail.load(std::memory_order_relaxed);
    std::uint64_t head = m_header->resp_head.load(std::memory_order_acquire);

    if (tail >= head)
    {
        return false; // Ring empty
    }

    std::uint32_t idx = static_cast<std::uint32_t>(tail & (RESPONSE_QUEUE_SIZE - 1));
    std::memcpy(&slot, &m_resp_ring[idx], sizeof(response_slot_t));

    // Single consumer (reactor) — no lock needed for resp_tail
    m_header->resp_tail.store(tail + 1, std::memory_order_release);

    // Signal workers that a response slot is free
    sem_post(&m_header->sem_free_resp_slots);
    return true;
}

// ==================== Worker-side: wait_request ====================

bool shared_queue::wait_request(request_slot_t& slot)
{
    // Block until a request is available
    while (sem_wait(&m_header->sem_requests) == -1)
    {
        if (errno == EINTR)
        {
            if (m_header->shutdown_flag.load(std::memory_order_acquire))
            {
                return false;
            }
            continue;
        }
        return false;
    }

    if (m_header->shutdown_flag.load(std::memory_order_acquire))
    {
        return false;
    }

    // Multiple consumers — use atomic tail with CAS
    std::uint64_t tail;
    do
    {
        tail = m_header->req_tail.load(std::memory_order_relaxed);
    } while (!m_header->req_tail.compare_exchange_weak(tail, tail + 1, std::memory_order_acq_rel));

    std::uint32_t idx = static_cast<std::uint32_t>(tail & (REQUEST_QUEUE_SIZE - 1));
    std::memcpy(&slot, &m_req_ring[idx], sizeof(request_slot_t));

    // Signal reactor that a slot is free
    sem_post(&m_header->sem_free_req_slots);
    return true;
}

// ==================== Worker-side: push_response ====================

void shared_queue::push_response(const response_slot_t& slot, int efd)
{
    // Wait for a free response slot (backpressure — prevents overwriting unread responses)
    while (sem_wait(&m_header->sem_free_resp_slots) == -1)
    {
        if (errno == EINTR)
        {
            if (m_header->shutdown_flag.load(std::memory_order_acquire))
            {
                return;
            }
            continue;
        }
        return;
    }

    if (m_header->shutdown_flag.load(std::memory_order_acquire))
    {
        return;
    }

    // Multiple producers (worker threads) — protect with mutex
    pthread_mutex_lock(&m_header->resp_mutex);

    std::uint64_t head = m_header->resp_head.load(std::memory_order_relaxed);
    std::uint32_t idx = static_cast<std::uint32_t>(head & (RESPONSE_QUEUE_SIZE - 1));

    std::memcpy(&m_resp_ring[idx], &slot, sizeof(response_slot_t));

    m_header->resp_head.store(head + 1, std::memory_order_release);

    pthread_mutex_unlock(&m_header->resp_mutex);

    // Notify reactor via eventfd
    std::uint64_t val = 1;
    if (write(efd, &val, sizeof(val)) == -1)
    {
        std::cerr << "[IPC] WARNING: eventfd write failed: " << strerror(errno) << std::endl;
    }
}

// ==================== Shutdown ====================

void shared_queue::signal_shutdown()
{
    m_header->shutdown_flag.store(1, std::memory_order_release);

    // Wake up all blocked workers on request ring
    for (int i = 0; i < REQUEST_QUEUE_SIZE; ++i)
    {
        sem_post(&m_header->sem_requests);
    }

    // Wake up any workers blocked on the response ring (backpressure)
    for (int i = 0; i < RESPONSE_QUEUE_SIZE; ++i)
    {
        sem_post(&m_header->sem_free_resp_slots);
    }
}

bool shared_queue::is_shutdown() const
{
    return m_header->shutdown_flag.load(std::memory_order_acquire) != 0;
}

void shared_queue::unlink()
{
    shm_unlink(SHM_NAME);
}
