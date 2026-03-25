#include "ipc.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

class SharedQueueTest : public ::testing::Test
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
};

TEST_F(SharedQueueTest, TotalShmSizePositive)
{
    std::size_t size = shared_queue::total_shm_size();
    EXPECT_GT(size, sizeof(shm_header_t));
}

TEST_F(SharedQueueTest, CreateAndDestroyDoesNotLeak)
{
    {
        auto q = shared_queue::create();
        EXPECT_FALSE(q.is_shutdown());
    }
}

TEST_F(SharedQueueTest, CreateThenOpen)
{
    auto owner = shared_queue::create();
    auto client = shared_queue::open();
    EXPECT_FALSE(owner.is_shutdown());
    EXPECT_FALSE(client.is_shutdown());
}

TEST_F(SharedQueueTest, PushRequestAndWaitRequest)
{
    auto owner = shared_queue::create();

    request_slot_t req{};
    std::strncpy(req.session_id, "sess_001", SESSION_ID_SIZE - 1);
    req.payload_len = 5;
    std::strncpy(req.raw_json, "hello", BUFFER_SIZE - 1);

    ASSERT_TRUE(owner.push_request(req));

    request_slot_t popped{};

    std::thread consumer([&owner, &popped]() { owner.wait_request(popped); });

    consumer.join();

    EXPECT_STREQ(popped.session_id, "sess_001");
    EXPECT_EQ(popped.payload_len, 5u);
    EXPECT_STREQ(popped.raw_json, "hello");
}

TEST_F(SharedQueueTest, PushResponseAndPopResponse)
{
    auto owner = shared_queue::create();

    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_NE(efd, -1);

    response_slot_t resp{};
    resp.command = static_cast<std::uint8_t>(response_command::SEND);
    std::strncpy(resp.session_id, "sess_002", SESSION_ID_SIZE - 1);
    resp.payload_len = 4;
    std::strncpy(resp.payload, "data", BUFFER_SIZE - 1);

    owner.push_response(resp, efd);

    std::uint64_t val = 0;
    EXPECT_EQ(read(efd, &val, sizeof(val)), static_cast<ssize_t>(sizeof(val)));
    EXPECT_EQ(val, 1u);

    response_slot_t popped{};
    ASSERT_TRUE(owner.pop_response(popped));

    EXPECT_EQ(popped.command, static_cast<std::uint8_t>(response_command::SEND));
    EXPECT_STREQ(popped.session_id, "sess_002");
    EXPECT_EQ(popped.payload_len, 4u);
    EXPECT_STREQ(popped.payload, "data");

    ::close(efd);
}

TEST_F(SharedQueueTest, PopResponseWhenEmptyReturnsFalse)
{
    auto owner = shared_queue::create();
    response_slot_t slot{};
    EXPECT_FALSE(owner.pop_response(slot));
}

TEST_F(SharedQueueTest, SignalShutdownStopsQueue)
{
    auto owner = shared_queue::create();
    EXPECT_FALSE(owner.is_shutdown());

    owner.signal_shutdown();
    EXPECT_TRUE(owner.is_shutdown());
}

TEST_F(SharedQueueTest, PushRequestAfterShutdownReturnsFalse)
{
    auto owner = shared_queue::create();
    owner.signal_shutdown();

    request_slot_t req{};
    EXPECT_FALSE(owner.push_request(req));
}

