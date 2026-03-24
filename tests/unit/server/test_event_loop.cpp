#include "event_loop.hpp"

#include <gtest/gtest.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

class EventLoopTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
    }
    void TearDown() override
    {
    }
};

TEST_F(EventLoopTest, ConstructionSucceeds)
{
    ASSERT_NO_THROW(event_loop loop);
}

TEST_F(EventLoopTest, InitiallyNotRunning)
{
    event_loop loop;
    EXPECT_FALSE(loop.is_running());
}

TEST_F(EventLoopTest, StopWithoutRunDoesNotCrash)
{
    event_loop loop;
    ASSERT_NO_THROW(loop.stop());
}

TEST_F(EventLoopTest, RunAndStopFromAnotherThread)
{
    event_loop loop;

    std::thread runner([&loop]() { loop.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(loop.is_running());

    loop.stop();
    runner.join();

    EXPECT_FALSE(loop.is_running());
}

TEST_F(EventLoopTest, AddFdAndReceiveCallback)
{
    event_loop loop;

    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_NE(efd, -1);

    bool callback_fired = false;

    loop.add_fd(efd, EPOLLIN, [&callback_fired, &loop](std::uint32_t) {
        callback_fired = true;
        loop.stop();
    });

    std::thread runner([&loop]() { loop.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::uint64_t val = 1;
    ASSERT_EQ(write(efd, &val, sizeof(val)), static_cast<ssize_t>(sizeof(val)));

    runner.join();
    ::close(efd);

    EXPECT_TRUE(callback_fired);
}

TEST_F(EventLoopTest, RemoveFdPreventsCallback)
{
    event_loop loop;

    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_NE(efd, -1);

    bool callback_fired = false;

    loop.add_fd(efd, EPOLLIN, [&callback_fired](std::uint32_t) { callback_fired = true; });

    loop.remove_fd(efd);

    std::thread runner([&loop]() { loop.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::uint64_t val = 1;
    (void)write(efd, &val, sizeof(val));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();
    runner.join();
    ::close(efd);

    EXPECT_FALSE(callback_fired);
}

TEST_F(EventLoopTest, ModifyFdEvents)
{
    event_loop loop;

    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_NE(efd, -1);

    bool callback_fired = false;

    loop.add_fd(efd, EPOLLIN, [&callback_fired, &loop](std::uint32_t) {
        callback_fired = true;
        loop.stop();
    });

    ASSERT_NO_THROW(loop.modify_fd(efd, EPOLLIN | EPOLLOUT));

    std::thread runner([&loop]() { loop.run(); });
    runner.join();

    ::close(efd);
    EXPECT_TRUE(callback_fired);
}

TEST_F(EventLoopTest, AddInvalidFdThrows)
{
    event_loop loop;
    EXPECT_THROW(loop.add_fd(-1, EPOLLIN, [](std::uint32_t) {}), std::runtime_error);
}

TEST_F(EventLoopTest, ModifyUnregisteredFdThrows)
{
    event_loop loop;
    EXPECT_THROW(loop.modify_fd(9999, EPOLLIN), std::runtime_error);
}

TEST_F(EventLoopTest, MultipleFdsReceiveCallbacks)
{
    event_loop loop;

    int pipefd1[2];
    int pipefd2[2];
    ASSERT_EQ(pipe(pipefd1), 0);
    ASSERT_EQ(pipe(pipefd2), 0);

    int fire_count = 0;

    loop.add_fd(pipefd1[0], EPOLLIN, [&fire_count, &pipefd1](std::uint32_t) {
        char buf;
        (void)read(pipefd1[0], &buf, 1);
        ++fire_count;
    });

    loop.add_fd(pipefd2[0], EPOLLIN, [&fire_count, &loop, &pipefd2](std::uint32_t) {
        char buf;
        (void)read(pipefd2[0], &buf, 1);
        ++fire_count;
        loop.stop();
    });

    std::thread runner([&loop]() { loop.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    char msg = 'a';
    ASSERT_EQ(write(pipefd1[1], &msg, 1), 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ASSERT_EQ(write(pipefd2[1], &msg, 1), 1);

    runner.join();
    ::close(pipefd1[0]);
    ::close(pipefd1[1]);
    ::close(pipefd2[0]);
    ::close(pipefd2[1]);

    EXPECT_EQ(fire_count, 2);
}

TEST_F(EventLoopTest, PipeFdTriggersCallback)
{
    event_loop loop;

    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    bool callback_fired = false;

    loop.add_fd(pipefd[0], EPOLLIN, [&callback_fired, &loop](std::uint32_t) {
        callback_fired = true;
        loop.stop();
    });

    std::thread runner([&loop]() { loop.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    char msg = 'x';
    ASSERT_EQ(write(pipefd[1], &msg, 1), 1);

    runner.join();
    ::close(pipefd[0]);
    ::close(pipefd[1]);

    EXPECT_TRUE(callback_fired);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
