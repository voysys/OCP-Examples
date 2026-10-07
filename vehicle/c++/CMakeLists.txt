cmake_minimum_required(VERSION 3.14)
project(OcpVehicleExample VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(nlohmann_json 3.11.3 QUIET)

if(NOT TARGET nlohmann_json::nlohmann_json)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_executable(OcpVehicleExample main.cpp protocol.h)

target_link_libraries(
    OcpVehicleExample
    PRIVATE
        nlohmann_json::nlohmann_json
        Threads::Threads
)

if(WIN32)
    target_link_libraries(OcpVehicleExample PRIVATE Ws2_32)
endif()
