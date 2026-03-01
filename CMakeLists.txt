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



include_directories(include)
include_directories(include/common)

if(BUILD_TARGET STREQUAL "server")

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

    set(PQXX_GIT_URL "https://github.com/jtv/libpqxx.git")
    FetchContent_Declare(
        pqxx
        GIT_REPOSITORY ${PQXX_GIT_URL}
        GIT_TAG        master
        SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/external/server/pqxx
        UPDATE_COMMAND ""
    )
    FetchContent_MakeAvailable(pqxx)
    add_definitions(-DTARGET_SERVER)
    include_directories(include/server)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/server/asio/asio/include)
    
    find_package(PostgreSQL REQUIRED)
    include_directories(${PostgreSQL_INCLUDE_DIRS})
    
    file(GLOB SOURCES
        src/server/*.cpp
        src/common/*.cpp
        src/common/*.c
        include/server/*.hpp
    )
    set(TARGET_EXEC dhl_server)
    add_executable(${TARGET_EXEC} ${SOURCES})
    target_link_libraries(${TARGET_EXEC} pthread)
    target_link_libraries(${TARGET_EXEC} pqxx ${PostgreSQL_LIBRARIES})


elseif(BUILD_TARGET STREQUAL "client")
    add_definitions(-DTARGET_CLIENT)
    include_directories(include/client)
    include_directories(include/client/emergency)

    # Shared library: libemergency.so
    add_library(emergency SHARED
        src/client/emergency/emergency_detector.c
    )
    target_include_directories(emergency PUBLIC include/client/emergency)
    set_target_properties(emergency PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        SOVERSION 1
        VERSION 1.0.0
    )

    file(GLOB SOURCES
        src/client/*.c
        src/common/*.c
        include/client/*.h
    )
    set(TARGET_EXEC dhl_client)
    add_executable(${TARGET_EXEC} ${SOURCES})
    target_link_libraries(${TARGET_EXEC} dl)

    # Embed the build-tree location of libemergency.so into the executable RPATH
    # so that dlopen("libemergency.so") resolves correctly when running locally
    # (outside Docker). In Docker, ldconfig makes it available system-wide.
    set_target_properties(${TARGET_EXEC} PROPERTIES
        BUILD_RPATH  "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
    )

else()
    message(FATAL_ERROR "Invalid TARGET. Use -DTARGET=server or -DTARGET=client")
endif()

target_include_directories(${TARGET_EXEC} PRIVATE ${cjson_SOURCE_DIR})
target_link_libraries(${TARGET_EXEC} cjson)

if(RUN_TEST EQUAL 1 OR RUN_COVERAGE EQUAL 1)
    enable_testing()
    add_subdirectory(tests)
endif()
