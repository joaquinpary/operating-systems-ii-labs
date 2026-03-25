#include "timer_manager.hpp"

#include "event_loop.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>

/**
 * timer_manager is reactor-only (single-threaded).
 * All timer operations MUST happen on the same thread as the event loop.
 *
 * Strategy:
 *   - Set up timers before starting the loop, OR
 *   - Use a one-shot timerfd registered in the loop to schedule operations
 *     (to avoid cross-thread access to timer_manager).
 *   - Run the loop on a background thread, stop it after a bounded wait.
 */

class TimerManagerTest : public ::testing::Test
{
  protected:
    std::unique_ptr<event_loop> loop;
    std::unique_ptr<timer_manager> tm;
    std::unique_ptr<std::thread> io_thread;

    void SetUp() override
    {
        loop = std::make_unique<event_loop>();
        tm = std::make_unique<timer_manager>(*loop);
    }

    void TearDown() override
    {
        if (loop)
        {
            loop->stop();
        }
        if (io_thread && io_thread->joinable())
        {
            io_thread->join();
        }
    }

    /// Start the event loop on a background thread.
    void start_loop()
    {
        io_thread = std::make_unique<std::thread>([this]() { loop->run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    /**
     * Schedule a callback to run on the reactor thread after @p delay_ms.
     * Uses a one-shot timerfd so the operation executes inside the epoll loop.
     */
    void schedule_on_loop(int delay_ms, std::function<void()> fn)
    {
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        ASSERT_GE(tfd, 0);

        struct itimerspec ts
        {
        };
        if (delay_ms <= 0)
        {
            ts.it_value.tv_nsec = 1; // fire on next epoll_wait iteration
        }
        else
        {
            ts.it_value.tv_sec = delay_ms / 1000;
            ts.it_value.tv_nsec = (delay_ms % 1000) * 1000000L;
        }

        ASSERT_EQ(timerfd_settime(tfd, 0, &ts, nullptr), 0);

        loop->add_fd(tfd, EPOLLIN, [this, tfd, fn = std::move(fn)](std::uint32_t) {
            std::uint64_t expirations;
            (void)read(tfd, &expirations, sizeof(expirations));
            loop->remove_fd(tfd);
            ::close(tfd);
            fn();
        });
    }
};

// ---------- ACK timer basic expiry ----------

TEST_F(TimerManagerTest, StartAckTimer)
{
    std::atomic<bool> timeout_called{false};

    tm->start_ack_timer("session_1", "timestamp_1", 0, [&timeout_called]() { timeout_called = true; });

    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(timeout_called.load());
}

// ---------- ACK timer cancellation ----------

TEST_F(TimerManagerTest, CancelAckTimer)
{
    std::atomic<bool> timeout_called{false};

    tm->start_ack_timer("session_1", "timestamp_1", 2, [&timeout_called]() { timeout_called = true; });

    // Cancel on the reactor thread (50 ms in, well before 2s timeout)
    schedule_on_loop(50, [this]() { tm->cancel_ack_timer("session_1", "timestamp_1"); });

    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_FALSE(timeout_called.load());
}

// ---------- Cancel non-existent timer ----------

TEST_F(TimerManagerTest, CancelNonExistentAckTimer)
{
    // No loop needed — cancel returns false synchronously (safe before run())
    bool cancelled = tm->cancel_ack_timer("session_1", "nonexistent_timestamp");
    EXPECT_FALSE(cancelled);
}

// ---------- Multiple ACK timers per session ----------

TEST_F(TimerManagerTest, MultipleAckTimersPerSession)
{
    std::atomic<int> timeout_count{0};

    tm->start_ack_timer("session_1", "timestamp_1", 0, [&timeout_count]() { timeout_count++; });
    tm->start_ack_timer("session_1", "timestamp_2", 0, [&timeout_count]() { timeout_count++; });
    tm->start_ack_timer("session_1", "timestamp_3", 0, [&timeout_count]() { timeout_count++; });

    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(timeout_count.load(), 3);
}

// ---------- Multiple sessions ----------

TEST_F(TimerManagerTest, MultipleSessionsAckTimers)
{
    std::atomic<int> session1_count{0};
    std::atomic<int> session2_count{0};

    tm->start_ack_timer("session_1", "timestamp_1", 0, [&session1_count]() { session1_count++; });
    tm->start_ack_timer("session_2", "timestamp_1", 0, [&session2_count]() { session2_count++; });

    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(session1_count.load(), 1);
    EXPECT_EQ(session2_count.load(), 1);
}

// ---------- clear_session_timers ----------

TEST_F(TimerManagerTest, ClearSessionTimers)
{
    std::atomic<int> timeout_count{0};

    tm->start_ack_timer("session_1", "timestamp_1", 2, [&timeout_count]() { timeout_count++; });
    tm->start_ack_timer("session_1", "timestamp_2", 2, [&timeout_count]() { timeout_count++; });

    // Clear on reactor thread then stop loop so we don't wait 2s
    schedule_on_loop(50, [this]() {
        tm->clear_session_timers("session_1");
        loop->stop();
    });

    start_loop();
    if (io_thread && io_thread->joinable())
    {
        io_thread->join();
    }
    io_thread.reset();

    EXPECT_EQ(timeout_count.load(), 0);
}

// ---------- Keepalive timer expiry ----------

TEST_F(TimerManagerTest, StartKeepaliveTimer)
{
    std::atomic<bool> timeout_called{false};

    tm->start_keepalive_timer("session_1", 0, [&timeout_called]() { timeout_called = true; });

    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(timeout_called.load());
}

// ---------- Keepalive timer cancellation ----------

TEST_F(TimerManagerTest, CancelKeepaliveTimer)
{
    std::atomic<bool> timeout_called{false};

    tm->start_keepalive_timer("session_1", 2, [&timeout_called]() { timeout_called = true; });

    schedule_on_loop(50, [this]() { tm->cancel_keepalive_timer("session_1"); });

    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_FALSE(timeout_called.load());
}

// ---------- Reset keepalive (placeholder) ----------

TEST_F(TimerManagerTest, ResetKeepaliveTimer)
{
    tm->start_keepalive_timer("session_1", 2, []() {});
    EXPECT_NO_THROW(tm->reset_keepalive_timer("session_1"));
}

// ---------- Clear mixed timer types ----------

TEST_F(TimerManagerTest, ClearMixedTimers)
{
    std::atomic<int> timeout_count{0};

    tm->start_ack_timer("session_1", "timestamp_1", 2, [&timeout_count]() { timeout_count++; });
    tm->start_keepalive_timer("session_1", 2, [&timeout_count]() { timeout_count++; });

    schedule_on_loop(50, [this]() {
        tm->clear_session_timers("session_1");
        loop->stop();
    });

    start_loop();
    if (io_thread && io_thread->joinable())
    {
        io_thread->join();
    }
    io_thread.reset();

    EXPECT_EQ(timeout_count.load(), 0);
}

// ---------- Immediate timeout (0s) ----------

TEST_F(TimerManagerTest, VeryShortTimeout)
{
    std::atomic<bool> timeout_called{false};

    tm->start_ack_timer("session_1", "timestamp_1", 0, [&timeout_called]() { timeout_called = true; });

    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_TRUE(timeout_called.load());
}

// ---------- Cancel from wrong session ----------

TEST_F(TimerManagerTest, CancelTimerWrongSession)
{
    std::atomic<bool> timeout_called{false};

    // 1s timeout — gives us time to attempt cancel from reactor thread
    tm->start_ack_timer("session_1", "timestamp_1", 1, [&timeout_called]() { timeout_called = true; });

    std::atomic<bool> cancel_result{true};
    schedule_on_loop(50,
                     [this, &cancel_result]() { cancel_result = tm->cancel_ack_timer("session_2", "timestamp_1"); });

    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    EXPECT_FALSE(cancel_result.load());
    EXPECT_TRUE(timeout_called.load());
}

// ---------- Multiple timers created on reactor thread ----------

TEST_F(TimerManagerTest, MultipleTimersFromReactorThread)
{
    std::atomic<int> timeout_count{0};

    for (int i = 0; i < 10; ++i)
    {
        std::string session_id = "session_" + std::to_string(i);
        schedule_on_loop(0, [this, session_id, &timeout_count]() {
            tm->start_ack_timer(session_id, "timestamp_1", 0, [&timeout_count]() { timeout_count++; });
        });
    }

    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(timeout_count.load(), 10);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
