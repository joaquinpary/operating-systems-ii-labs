cmake_minimum_required(VERSION 3.22)
project(DHL_LOGISTICS
        LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)  # C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)  # C17
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_TARGET "Build target: server or client" "server")
option(RUN_TEST "Run tests" OFF)
option(ENABLE_MONGO_REST_STORAGE "Enable MongoDB-backed REST storage for the server API" ON)
option(FETCH_MONGOCXX_IF_MISSING "Fetch mongo-cxx-driver with FetchContent when system packages are unavailable" ON)

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

    set(MONGO_CXX_DRIVER_GIT_URL "https://github.com/mongodb/mongo-cxx-driver.git")
    set(MONGO_CXX_DRIVER_GIT_TAG "r4.1.4")

    set(CPP_HTTPLIB_GIT_URL "https://github.com/yhirose/cpp-httplib.git")
    FetchContent_Declare(
        cpp_httplib
        GIT_REPOSITORY ${CPP_HTTPLIB_GIT_URL}
        GIT_TAG        master
        SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/external/server/cpp-httplib
        UPDATE_COMMAND ""
    )
    FetchContent_MakeAvailable(cpp_httplib)

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
    include_directories(${cpp_httplib_SOURCE_DIR})

    find_package(PostgreSQL REQUIRED)
    include_directories(${PostgreSQL_INCLUDE_DIRS})

    # Shared library: libadmin_cli.so (admin CLI plugin, loaded via dlopen)
    add_library(admin_cli SHARED
        src/server/admin_cli/admin_cli.c
    )
    target_include_directories(admin_cli PRIVATE
        include/server
        ${cjson_SOURCE_DIR}
        ${PostgreSQL_INCLUDE_DIRS}
    )
    target_link_libraries(admin_cli PRIVATE cjson ${PostgreSQL_LIBRARIES})
    set_target_properties(admin_cli PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        SOVERSION 1
        VERSION 1.0.0
    )

    set(MONGO_LINK_TARGETS "")
    if(ENABLE_MONGO_REST_STORAGE)
        find_package(mongocxx QUIET)
        find_package(bsoncxx QUIET)

        if(mongocxx_FOUND AND bsoncxx_FOUND)
            list(APPEND MONGO_LINK_TARGETS mongo::mongocxx_shared mongo::bsoncxx_shared)
        elseif(FETCH_MONGOCXX_IF_MISSING)
            set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
            set(BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
            set(BUILD_TESTING OFF CACHE BOOL "Disable subproject testing" FORCE)
            set(ENABLE_TESTS OFF CACHE BOOL "Disable subproject tests" FORCE)
            set(ENABLE_UNINSTALL OFF CACHE BOOL "Disable subproject uninstall target" FORCE)
            set(MONGOC_ENABLE_CLIENT_SIDE_ENCRYPTION OFF CACHE BOOL "Disable libmongocrypt support" FORCE)
            set(BUILD_VERSION "4.1.4" CACHE STRING "Library version (for both bsoncxx and mongocxx)" FORCE)

            FetchContent_Declare(
                mongo_cxx_driver
                GIT_REPOSITORY ${MONGO_CXX_DRIVER_GIT_URL}
                GIT_TAG        ${MONGO_CXX_DRIVER_GIT_TAG}
                SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/external/server/mongo-cxx-driver
                UPDATE_COMMAND ""
            )
            FetchContent_MakeAvailable(mongo_cxx_driver)

            if(TARGET mongocxx_shared AND TARGET bsoncxx_shared)
                list(APPEND MONGO_LINK_TARGETS mongocxx_shared bsoncxx_shared)
            elseif(TARGET mongo::mongocxx_shared AND TARGET mongo::bsoncxx_shared)
                list(APPEND MONGO_LINK_TARGETS mongo::mongocxx_shared mongo::bsoncxx_shared)
            else()
                message(FATAL_ERROR "mongo-cxx-driver was fetched but the expected targets were not created")
            endif()
        endif()
    endif()
    
    file(GLOB SOURCES
        src/server/*.cpp
        src/common/*.cpp
        src/common/*.c
        include/server/*.hpp
    )
    set(TARGET_EXEC dhl_server)

    add_executable(${TARGET_EXEC} ${SOURCES})
    target_link_libraries(${TARGET_EXEC} pthread rt dl)
    target_link_libraries(${TARGET_EXEC} pqxx ${PostgreSQL_LIBRARIES})
    set_target_properties(${TARGET_EXEC} PROPERTIES
        BUILD_RPATH  "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
    )

    if(MONGO_LINK_TARGETS)
        target_compile_definitions(${TARGET_EXEC} PRIVATE HAVE_MONGOCXX=1)
        target_link_libraries(${TARGET_EXEC} ${MONGO_LINK_TARGETS})
    else()
        message(STATUS "mongocxx/bsoncxx not found: REST API will use in-memory storage for now")
    endif()

    # Embed build-tree location of libadmin_cli.so into RPATH
    set_target_properties(${TARGET_EXEC} PROPERTIES
        BUILD_RPATH  "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
    )


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
