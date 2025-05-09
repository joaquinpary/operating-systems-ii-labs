#include "config.h"
#include "unity.h"
#include <string.h>
#include <unistd.h>

void setUp(void)
{
    memset(get_identifiers(), 0, sizeof(identifiers));
}

void tearDown(void)
{
}

void test_set_session_token_and_get_identifiers(void)
{
    const char* test_token = "abc123";

    set_session_token(test_token);

    identifiers* id = get_identifiers();
    TEST_ASSERT_EQUAL_STRING(test_token, id->session_token);
}

void test_set_client_id_and_shm_path(void)
{
    const char* test_client = "client1";
    char expected_shm_path[SHM_PATH_SIZE];
    snprintf(expected_shm_path, sizeof(expected_shm_path), "/tmp/shm_client_%s", test_client);

    set_client_id(test_client);

    identifiers* id = get_identifiers();

    TEST_ASSERT_EQUAL_STRING(test_client, id->client_id);
    TEST_ASSERT_EQUAL_STRING(expected_shm_path, id->shm_path);

    FILE* f = fopen(id->shm_path, "r");
    TEST_ASSERT_NOT_NULL(f);
    if (f)
        fclose(f);
}

void test_set_client_type(void)
{
    const char* test_type = "warehouse";

    set_client_type(test_type);

    identifiers* id = get_identifiers();
    TEST_ASSERT_EQUAL_STRING(test_type, id->client_type);
}

void test_set_protocol(void)
{
    const char* test_protocol = "tcp";

    set_protocol(test_protocol);

    identifiers* id = get_identifiers();
    TEST_ASSERT_EQUAL_STRING(test_protocol, id->protocol);
}

void test_set_username(void)
{
    const char* test_username = "user";

    set_username(test_username);

    identifiers* id = get_identifiers();
    TEST_ASSERT_EQUAL_STRING(test_username, id->username);
}

void test_set_password(void)
{
    const char* test_password = "pass";

    set_password(test_password);

    identifiers* id = get_identifiers();
    TEST_ASSERT_EQUAL_STRING(test_password, id->password);
}

void test_set_params(void)
{
    init_params_client params = {
        .client_id = "test_client",
        .client_type = "warehouse",
        .username = "user_test",
        .password = "pass_test",
        .connection_params = {.protocol = "tcp", .host = "localhost", .port = "8080"},
    };

    set_params(params);

    identifiers* id = get_identifiers();
    TEST_ASSERT_EQUAL_STRING(params.client_id, id->client_id);
    TEST_ASSERT_EQUAL_STRING(params.client_type, id->client_type);
    TEST_ASSERT_EQUAL_STRING(params.username, id->username);
    TEST_ASSERT_EQUAL_STRING(params.password, id->password);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_set_session_token_and_get_identifiers);
    RUN_TEST(test_set_client_id_and_shm_path);
    RUN_TEST(test_set_client_type);
    RUN_TEST(test_set_protocol);
    RUN_TEST(test_set_username);
    RUN_TEST(test_set_password);
    RUN_TEST(test_set_params);
    return UNITY_END();
}
