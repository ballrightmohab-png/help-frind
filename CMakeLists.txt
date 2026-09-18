cmake_minimum_required(VERSION 3.22.1)
project(LeviModule CXX C)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-D_GNU_SOURCE)

include(FetchContent)

set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    dobby
    GIT_REPOSITORY https://github.com/jmpews/Dobby.git
    GIT_TAG main
)
FetchContent_MakeAvailable(dobby)

add_library(levimodule SHARED
    src/main.cpp
)

find_library(log-lib log)

target_link_libraries(levimodule
    ${log-lib}
    dobby
)
