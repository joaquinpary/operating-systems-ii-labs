#include <chrono>
#include <gtest/gtest.h>
#include <server/connection_pool.hpp>
#include <server/database.hpp>
#include <thread>
#include <vector>

class ConnectionPoolTest : public ::testing::Test
{
  protected:
    std::string conn_string;

    void SetUp() override
    {
        conn_string = build_connection_string();

        // Test if DB is reachable before running pool tests
        try
        {
            pqxx::connection conn(conn_string);
            if (!conn.is_open())
            {
                GTEST_SKIP() << "Database not available, skipping connection_pool tests";
            }
        }
        catch (const std::exception&)
        {
            GTEST_SKIP() << "Database not available, skipping connection_pool tests";
        }
    }
};

TEST_F(ConnectionPoolTest, CreationAndSize)
{
    connection_pool pool(conn_string, 3);
    EXPECT_EQ(pool.size(), 3);
    EXPECT_EQ(pool.available(), 3);
}

TEST_F(ConnectionPoolTest, AcquireAndRelease)
{
    connection_pool pool(conn_string, 2);

    {
        auto guard1 = pool.acquire();
        EXPECT_EQ(pool.available(), 1);
        EXPECT_TRUE(guard1.get().is_open());

        {
            auto guard2 = pool.acquire();
            EXPECT_EQ(pool.available(), 0);
            EXPECT_TRUE(guard2.get().is_open());
        } // guard2 goes out of scope, release connection

        EXPECT_EQ(pool.available(), 1);
    } // guard1 goes out of scope, release connection

    EXPECT_EQ(pool.available(), 2);
}

TEST_F(ConnectionPoolTest, ExhaustionBlocks)
{
    connection_pool pool(conn_string, 1);

    // Acquire the only connection
    auto guard1 = pool.acquire();
    EXPECT_EQ(pool.available(), 0);

    // Launch a thread that will block trying to acquire
    std::atomic<bool> thread_acquired{false};
    std::thread t([&pool, &thread_acquired]() {
        auto guard2 = pool.acquire(); // blocks until guard1 is released
        thread_acquired = true;
    });

    // Give the thread time to start and block on acquire()
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(thread_acquired); // Should still be blocked

    // Release the connection by destroying guard1 in an inner scope
    { auto released = std::move(guard1); }

    // The blocked thread should now complete
    t.join();
    EXPECT_TRUE(thread_acquired);
}

TEST_F(ConnectionPoolTest, ConcurrentAccess)
{
    connection_pool pool(conn_string, 4);
    std::vector<std::thread> threads;
    std::atomic<int> successful_queries{0};

    for (int i = 0; i < 10; ++i)
    {
        threads.emplace_back([&pool, &successful_queries]() {
            try
            {
                auto guard = pool.acquire();
                pqxx::work txn(guard.get());
                pqxx::result res = txn.exec("SELECT 1");
                if (!res.empty())
                {
                    successful_queries++;
                }
            }
            catch (...)
            {
            }
        });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(successful_queries.load(), 10);
    EXPECT_EQ(pool.available(), 4);
}
