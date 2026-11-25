cmake_minimum_required(VERSION 3.20)

project(ATM_Transaction_Simulation
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATM_BUILD_TESTS "Build ATM automated tests" ON)

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

add_subdirectory(backend)

if(ATM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
