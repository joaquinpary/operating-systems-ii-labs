cmake_minimum_required(VERSION 3.22 FATAL_ERROR)

# Collect test
file(GLOB CLIENT_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/client/*.c)
file(GLOB SERVER_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/server/*.cpp)
file(GLOB COMMON_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/common/*.c)

# Exclude main test files
list(FILTER CLIENT_TEST EXCLUDE REGEX ".*test_server\\.c$")

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

message(STATUS "Setting up multiple client test executables...")

set(test_server_file ${CMAKE_CURRENT_SOURCE_DIR}/unit/client/test_server.c)

foreach(test_file ${CLIENT_TEST})
    get_filename_component(test_name ${test_file} NAME_WE)
    set(exec_name test_client_${test_name})
    add_executable(${exec_name} ${test_file} ${CLIENT_SRC} ${COMMON_SRC} ${test_server_file})
    target_link_libraries(${exec_name} PRIVATE unity pthread)
    target_compile_definitions(${exec_name} PRIVATE TESTING)
    add_test(NAME ${exec_name} COMMAND ${exec_name})
endforeach()

message(STATUS "Setting up multiple common test executables...")

foreach(test_file ${COMMON_TEST})
    get_filename_component(test_name ${test_file} NAME_WE)
    set(exec_name test_common_${test_name})
    add_executable(${exec_name} ${test_file} ${COMMON_SRC})
    target_link_libraries(${exec_name} PRIVATE unity) #ZLIB::ZLIB
    target_compile_definitions(${exec_name} PRIVATE TESTING)
    add_test(NAME ${exec_name} COMMAND ${exec_name})
endforeach()

message(STATUS "Setting up server test executables...")

foreach(test_file ${SERVER_TEST})
    get_filename_component(test_name ${test_file} NAME_WE)
    set(exec_name test_server_${test_name})
    add_executable(${exec_name} ${test_file} ${SERVER_SRC} ${COMMON_SRC})
    target_include_directories(${exec_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/include/server
        ${PROJECT_SOURCE_DIR}/include/common
        ${CMAKE_CURRENT_SOURCE_DIR}/external/server/asio/asio/include
        ${LIBPQXX_INCLUDE_DIRS}
    )
    target_link_libraries(${exec_name} PRIVATE
        gtest
        gtest_main
        pthread
        # ZLIB::ZLIB
        ${LIBPQXX_LIBRARIES}
        pq
    )
    target_compile_definitions(${exec_name} PRIVATE TESTING)

    add_test(NAME ${exec_name} COMMAND ${exec_name})
endforeach()