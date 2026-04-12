cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dhl_courier)

include(FetchContent)
FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        v1.7.18
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/external/cjson
    UPDATE_COMMAND ""
)
FetchContent_GetProperties(cjson)
if(NOT cjson_POPULATED)
    FetchContent_Populate(cjson)
endif()

target_sources(app PRIVATE
	src/gps_telemetry.c
	src/main.c
	src/provisioning.c
	src/route_manager.c
	external/cjson/cJSON.c
)

target_include_directories(app PRIVATE external/cjson)