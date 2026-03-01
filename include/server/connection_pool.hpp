#ifndef CONNECTION_POOL_HPP
#define CONNECTION_POOL_HPP

#include <condition_variable>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <queue>
#include <string>

#define DEFAULT_POOL_SIZE 8

class connection_pool
{
  public:
    /**
     * RAII guard that holds a connection and returns it to the pool on destruction.
     * The guard is non-copyable but movable.
     */
    class connection_guard
    {
      public:
        connection_guard(connection_pool& pool, std::unique_ptr<pqxx::connection> conn);
        ~connection_guard();

        // Non-copyable
        connection_guard(const connection_guard&) = delete;
        connection_guard& operator=(const connection_guard&) = delete;

        // Movable
        connection_guard(connection_guard&& other) noexcept;
        connection_guard& operator=(connection_guard&& other) noexcept;

        // Access the underlying connection
        pqxx::connection& get();

      private:
        connection_pool* m_pool;
        std::unique_ptr<pqxx::connection> m_connection;
    };

    /**
     * Create a connection pool with the given connection string and pool size.
     * @param conn_string PostgreSQL connection string
     * @param pool_size Number of connections to pre-create
     */
    connection_pool(const std::string& conn_string, size_t pool_size = DEFAULT_POOL_SIZE);
    ~connection_pool();

    // Non-copyable, non-movable
    connection_pool(const connection_pool&) = delete;
    connection_pool& operator=(const connection_pool&) = delete;
    connection_pool(connection_pool&&) = delete;
    connection_pool& operator=(connection_pool&&) = delete;

    /**
     * Acquire a connection from the pool. Blocks if no connections are available.
     * The returned guard will automatically return the connection to the pool on destruction.
     */
    connection_guard acquire();

    /**
     * Return a connection to the pool. Called automatically by connection_guard destructor.
     */
    void release(std::unique_ptr<pqxx::connection> conn);

    /**
     * Get the total number of connections in the pool (both available and in use).
     */
    size_t size() const;

    /**
     * Get the number of currently available connections.
     */
    size_t available() const;

  private:
    std::queue<std::unique_ptr<pqxx::connection>> m_available;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    size_t m_pool_size;
    std::string m_conn_string;
};

#endif // CONNECTION_POOL_HPP
