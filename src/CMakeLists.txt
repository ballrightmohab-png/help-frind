cmake_minimum_required(VERSION 3.22.1)
project(LeviModule CXX C)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

# Fetch Dobby hook engine automatically
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

target_include_directories(levimodule PRIVATE
    ${dobby_SOURCE_DIR}/include
)

target_link_libraries(levimodule
    ${log-lib}
    dobby
)
