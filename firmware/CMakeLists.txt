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

if(CONFIG_DHL_UI_QT)
    # Qt5/6 cannot be compiled under Zephyr's toolchain flags (-nostdinc,
    # -m32, no C++ stdlib).  Instead we compile ui_qt.cpp with the host
    # system compiler as a standalone object and link it into the app.
    find_program(HOST_CXX NAMES g++ c++ REQUIRED)

    # Obtain Qt5 flags via pkg-config (works regardless of Zephyr's toolchain).
    find_program(PKG_CONFIG NAMES pkg-config REQUIRED)
    execute_process(
        COMMAND ${PKG_CONFIG} --cflags Qt5Widgets
        OUTPUT_VARIABLE _qt_cflags OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
        COMMAND ${PKG_CONFIG} --libs Qt5Widgets
        OUTPUT_VARIABLE _qt_libs OUTPUT_STRIP_TRAILING_WHITESPACE)

    if(_qt_cflags)
        # Convert space-separated strings into proper CMake lists.
        separate_arguments(_qt_cflags_list UNIX_COMMAND "${_qt_cflags}")
        separate_arguments(_qt_libs_list  UNIX_COMMAND "${_qt_libs}")

        set(_qt_obj "${CMAKE_CURRENT_BINARY_DIR}/ui_qt.o")
        add_custom_command(
            OUTPUT ${_qt_obj}
            COMMAND ${HOST_CXX} -c -std=c++17 -fPIC -O2
                    -I${CMAKE_CURRENT_SOURCE_DIR}/src/ui
                    ${_qt_cflags_list}
                    -o ${_qt_obj}
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ui_qt.cpp
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ui_qt.cpp
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ui_bridge.h
            COMMENT "Compiling ui_qt.cpp with host C++ compiler"
        )
        add_custom_target(qt_ui_object DEPENDS ${_qt_obj})
        add_dependencies(app qt_ui_object)

        target_link_libraries(app PRIVATE ${_qt_obj})

        # Qt shared libs must go into the FINAL native_sim link step (the
        # "runner"), not the intermediate relocatable link (-r).
        # INTERFACE_LINK_OPTIONS → NSI_LINK_OPTIONS → FINALLINK_FLAGS
        # (appears AFTER --no-whole-archive in the NSI Makefile).
        set_property(TARGET native_simulator APPEND PROPERTY
            INTERFACE_LINK_OPTIONS
            -L/lib/x86_64-linux-gnu ${_qt_libs_list} -lstdc++)
    else()
        message(WARNING "CONFIG_DHL_UI_QT is enabled but pkg-config Qt5Widgets not found; building stub UI.")
        target_sources(app PRIVATE src/ui/ui_stub.c)
    endif()
else()
    target_sources(app PRIVATE src/ui/ui_stub.c)
endif()

target_include_directories(app PRIVATE
    external/cjson
    src/ui
)