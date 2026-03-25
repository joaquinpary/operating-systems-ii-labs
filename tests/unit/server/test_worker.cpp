#include "worker.hpp"

#include "config.hpp"
#include "database.hpp"
#include "ipc.hpp"

#include <gtest/gtest.h>
#include <pqxx/pqxx>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

class WorkerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        shared_queue::unlink();
    }

    void TearDown() override
    {
        shared_queue::unlink();
    }

    config::server_config make_config()
    {
        config::server_config cfg{};
        cfg.ip_v4 = "127.0.0.1";
        cfg.ip_v6 = "::1";
        cfg.network_port = 9999;
        cfg.ack_timeout = 5000;
        cfg.max_auth_attempts = 3;
        cfg.max_retries = 3;
        cfg.keepalive_timeout = 120;
        cfg.pool_size = 1;
        cfg.worker_threads = 1;
        cfg.credentials_path = "config/clients";
        return cfg;
    }
};

TEST_F(WorkerTest, StartsAndShutsDownCleanly)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available";
    }
    conn.reset();

    auto owner = shared_queue::create();

    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_NE(efd, -1);

    auto cfg = make_config();

    std::thread worker_thread([efd, &cfg]() { run_worker_process(efd, cfg); });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    owner.signal_shutdown();

    worker_thread.join();
    ::close(efd);
}

TEST_F(WorkerTest, ProcessesRequestAndProducesResponse)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    {
        create_credentials_table(*conn);
        pqxx::work txn(*conn);
        txn.exec("DELETE FROM credentials");
        txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                 "VALUES ('test_hub', 'testhash', 'HUB', FALSE)");
        txn.commit();
    }
    conn.reset();

    auto owner = shared_queue::create();

    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_NE(efd, -1);

    auto cfg = make_config();

    std::thread worker_thread([efd, &cfg]() { run_worker_process(efd, cfg); });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    request_slot_t req{};
    std::strncpy(req.session_id, "sess_test", SESSION_ID_SIZE - 1);
    req.protocol = static_cast<std::uint8_t>(protocol_type::TCP);
    req.is_disconnect = true;
    req.is_authenticated = true;
    std::strncpy(req.username, "test_hub", CREDENTIALS_SIZE - 1);
    std::strncpy(req.client_type, "HUB", ROLE_SIZE - 1);
    req.payload_len = 0;

    ASSERT_TRUE(owner.push_request(req));

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    owner.signal_shutdown();
    worker_thread.join();
    ::close(efd);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
