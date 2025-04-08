cmake_minimum_required(VERSION 3.22 FATAL_ERROR)

# Collect test
file(GLOB CLIENT_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/client/*.c)
file(GLOB SERVER_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/server/*.cpp)
file(GLOB COMMON_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/common/*.c)

# Collect source files
file(GLOB CLIENT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src/client/*.c)
file(GLOB SERVER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src/server/*.cpp)
file(GLOB COMMON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/*.c)

list(FILTER CLIENT_SRC EXCLUDE REGEX ".*main\\.c$")
list(FILTER SERVER_SRC EXCLUDE REGEX ".*main\\.cpp$")
list(FILTER COMMON_SRC EXCLUDE REGEX ".*main\\.c$")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include/common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include/server)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include/client)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../external/common/cjson) 

list(APPEND COMMON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../external/common/cjson/cJSON.c)

# Link with Unity and gtest
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../external/client/unity/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../external/server/gtest/googletest/include)

# Coverage (Optional)
if(RUN_COVERAGE)
    message("Coverage enabled")
    find_program(LCOV_PATH /usr/bin/lcov)
        if(NOT LCOV_PATH)
        message(FATAL_ERROR "lcov not found! Aborting...")
    endif()

    find_program(GCOV_PATH gcov)
        if(NOT GCOV_PATH)
        message(FATAL_ERROR "gcov not found! Aborting...")
    endif()
    
    find_program(GENHTML_PATH genhtml)
        if(NOT GENHTML_PATH)
        message(FATAL_ERROR "genhtml not found! Aborting...")
    endif()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fprofile-arcs -ftest-coverage --coverage")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fprofile-arcs -ftest-coverage --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgcov --coverage")
endif()

option(TEST_TARGET "Test target: server, client or common")

if(TEST_TARGET STREQUAL "client")
    add_executable(test_client ${CLIENT_TEST} ${CLIENT_SRC} ${COMMON_SRC})
    target_link_libraries(test_client unity)
    add_test(NAME test_client COMMAND test_client)

elseif(TEST_TARGET STREQUAL "server")
    add_executable(test_server ${SERVER_TEST} ${SERVER_SRC} ${COMMON_SRC})
    target_link_libraries(test_server gtest gtest_main pthread)
    add_test(NAME test_server COMMAND test_server)

elseif(TEST_TARGET STREQUAL "common")
    add_executable(test_common ${COMMON_TEST} ${COMMON_SRC})
    target_link_libraries(test_common unity)
    add_test(NAME test_common COMMAND test_common)

else()
    message(FATAL_ERROR "Invalid test target specified. Use 'client', 'server', or 'common'.")
endif()

