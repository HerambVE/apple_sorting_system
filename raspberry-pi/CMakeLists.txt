cmake_minimum_required(VERSION 3.10)
project(AppleSorter VERSION 2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find necessary packages
find_package(Threads REQUIRED)

# Add executable
add_executable(applesortv2 apple_sorter2.cpp)

# Link libraries: pigpio, pthread, curl
target_link_libraries(applesortv2
    pigpio
    Threads::Threads
    curl
)
