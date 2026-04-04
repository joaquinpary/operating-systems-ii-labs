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
option(ENABLE_OPENMP_FLOW "Enable OpenMP parallelization for flow algorithm" OFF)
option(ENABLE_OPENMP_CIRCUIT "Enable OpenMP parallelization for circuit algorithm" OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

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

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MONGOC REQUIRED IMPORTED_TARGET libmongoc-1.0)

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

    # Shared library: libapi_gateway.so (API gateway plugin, loaded via dlopen)
    add_library(api_gateway SHARED
        src/server/api_gateway/api_gateway.c
        src/common/json_manager.c
    )
    target_include_directories(api_gateway PRIVATE
        include
        include/server
        ${cjson_SOURCE_DIR}
        ${PostgreSQL_INCLUDE_DIRS}
    )
    target_link_libraries(api_gateway PRIVATE cjson ${PostgreSQL_LIBRARIES})
    set_target_properties(api_gateway PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        SOVERSION 1
        VERSION 1.0.0
    )
    
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
    target_link_libraries(${TARGET_EXEC} PkgConfig::MONGOC)
    set_target_properties(${TARGET_EXEC} PROPERTIES
        BUILD_RPATH  "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
    )

    if(ENABLE_OPENMP_FLOW OR ENABLE_OPENMP_CIRCUIT)
        find_package(OpenMP REQUIRED)
        target_link_libraries(${TARGET_EXEC} OpenMP::OpenMP_CXX)
        if(ENABLE_OPENMP_FLOW)
            target_compile_definitions(${TARGET_EXEC} PRIVATE USE_OPENMP_FLOW=1)
        endif()
        if(ENABLE_OPENMP_CIRCUIT)
            target_compile_definitions(${TARGET_EXEC} PRIVATE USE_OPENMP_CIRCUIT=1)
        endif()
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
