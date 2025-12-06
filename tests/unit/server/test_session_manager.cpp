#include <gtest/gtest.h>
#include <server/session_manager.hpp>

class SessionManagerTest : public ::testing::Test
{
  protected:
    std::unique_ptr<session_manager> mgr;

    void SetUp() override
    {
        mgr = std::make_unique<session_manager>();
    }

    void TearDown() override
    {
        mgr.reset();
    }
};

TEST_F(SessionManagerTest, CreateSessionReturnsUniqueId)
{
    std::string session1 = mgr->create_session();
    std::string session2 = mgr->create_session();

    EXPECT_FALSE(session1.empty());
    EXPECT_FALSE(session2.empty());
    EXPECT_NE(session1, session2);
}

TEST_F(SessionManagerTest, NewSessionIsNotAuthenticated)
{
    std::string session_id = mgr->create_session();
    EXPECT_FALSE(mgr->is_authenticated(session_id));
}

TEST_F(SessionManagerTest, MarkAuthenticatedSetsAuthStatus)
{
    std::string session_id = mgr->create_session();
    mgr->mark_authenticated(session_id, "HUB", "test_user");

    EXPECT_TRUE(mgr->is_authenticated(session_id));
}

TEST_F(SessionManagerTest, GetSessionInfoReturnsCorrectData)
{
    std::string session_id = mgr->create_session();
    mgr->mark_authenticated(session_id, "WAREHOUSE", "warehouse_001");

    auto info = mgr->get_session_info(session_id);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->session_id, session_id);
    EXPECT_TRUE(info->is_authenticated);
    EXPECT_EQ(info->client_type, "WAREHOUSE");
    EXPECT_EQ(info->username, "warehouse_001");
}

TEST_F(SessionManagerTest, GetSessionInfoReturnsNullForInvalidSession)
{
    auto info = mgr->get_session_info("non_existent_session");
    EXPECT_EQ(info, nullptr);
}

TEST_F(SessionManagerTest, RemoveSessionDeletesSession)
{
    std::string session_id = mgr->create_session();
    mgr->mark_authenticated(session_id, "HUB", "hub_001");

    EXPECT_TRUE(mgr->is_authenticated(session_id));

    mgr->remove_session(session_id);

    EXPECT_FALSE(mgr->is_authenticated(session_id));
    auto info = mgr->get_session_info(session_id);
    EXPECT_EQ(info, nullptr);
}

TEST_F(SessionManagerTest, GetClientTypeReturnsCorrectType)
{
    std::string session_id = mgr->create_session();
    mgr->mark_authenticated(session_id, "HUB", "hub_002");

    std::string client_type = mgr->get_client_type(session_id);
    EXPECT_EQ(client_type, "HUB");
}

TEST_F(SessionManagerTest, GetClientTypeReturnsEmptyForUnauthenticated)
{
    std::string session_id = mgr->create_session();
    std::string client_type = mgr->get_client_type(session_id);
    EXPECT_TRUE(client_type.empty());
}

TEST_F(SessionManagerTest, MultipleConcurrentSessions)
{
    std::vector<std::string> sessions;
    for (int i = 0; i < 100; ++i)
    {
        sessions.push_back(mgr->create_session());
    }

    for (size_t i = 0; i < sessions.size(); ++i)
    {
        mgr->mark_authenticated(sessions[i], "HUB", "user_" + std::to_string(i));
    }

    for (size_t i = 0; i < sessions.size(); ++i)
    {
        EXPECT_TRUE(mgr->is_authenticated(sessions[i]));
        auto info = mgr->get_session_info(sessions[i]);
        ASSERT_NE(info, nullptr);
        EXPECT_EQ(info->username, "user_" + std::to_string(i));
    }
}