TEST_F(SharedQueueTest, WaitRequestUnblocksOnShutdown)
{
    auto owner = shared_queue::create();

    bool returned = false;
    std::thread consumer([&owner, &returned]() {
        request_slot_t slot{};
        owner.wait_request(slot);
        returned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(returned);

    owner.signal_shutdown();
    consumer.join();
    EXPECT_TRUE(returned);
}

TEST_F(SharedQueueTest, MoveConstructor)
{
    auto q1 = shared_queue::create();
    EXPECT_FALSE(q1.is_shutdown());

    auto q2 = std::move(q1);
    EXPECT_FALSE(q2.is_shutdown());
}

TEST_F(SharedQueueTest, MoveAssignment)
{
    auto q1 = shared_queue::create();
    auto q2 = shared_queue::create();
    q2 = std::move(q1);
    EXPECT_FALSE(q2.is_shutdown());
}

TEST_F(SharedQueueTest, MultipleRequests)
{
    auto owner = shared_queue::create();

    static constexpr int NUM_REQUESTS = 100;
    for (int i = 0; i < NUM_REQUESTS; ++i)
    {
        request_slot_t req{};
        snprintf(req.session_id, SESSION_ID_SIZE, "sess_%03d", i);
        req.payload_len = static_cast<std::uint32_t>(i);
        ASSERT_TRUE(owner.push_request(req));
    }

    for (int i = 0; i < NUM_REQUESTS; ++i)
    {
        request_slot_t popped{};
        ASSERT_TRUE(owner.wait_request(popped));

        char expected_id[SESSION_ID_SIZE];
        snprintf(expected_id, SESSION_ID_SIZE, "sess_%03d", i);
        EXPECT_STREQ(popped.session_id, expected_id);
        EXPECT_EQ(popped.payload_len, static_cast<std::uint32_t>(i));
    }
}

TEST_F(SharedQueueTest, MultipleResponses)
{
    auto owner = shared_queue::create();
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_NE(efd, -1);

    static constexpr int NUM_RESPONSES = 50;
    for (int i = 0; i < NUM_RESPONSES; ++i)
    {
        response_slot_t resp{};
        resp.command = static_cast<std::uint8_t>(response_command::SEND);
        snprintf(resp.session_id, SESSION_ID_SIZE, "resp_%03d", i);
        resp.payload_len = static_cast<std::uint32_t>(i);
        owner.push_response(resp, efd);
    }

    for (int i = 0; i < NUM_RESPONSES; ++i)
    {
        response_slot_t popped{};
        ASSERT_TRUE(owner.pop_response(popped));

        char expected_id[SESSION_ID_SIZE];
        snprintf(expected_id, SESSION_ID_SIZE, "resp_%03d", i);
        EXPECT_STREQ(popped.session_id, expected_id);
        EXPECT_EQ(popped.payload_len, static_cast<std::uint32_t>(i));
    }

    ::close(efd);
}

TEST_F(SharedQueueTest, ConcurrentPushAndWaitRequests)
{
    auto q = shared_queue::create();
    static constexpr int N = 200;

    std::thread producer([&q]() {
        for (int i = 0; i < N; ++i)
        {
            request_slot_t req{};
            snprintf(req.session_id, SESSION_ID_SIZE, "c_%d", i);
            req.payload_len = static_cast<std::uint32_t>(i);
            while (!q.push_request(req))
            {
                std::this_thread::yield();
            }
        }
    });

    int received = 0;
    std::thread consumer([&q, &received]() {
        for (int i = 0; i < N; ++i)
        {
            request_slot_t slot{};
            if (q.wait_request(slot))
            {
                ++received;
            }
        }
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(received, N);
}

TEST_F(SharedQueueTest, UnlinkRemovesSharedMemory)
{
    {
        auto q = shared_queue::create();
    }
    shared_queue::unlink();

    int fd = shm_open(SHM_NAME, O_RDONLY, SHM_PERMISSIONS);
    if (fd != -1)
    {
        ::close(fd);
        FAIL() << "Shared memory should have been unlinked";
    }
    EXPECT_EQ(errno, ENOENT);
}

TEST_F(SharedQueueTest, RequestSlotFieldPreservation)
{
    auto q = shared_queue::create();

    request_slot_t req{};
    std::strncpy(req.session_id, "test_sess", SESSION_ID_SIZE - 1);
    req.protocol = static_cast<std::uint8_t>(protocol_type::TCP);
    req.payload_len = 10;
    req.is_authenticated = true;
    req.is_blacklisted = false;
    req.is_disconnect = true;
    std::strncpy(req.client_type, "warehouse", ROLE_SIZE - 1);
    std::strncpy(req.username, "user1", CREDENTIALS_SIZE - 1);

    ASSERT_TRUE(q.push_request(req));

    request_slot_t popped{};
    ASSERT_TRUE(q.wait_request(popped));

    EXPECT_STREQ(popped.session_id, "test_sess");
    EXPECT_EQ(popped.protocol, static_cast<std::uint8_t>(protocol_type::TCP));
    EXPECT_EQ(popped.payload_len, 10u);
    EXPECT_TRUE(popped.is_authenticated);
    EXPECT_FALSE(popped.is_blacklisted);
    EXPECT_TRUE(popped.is_disconnect);
    EXPECT_STREQ(popped.client_type, "warehouse");
    EXPECT_STREQ(popped.username, "user1");
}

TEST_F(SharedQueueTest, ResponseSlotFieldPreservation)
{
    auto q = shared_queue::create();
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_NE(efd, -1);

    response_slot_t resp{};
    resp.command = static_cast<std::uint8_t>(response_command::START_ACK_TIMER);
    std::strncpy(resp.session_id, "ack_sess", SESSION_ID_SIZE - 1);
    resp.payload_len = 0;
    resp.timer_timeout = 5000;
    std::strncpy(resp.timer_key, "2025-01-01T00:00:00", TIMESTAMP_SIZE - 1);
    resp.retry_count = 2;
    resp.max_retries = 5;
    std::strncpy(resp.client_type, "delivery", ROLE_SIZE - 1);
    std::strncpy(resp.username, "driver01", CREDENTIALS_SIZE - 1);
    std::strncpy(resp.target_username, "warehouse01", CREDENTIALS_SIZE - 1);

    q.push_response(resp, efd);

    response_slot_t popped{};
    ASSERT_TRUE(q.pop_response(popped));

    EXPECT_EQ(popped.command, static_cast<std::uint8_t>(response_command::START_ACK_TIMER));
    EXPECT_STREQ(popped.session_id, "ack_sess");
    EXPECT_EQ(popped.timer_timeout, 5000u);
    EXPECT_STREQ(popped.timer_key, "2025-01-01T00:00:00");
    EXPECT_EQ(popped.retry_count, 2u);
    EXPECT_EQ(popped.max_retries, 5u);
    EXPECT_STREQ(popped.client_type, "delivery");
    EXPECT_STREQ(popped.username, "driver01");
    EXPECT_STREQ(popped.target_username, "warehouse01");

    ::close(efd);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
