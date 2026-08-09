cmake_minimum_required(VERSION 3.10)
project(bt_reader C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    main.c
    ui/config.c
    bt/bt_link.c
    bt/bt_log.c
)

if(WIN32)
    list(APPEND SOURCES ui/ui_win.c bt/bt_pair_win.c clipboard/clipboard_win.c)
    add_executable(bt_reader ${SOURCES})
    target_include_directories(bt_reader PRIVATE bt ui clipboard)
    target_link_libraries(bt_reader ws2_32 Bthprops.lib user32)
else()
    list(APPEND SOURCES ui/ui_gtk.c bt/bt_pair_linux.c clipboard/clipboard_linux.c)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    find_library(BLUETOOTH_LIB bluetooth REQUIRED)
    add_executable(bt_reader ${SOURCES})
    target_include_directories(bt_reader PRIVATE ${GTK3_INCLUDE_DIRS} bt ui clipboard)
    target_compile_options(bt_reader PRIVATE ${GTK3_CFLAGS_OTHER})
    target_link_libraries(bt_reader ${GTK3_LIBRARIES} ${GTK3_LDFLAGS_OTHER} ${BLUETOOTH_LIB})
endif()
