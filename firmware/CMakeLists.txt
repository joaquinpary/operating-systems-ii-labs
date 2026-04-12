cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dhl_courier)

target_sources(app PRIVATE
	src/gps_telemetry.c
	src/main.c
	src/provisioning.c
	src/route_manager.c
)