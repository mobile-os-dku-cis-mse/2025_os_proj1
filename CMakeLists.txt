cmake_minimum_required(VERSION 3.10)

project(scheduler)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

add_compile_options(-Wall -Wextra -O2)

set(SCHEDULER_SOURCES
        RR_scheduler_sim.c
)

add_executable(scheduler ${SCHEDULER_SOURCES})
