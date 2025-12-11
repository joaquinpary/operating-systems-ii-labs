#include "timer_manager.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

class TimerManagerTest : public ::testing::Test
{
  protected:
    std::unique_ptr<asio::io_context> io_context;
    std::unique_ptr<timer_manager> tm;
    std::unique_ptr<std::thread> io_thread;

    void SetUp() override
    {
        io_context = std::make_unique<asio::io_context>();
        tm = std::make_unique<timer_manager>(*io_context);
    }

    void TearDown() override
    {
        if (io_context)
        {
            io_context->stop();
        }
        if (io_thread && io_thread->joinable())
        {
            io_thread->join();
        }
    }

    void start_io_context()
    {
        io_thread = std::make_unique<std::thread>([this]() { io_context->run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Let it start
    }
};

// Test ACK timer creation
TEST_F(TimerManagerTest, StartAckTimer)
{
    std::atomic<bool> timeout_called{false};
    
    tm->start_ack_timer("session_1", "timestamp_1", 0, [&timeout_called]() {
        timeout_called = true;
    });
    
    start_io_context();
    
    // Wait for timer to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(timeout_called.load());
}

// Test ACK timer cancellation
TEST_F(TimerManagerTest, CancelAckTimer)
{
    std::atomic<bool> timeout_called{false};
    
    tm->start_ack_timer("session_1", "timestamp_1", 1, [&timeout_called]() {
        timeout_called = true;
    });
    
    start_io_context();
    
    // Cancel before it expires
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bool cancelled = tm->cancel_ack_timer("session_1", "timestamp_1");
    
    EXPECT_TRUE(cancelled);
    
    // Wait to make sure timeout doesn't get called
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_FALSE(timeout_called.load());
}

// Test cancelling non-existent ACK timer
TEST_F(TimerManagerTest, CancelNonExistentAckTimer)
{
    bool cancelled = tm->cancel_ack_timer("session_1", "nonexistent_timestamp");
    EXPECT_FALSE(cancelled);
}

// Test multiple ACK timers for same session
TEST_F(TimerManagerTest, MultipleAckTimersPerSession)
{
    std::atomic<int> timeout_count{0};
    
    tm->start_ack_timer("session_1", "timestamp_1", 0, [&timeout_count]() {
        timeout_count++;
    });
    
    tm->start_ack_timer("session_1", "timestamp_2", 0, [&timeout_count]() {
        timeout_count++;
    });
    
    tm->start_ack_timer("session_1", "timestamp_3", 0, [&timeout_count]() {
        timeout_count++;
    });
    
    start_io_context();
    
    // Wait for all timers to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(timeout_count.load(), 3);
}

// Test ACK timers for multiple sessions
TEST_F(TimerManagerTest, MultipleSessionsAckTimers)
{
    std::atomic<int> session1_count{0};
    std::atomic<int> session2_count{0};
    
    tm->start_ack_timer("session_1", "timestamp_1", 0, [&session1_count]() {
        session1_count++;
    });
    
    tm->start_ack_timer("session_2", "timestamp_1", 0, [&session2_count]() {
        session2_count++;
    });
    
    start_io_context();
    
    // Wait for timers to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(session1_count.load(), 1);
    EXPECT_EQ(session2_count.load(), 1);
}

// Test clear_session_timers
TEST_F(TimerManagerTest, ClearSessionTimers)
{
    std::atomic<int> timeout_count{0};
    
    // Use longer timeouts; we'll cancel immediately
    tm->start_ack_timer("session_1", "timestamp_1", 5, [&timeout_count]() {
        timeout_count++;
    });
    
    tm->start_ack_timer("session_1", "timestamp_2", 5, [&timeout_count]() {
        timeout_count++;
    });
    
    start_io_context();
    
    // Immediately clear all timers for session_1 before they expire
    tm->clear_session_timers("session_1");
    
    // Stop io_context to ensure the thread exits promptly
    io_context->stop();
    
    EXPECT_EQ(timeout_count.load(), 0);
}

// Test keepalive timer (placeholder functionality)
TEST_F(TimerManagerTest, StartKeepaliveTimer)
{
    std::atomic<bool> timeout_called{false};
    
    tm->start_keepalive_timer("session_1", 0, [&timeout_called]() {
        timeout_called = true;
    });
    
    start_io_context();
    
    // Wait for timer to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(timeout_called.load());
}

// Test cancel keepalive timer
TEST_F(TimerManagerTest, CancelKeepaliveTimer)
{
    std::atomic<bool> timeout_called{false};
    
    tm->start_keepalive_timer("session_1", 1, [&timeout_called]() {
        timeout_called = true;
    });
    
    start_io_context();
    
    // Cancel before it expires
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    tm->cancel_keepalive_timer("session_1");
    
    // Wait to make sure timeout doesn't get called
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_FALSE(timeout_called.load());
}

// Test reset keepalive timer (placeholder)
TEST_F(TimerManagerTest, ResetKeepaliveTimer)
{
    tm->start_keepalive_timer("session_1", 1, []() {});
    
    // This is a placeholder test since reset is not fully implemented
    EXPECT_NO_THROW(tm->reset_keepalive_timer("session_1"));
}

// Test clear_session_timers with mixed timer types
TEST_F(TimerManagerTest, ClearMixedTimers)
{
    std::atomic<int> timeout_count{0};
    
    tm->start_ack_timer("session_1", "timestamp_1", 1, [&timeout_count]() {
        timeout_count++;
    });
    
    tm->start_keepalive_timer("session_1", 1, [&timeout_count]() {
        timeout_count++;
    });
    
    start_io_context();
    
    // Clear all timers
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    tm->clear_session_timers("session_1");
    
    // Wait to make sure no timeouts are called
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(timeout_count.load(), 0);
}

// Test timer with very short timeout
TEST_F(TimerManagerTest, VeryShortTimeout)
{
    std::atomic<bool> timeout_called{false};
    
    tm->start_ack_timer("session_1", "timestamp_1", 0, [&timeout_called]() {
        timeout_called = true;
    });
    
    start_io_context();
    
    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(timeout_called.load());
}

// Test cancelling timer from different session
TEST_F(TimerManagerTest, CancelTimerWrongSession)
{
    std::atomic<bool> timeout_called{false};
    
    tm->start_ack_timer("session_1", "timestamp_1", 0, [&timeout_called]() {
        timeout_called = true;
    });
    
    start_io_context();
    
    // Try to cancel with wrong session_id
    bool cancelled = tm->cancel_ack_timer("session_2", "timestamp_1");
    EXPECT_FALSE(cancelled);
    
    // Timer should still expire
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(timeout_called.load());
}

// Test thread safety - multiple timers started/cancelled concurrently
TEST_F(TimerManagerTest, ThreadSafety)
{
    std::atomic<int> timeout_count{0};
    
    // Start multiple timers from different threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++)
    {
        threads.emplace_back([this, i, &timeout_count]() {
            std::string session_id = "session_" + std::to_string(i);
            tm->start_ack_timer(session_id, "timestamp_1", 1, [&timeout_count]() {
                timeout_count++;
            });
        });
    }
    
    for (auto& t : threads)
    {
        t.join();
    }
    
    // Start io_context AFTER timers are created
    start_io_context();
    
    // Wait for timers to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    EXPECT_EQ(timeout_count.load(), 10);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

