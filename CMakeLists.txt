cmake_minimum_required(VERSION 3.12)
project(mqtt_simple C)

set(CMAKE_C_STANDARD 11)

set(SOURCES mqtt_simple.c mqtt_simple.h)
add_executable(mqtt_simple mqtt_simple.c)
find_package(mosquitto)
if (mosquitto_FOUND)
    include_directories(${mosquitto_INCLUDE_DIR})
endif()

target_link_libraries(mqtt_simple mosquitto)