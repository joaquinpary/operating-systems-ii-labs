#include "connection_pool.hpp"

#include <iostream>
#include <stdexcept>

#define POOL_LOG_PREFIX "[CONNECTION_POOL] "

// ==================== connection_guard ====================

connection_pool::connection_guard::connection_guard(connection_pool& pool, std::unique_ptr<pqxx::connection> conn)
    : m_pool(&pool), m_connection(std::move(conn))
{
}

connection_pool::connection_guard::~connection_guard()
{
    if (m_connection)
    {
        m_pool->release(std::move(m_connection));
    }
}

connection_pool::connection_guard::connection_guard(connection_guard&& other) noexcept
    : m_pool(other.m_pool), m_connection(std::move(other.m_connection))
{
    other.m_pool = nullptr;
}

connection_pool::connection_guard& connection_pool::connection_guard::operator=(connection_guard&& other) noexcept
{
    if (this != &other)
    {
        // Release current connection if we have one
        if (m_connection)
        {
            m_pool->release(std::move(m_connection));
        }
        m_pool = other.m_pool;
        m_connection = std::move(other.m_connection);
        other.m_pool = nullptr;
    }
    return *this;
}

pqxx::connection& connection_pool::connection_guard::get()
{
    if (!m_connection)
    {
        throw std::runtime_error(POOL_LOG_PREFIX "Attempted to use a moved-from connection guard");
    }
    return *m_connection;
}

// ==================== connection_pool ====================

connection_pool::connection_pool(const std::string& conn_string, size_t pool_size)
    : m_pool_size(pool_size), m_conn_string(conn_string)
{
    if (pool_size == 0)
    {
        throw std::invalid_argument(POOL_LOG_PREFIX "Pool size must be greater than 0");
    }

    std::cout << POOL_LOG_PREFIX "Creating pool with " << pool_size << " connections..." << std::endl;

    for (size_t i = 0; i < pool_size; i++)
    {
        try
        {
            auto conn = std::make_unique<pqxx::connection>(conn_string);
            if (!conn->is_open())
            {
                throw std::runtime_error(POOL_LOG_PREFIX "Failed to open connection");
            }
            m_available.push(std::move(conn));
            std::cout << POOL_LOG_PREFIX "Connection " << (i + 1) << "/" << pool_size << " created" << std::endl;
        }
        catch (const std::exception& ex)
        {
            std::cerr << POOL_LOG_PREFIX "Error creating connection " << (i + 1) << ": " << ex.what() << std::endl;
            throw;
        }
    }

    std::cout << POOL_LOG_PREFIX "Pool ready with " << pool_size << " connections" << std::endl;
}

connection_pool::~connection_pool()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_available.empty())
    {
        m_available.pop();
    }
    std::cout << POOL_LOG_PREFIX "Pool destroyed" << std::endl;
}

connection_pool::connection_guard connection_pool::acquire()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    m_cv.wait(lock, [this]() { return !m_available.empty(); });

    auto conn = std::move(m_available.front());
    m_available.pop();

    std::cout << POOL_LOG_PREFIX "Connection acquired (" << m_available.size() << "/" << m_pool_size << " available)"
              << std::endl;

    return connection_guard(*this, std::move(conn));
}

void connection_pool::release(std::unique_ptr<pqxx::connection> conn)
{
    if (!conn)
    {
        return;
    }

    // If the connection is broken, try to reconnect
    if (!conn->is_open())
    {
        std::cerr << POOL_LOG_PREFIX "Connection was closed, reconnecting..." << std::endl;
        try
        {
            conn = std::make_unique<pqxx::connection>(m_conn_string);
        }
        catch (const std::exception& ex)
        {
            std::cerr << POOL_LOG_PREFIX "Reconnection failed: " << ex.what() << std::endl;
            // Still push the broken connection back — better than losing a pool slot
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_available.push(std::move(conn));
        std::cout << POOL_LOG_PREFIX "Connection released (" << m_available.size() << "/" << m_pool_size
                  << " available)" << std::endl;
    }

    m_cv.notify_one();
}

size_t connection_pool::size() const
{
    return m_pool_size;
}

size_t connection_pool::available() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_available.size();
}
