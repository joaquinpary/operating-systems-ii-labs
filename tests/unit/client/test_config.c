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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_set_session_token_and_get_identifiers);
    RUN_TEST(test_set_client_id_and_shm_path);
    return UNITY_END();
}
