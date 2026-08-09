cmake_minimum_required(VERSION 3.10)
project(bt_reader C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(bt_reader src/bt_reader.c)

if(WIN32)
    target_link_libraries(bt_reader ws2_32 Bthprops.lib)
else()
    find_library(BLUETOOTH_LIB bluetooth REQUIRED)
    target_link_libraries(bt_reader ${BLUETOOTH_LIB})
endif()
