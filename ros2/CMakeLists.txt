cmake_minimum_required(VERSION 3.14)
project(ocp_vehicle_example VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(Threads REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

find_package(nlohmann_json REQUIRED)

add_executable(OcpVehicleExample main.cpp protocol.h)

target_link_libraries(
    OcpVehicleExample
    PRIVATE
        ${sensor_msgs_TARGETS}
        nlohmann_json::nlohmann_json
        rclcpp::rclcpp
        sensor_msgs::sensor_msgs_library
        Threads::Threads
)

install(TARGETS OcpVehicleExample DESTINATION lib/${PROJECT_NAME})

ament_package()
