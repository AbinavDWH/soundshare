cmake_minimum_required(VERSION 3.16)
project(soundshare C)

set(CMAKE_C_STANDARD 11)

# Block MinGW cross-compiler
if(CMAKE_C_COMPILER MATCHES "mingw")
    message(FATAL_ERROR
        "MinGW detected! Use native gcc:\n"
        "  cmake .. -DCMAKE_C_COMPILER=/usr/bin/gcc")
endif()

add_definitions(
    -D_GNU_SOURCE
    -D_POSIX_C_SOURCE=200809L
    -D_DEFAULT_SOURCE
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pthread")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(PULSE REQUIRED libpulse libpulse-simple)

set(SOURCES
    src/main.c
    src/config.c
    src/protocol.c
    src/network.c
    src/audio.c
    src/streaming.c
    src/receiving.c
    src/ping.c
    src/chat.c
    src/ui.c
)

add_executable(soundshare ${SOURCES})

target_include_directories(soundshare PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTK3_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
)

target_link_libraries(soundshare
    ${GTK3_LIBRARIES}
    ${PULSE_LIBRARIES}
    pthread
    m
)

install(TARGETS soundshare DESTINATION bin)