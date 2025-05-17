cmake_minimum_required(VERSION 3.22)
project(DHL_LOGISTICS
        LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)  # C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)  # C17
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_TARGET "Build target: server or client" "server")
option(RUN_TEST "Run tests" OFF)

# Download UNITY
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(UNITY_GIT_URL "https://github.com/ThrowTheSwitch/Unity.git")
FetchContent_Declare(
    unity
    GIT_REPOSITORY ${UNITY_GIT_URL}
    GIT_TAG        master
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/external/client/unity
    UPDATE_COMMAND ""
)
FetchContent_MakeAvailable(unity)

# DownLoad gtest
set(GTEST_GIT_URL "https://github.com/google/googletest.git")
FetchContent_Declare(
    gtest
    GIT_REPOSITORY ${GTEST_GIT_URL}
    GIT_TAG        main
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/external/server/gtest
    UPDATE_COMMAND ""
)
FetchContent_MakeAvailable(gtest)

set(CJSON_GIT_URL "https://github.com/DaveGamble/cJSON.git")
set(ENABLE_CJSON_TEST OFF CACHE INTERNAL "Disable cJSON tests")
FetchContent_Declare(
    cjson
    GIT_REPOSITORY ${CJSON_GIT_URL}
    GIT_TAG        master
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/external/common/cjson
    UPDATE_COMMAND ""
)
FetchContent_MakeAvailable(cjson)

# ASIO
set(ASIO_GIT_URL "https://github.com/chriskohlhoff/asio.git")
FetchContent_Declare(
    asio
    GIT_REPOSITORY ${ASIO_GIT_URL}
    GIT_TAG        master
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/external/server/asio
    UPDATE_COMMAND ""
)
FetchContent_MakeAvailable(asio)

include_directories(include)
include_directories(include/common)

if(BUILD_TARGET STREQUAL "server")
    add_definitions(-DTARGET_SERVER)
    include_directories(include/server)
    include_directories(include/common)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/server/asio/asio/include)
    find_package(PkgConfig REQUIRED)
    find_package(PostgreSQL REQUIRED)
    
    pkg_check_modules(LIBPQXX REQUIRED libpqxx)
    include_directories(${LIBPQXX_INCLUDE_DIRS})
    link_directories(${LIBPQXX_LIBRARY_DIRS})
    
    file(GLOB SOURCES
        include/server/*.hpp
        include/common/*.h    
        src/common/*.c
        src/common/*.cpp
        src/server/*.cpp
    )
    set(TARGET_EXEC dhl_server)
    add_executable(${TARGET_EXEC} ${SOURCES})
    target_link_libraries(${TARGET_EXEC} PRIVATE pthread ${LIBPQXX_LIBRARIES} pq)


elseif(BUILD_TARGET STREQUAL "client")
    add_definitions(-DTARGET_CLIENT)
    include_directories(include/client)
    file(GLOB SOURCES
        src/client/*.c
        src/common/*.c
        include/common/*.h
        include/client/*.h
    )
    set(TARGET_EXEC dhl_client)
    add_executable(${TARGET_EXEC} ${SOURCES})
    
else()
    message(FATAL_ERROR "Invalid TARGET. Use -DTARGET=server or -DTARGET=client")
endif()

find_package(ZLIB REQUIRED)
target_include_directories(${TARGET_EXEC} PRIVATE ${cjson_SOURCE_DIR})
target_link_libraries(${TARGET_EXEC} PRIVATE cjson ZLIB::ZLIB)

if(RUN_TEST EQUAL 1 OR RUN_COVERAGE EQUAL 1)
    enable_testing()
    add_subdirectory(tests)
endif()
