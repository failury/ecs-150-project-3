cmake_minimum_required(VERSION 3.16)
project(proj2)

set(CMAKE_CXX_STANDARD 14)
set(GPP_ENABLE_WARNINGS "-O0 -Wall -Werror -Wno-unused-result")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GPP_ENABLE_WARNINGS}")
include_directories(.)
include_directories(apps)
include_directories(src)

add_executable(proj3
        apps/file.c
        apps/hello.c
        apps/preempt.c
        apps/sleep.c
        apps/thread.c
        src/Machine.cpp
        src/Machine.h
        src/main.c
        src/VirtualMachine.h
        src/VirtualMachineUtils.c
        Makefile src/VirtualMachine.cpp)
