cmake_minimum_required(VERSION 3.22 FATAL_ERROR)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Download UNITY
set(UNITY_GIT_URL "https://github.com/ThrowTheSwitch/Unity.git")
FetchContent_Declare(
    unity
    GIT_REPOSITORY ${UNITY_GIT_URL}
    GIT_TAG        master
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/../external/client/unity
    UPDATE_COMMAND ""
)
FetchContent_MakeAvailable(unity)

# DownLoad gtest
set(GTEST_GIT_URL "https://github.com/google/googletest.git")
FetchContent_Declare(
    gtest
    GIT_REPOSITORY ${GTEST_GIT_URL}
    GIT_TAG        main
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/../external/server/gtest
    UPDATE_COMMAND ""
)
FetchContent_MakeAvailable(gtest)

# Collect test
file(GLOB CLIENT_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/client/*.c)
file(GLOB SERVER_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/server/*.cpp)
file(GLOB COMMON_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/common/*.c)

# Exclude main test files and helper files
list(FILTER CLIENT_TEST EXCLUDE REGEX ".*mock_server\\.c$")

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
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include/client/emergency)

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
    
    if(test_name STREQUAL "test_logic_static")
        set(CLIENT_SRC_NO_LOGIC ${CLIENT_SRC})
        list(FILTER CLIENT_SRC_NO_LOGIC EXCLUDE REGEX ".*logic\\.c$")
        
        add_executable(${exec_name} ${test_file} ${CLIENT_SRC_NO_LOGIC} ${COMMON_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/unit/client/mock_server.c)
        
        target_compile_options(${exec_name} PRIVATE -Wno-missing-prototypes)
        target_link_options(${exec_name} PRIVATE 
            -Wl,--unresolved-symbols=ignore-all
            -Wl,-zmuldefs
            -no-pie
        )
        
        target_link_libraries(${exec_name} PRIVATE unity pthread)
        target_compile_definitions(${exec_name} PRIVATE TESTING)
        add_test(NAME ${exec_name} COMMAND ${exec_name})
    elseif(test_name STREQUAL "test_client_static")
        set(CLIENT_SRC_NO_CLIENT ${CLIENT_SRC})
        list(FILTER CLIENT_SRC_NO_CLIENT EXCLUDE REGEX ".*client\\.c$")
        
        add_executable(${exec_name} ${test_file} ${CLIENT_SRC_NO_CLIENT} ${COMMON_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/unit/client/mock_server.c)
        
        target_compile_options(${exec_name} PRIVATE -Wno-missing-prototypes)
        target_link_options(${exec_name} PRIVATE 
            -Wl,--unresolved-symbols=ignore-all
            -Wl,-zmuldefs
            -no-pie
        )
        
        target_link_libraries(${exec_name} PRIVATE unity pthread)
        target_compile_definitions(${exec_name} PRIVATE TESTING)
        add_test(NAME ${exec_name} COMMAND ${exec_name})
    else()
        add_executable(${exec_name} ${test_file} ${CLIENT_SRC} ${COMMON_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/unit/client/mock_server.c)
        target_link_libraries(${exec_name} PRIVATE unity pthread)
        target_compile_definitions(${exec_name} PRIVATE TESTING)
        add_test(NAME ${exec_name} COMMAND ${exec_name})
    endif()
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
    
    # Find PostgreSQL for server tests
    find_package(PostgreSQL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MONGOC REQUIRED IMPORTED_TARGET libmongoc-1.0)
    pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)

    target_include_directories(${exec_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/include/server
        ${PROJECT_SOURCE_DIR}/include/common
        ${PostgreSQL_INCLUDE_DIRS}
    )
    target_link_libraries(${exec_name} PRIVATE
        gtest
        gtest_main
        pthread
        rt
        pqxx
        PkgConfig::MONGOC
        PkgConfig::MOSQUITTO
        ${PostgreSQL_LIBRARIES}
    )
    target_compile_definitions(${exec_name} PRIVATE TESTING)

    if(ENABLE_OPENMP_FLOW OR ENABLE_OPENMP_CIRCUIT)
        find_package(OpenMP REQUIRED)
        target_link_libraries(${exec_name} PRIVATE OpenMP::OpenMP_CXX)
        if(ENABLE_OPENMP_FLOW)
            target_compile_definitions(${exec_name} PRIVATE USE_OPENMP_FLOW=1)
        endif()
        if(ENABLE_OPENMP_CIRCUIT)
            target_compile_definitions(${exec_name} PRIVATE USE_OPENMP_CIRCUIT=1)
        endif()
    endif()

    add_test(NAME ${exec_name} COMMAND ${exec_name})
endforeach()

message(STATUS "Setting up emergency detector test executables...")

file(GLOB EMERGENCY_TEST ${CMAKE_CURRENT_SOURCE_DIR}/unit/emergency/*.c)
file(GLOB EMERGENCY_SRC  ${CMAKE_CURRENT_SOURCE_DIR}/../src/client/emergency/*.c)

foreach(test_file ${EMERGENCY_TEST})
    get_filename_component(test_name ${test_file} NAME_WE)
    set(exec_name test_emergency_${test_name})

    add_executable(${exec_name} ${test_file} ${EMERGENCY_SRC})

    target_include_directories(${exec_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include/client/emergency
    )
    target_compile_options(${exec_name} PRIVATE -fPIC)
    target_link_libraries(${exec_name} PRIVATE unity)
    target_compile_definitions(${exec_name} PRIVATE TESTING)

    add_test(NAME ${exec_name} COMMAND ${exec_name})
endforeach()
