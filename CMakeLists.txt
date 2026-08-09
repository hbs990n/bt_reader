cmake_minimum_required(VERSION 3.10)
project(bt_reader C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    src/bt_reader.c
    src/bt_link.c
)

if(WIN32)
    list(APPEND SOURCES src/bt_pair_win.c)
    add_executable(bt_reader ${SOURCES})
    target_link_libraries(bt_reader ws2_32 Bthprops.lib user32)
else()
    list(APPEND SOURCES src/bt_pair_linux.c)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    find_library(BLUETOOTH_LIB bluetooth REQUIRED)
    add_executable(bt_reader ${SOURCES})
    target_include_directories(bt_reader PRIVATE ${GTK3_INCLUDE_DIRS})
    target_compile_options(bt_reader PRIVATE ${GTK3_CFLAGS_OTHER})
    target_link_libraries(bt_reader ${GTK3_LIBRARIES} ${GTK3_LDFLAGS_OTHER} ${BLUETOOTH_LIB})
endif()
