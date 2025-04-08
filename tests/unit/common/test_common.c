#include "unity.h"
#include "json_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// Setup de un archivo JSON válido
void create_valid_json_file() {
    FILE* f = fopen("test_config.json", "w");
    if (f) {
        fputs(
            "{\n"
            "  \"client\": {\n"
            "    \"host\": \"example.com\",\n"
            "    \"port\": \"8000\",\n"
            "    \"ip_version\": \"ipv4\",\n"
            "    \"protocol\": \"tcp\"\n"
            "  }\n"
            "}\n", f);
        fclose(f);
    }
}

// Setup de un JSON malformado
void create_bad_json_file() {
    FILE* f = fopen("bad.json", "w");
    if (f) {
        fputs("{ invalid json", f);
        fclose(f);
    }
}

void test_valid_config(void) {
    create_valid_json_file();

    init_params_client p = load_config_client("test_config.json", "client");

    TEST_ASSERT_EQUAL_STRING("example.com", p.host);
    TEST_ASSERT_EQUAL_STRING("8000", p.port);
    TEST_ASSERT_EQUAL_STRING("ipv4", p.ip_version);
    TEST_ASSERT_EQUAL_STRING("tcp", p.protocol);

    remove("test_config.json");
}

void test_invalid_file(void) {
    init_params_client p = load_config_client("no_file.json", "client");

    TEST_ASSERT_EQUAL_STRING("", p.host);
    TEST_ASSERT_EQUAL_STRING("", p.port);
    TEST_ASSERT_EQUAL_STRING("", p.ip_version);
    TEST_ASSERT_EQUAL_STRING("", p.protocol);
}

void test_invalid_section(void) {
    create_valid_json_file();

    init_params_client p = load_config_client("test_config.json", "wrong_section");

    TEST_ASSERT_EQUAL_STRING("", p.host);
    TEST_ASSERT_EQUAL_STRING("", p.port);
    TEST_ASSERT_EQUAL_STRING("", p.ip_version);
    TEST_ASSERT_EQUAL_STRING("", p.protocol);

    remove("test_config.json");
}

void test_bad_json(void) {
    create_bad_json_file();

    init_params_client p = load_config_client("bad.json", "client");

    TEST_ASSERT_EQUAL_STRING("", p.host);
    TEST_ASSERT_EQUAL_STRING("", p.port);
    TEST_ASSERT_EQUAL_STRING("", p.ip_version);
    TEST_ASSERT_EQUAL_STRING("", p.protocol);

    remove("bad.json");
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_valid_config);
    RUN_TEST(test_invalid_file);
    RUN_TEST(test_invalid_section);
    RUN_TEST(test_bad_json);

    return UNITY_END();
}
