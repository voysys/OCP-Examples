// Copyright (C) 2026 Voysys AB
// SPDX-License-Identifier: 0BSD

#include "protocol.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <exception>
#include <mutex>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joy.hpp>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using json = nlohmann::json;

constexpr const char * DEFAULT_ADDRESS = "127.0.0.1";
constexpr int DEFAULT_PORT = 4000;
constexpr size_t LENGTH_HEADER_SIZE = 4;
constexpr uint32_t MAX_MESSAGE_SIZE = 16 * 1024;
constexpr auto SOCKET_TIMEOUT = std::chrono::milliseconds(100);
constexpr auto READ_TIMEOUT = std::chrono::seconds(2);
constexpr auto RECONNECT_DELAY = std::chrono::milliseconds(100);
constexpr auto RESPONSE_INTERVAL = std::chrono::milliseconds(10);

struct AckTimes {
    uint64_t ack_time = 0;
    uint32_t ack_time_mac = 0;
};

enum class RecvStatus { Ok, Closed, TimedOut };

static uint32_t read_u32_le(const char * buffer) {
    return static_cast<uint32_t>(static_cast<uint8_t>(buffer[0])) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buffer[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buffer[2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buffer[3])) << 24);
}

static void write_u32_le(char * buffer, uint32_t value) {
    buffer[0] = static_cast<char>(value & 0xff);
    buffer[1] = static_cast<char>((value >> 8) & 0xff);
    buffer[2] = static_cast<char>((value >> 16) & 0xff);
    buffer[3] = static_cast<char>((value >> 24) & 0xff);
}

static bool set_socket_timeouts(int sock) {
    timeval timeout {};
    timeout.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(SOCKET_TIMEOUT).count();
    timeout.tv_usec =
        std::chrono::duration_cast<std::chrono::microseconds>(SOCKET_TIMEOUT % std::chrono::seconds(1))
            .count();
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
           setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}

class OcpVehicleExample: public rclcpp::Node {
public:
    OcpVehicleExample() : Node("ocp_vehicle_example") {
        address = declare_parameter<std::string>("address", DEFAULT_ADDRESS);
        port = static_cast<int>(declare_parameter<int>("port", DEFAULT_PORT));

        auto qos = rclcpp::SensorDataQoS();
        qos.keep_last(2);
        qos.reliable();
        publisher = create_publisher<sensor_msgs::msg::Joy>("ocp", qos);
        clientThread = std::thread([this] { client_loop(); });
    }

    ~OcpVehicleExample() override {
        stopped.store(true);
        clientThread.join();
    }

private:
    bool running() const { return !stopped.load() && rclcpp::ok(); }

    bool connection_running() const { return connRun.load() && running(); }

    void client_loop() {
        while (running()) {
            publish_neutral_joy();

            int sock = socket(PF_INET, SOCK_STREAM, 0);
            if (sock < 0 || !set_socket_timeouts(sock)) {
                RCLCPP_ERROR(get_logger(), "Could not create socket.");
                if (sock >= 0) {
                    close(sock);
                }
                std::this_thread::sleep_for(RECONNECT_DELAY);
                continue;
            }

            if (port <= 0 || port > 65535) {
                RCLCPP_FATAL(get_logger(), "Invalid port parameter %d (expected 1-65535).", port);
                close(sock);
                rclcpp::shutdown();
                return;
            }

            sockaddr_in servAddr = {};
            servAddr.sin_family = AF_INET;
            servAddr.sin_port = htons(static_cast<uint16_t>(port));
            if (inet_pton(AF_INET, address.c_str(), &servAddr.sin_addr) != 1) {
                RCLCPP_FATAL(get_logger(), "Invalid address parameter '%s'.", address.c_str());
                close(sock);
                rclcpp::shutdown();
                return;
            }

            if (connect(sock, reinterpret_cast<sockaddr *>(&servAddr), sizeof(servAddr)) != 0) {
                RCLCPP_WARN_THROTTLE(
                    get_logger(),
                    *get_clock(),
                    5000,
                    "Failed to connect to OCP at %s:%d: %s. Is the Oden Streamer running with the "
                    "OCP plugin enabled?",
                    address.c_str(),
                    port,
                    strerror(errno));
                close(sock);
                std::this_thread::sleep_for(RECONNECT_DELAY);
                continue;
            }

            RCLCPP_INFO_THROTTLE(
                get_logger(),
                *get_clock(),
                5000,
                "TCP connection established to %s:%d, waiting for OCP data...",
                address.c_str(),
                port);

            {
                std::scoped_lock lock(ackMutex);
                acks = {};
            }
            connRun.store(true);
            std::thread readThread([this, sock] { read_loop(sock); });
            std::thread writeThread([this, sock] { write_loop(sock); });
            readThread.join();
            writeThread.join();

            close(sock);
            RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 5000, "Disconnected from the Oden OCP ECU.");
            publish_neutral_joy();
            std::this_thread::sleep_for(RECONNECT_DELAY);
        }
    }

    void read_loop(int sock) {
        bool firstMessage = true;

        while (connection_running()) {
            const auto deadline = std::chrono::steady_clock::now() + READ_TIMEOUT;

            char lengthBuffer[LENGTH_HEADER_SIZE];
            RecvStatus status = recv_all(sock, lengthBuffer, LENGTH_HEADER_SIZE, deadline);
            if (status != RecvStatus::Ok) {
                if (status == RecvStatus::TimedOut) {
                    if (firstMessage) {
                        RCLCPP_ERROR_THROTTLE(
                            get_logger(),
                            *get_clock(),
                            5000,
                            "Connected to %s:%d but received no OCP data within 2s - another "
                            "application may be listening on this port instead of OCP.",
                            address.c_str(),
                            port);
                    } else {
                        RCLCPP_ERROR(get_logger(), "Connection to OCP timed out (no data in 2s)");
                    }
                }
                break;
            }

            const uint32_t length = read_u32_le(lengthBuffer);
            if (length > MAX_MESSAGE_SIZE) {
                RCLCPP_ERROR_THROTTLE(
                    get_logger(),
                    *get_clock(),
                    5000,
                    "Received an invalid frame (length %u bytes, max %u) from %s:%d - the service "
                    "on this port does not appear to be OCP.",
                    length,
                    MAX_MESSAGE_SIZE,
                    address.c_str(),
                    port);
                break;
            }

            std::string payload(length, '\0');
            status = recv_all(sock, payload.data(), payload.size(), deadline);
            if (status != RecvStatus::Ok) {
                if (status == RecvStatus::TimedOut) {
                    if (firstMessage) {
                        RCLCPP_ERROR_THROTTLE(
                            get_logger(),
                            *get_clock(),
                            5000,
                            "Connected to %s:%d but did not receive a complete OCP message within "
                            "2s - another application may be listening on this port instead of OCP.",
                            address.c_str(),
                            port);
                    } else {
                        RCLCPP_ERROR(get_logger(), "Connection to OCP timed out (incomplete message)");
                    }
                }
                break;
            }

            OCP::VehicleControlMessage message;
            try {
                message = json::parse(payload).get<OCP::VehicleControlMessage>();
            } catch (const std::exception & error) {
                if (firstMessage) {
                    RCLCPP_ERROR_THROTTLE(
                        get_logger(),
                        *get_clock(),
                        5000,
                        "Data from %s:%d does not match the OCP protocol - another application may "
                        "be listening on this port instead of OCP.",
                        address.c_str(),
                        port);
                } else {
                    RCLCPP_ERROR(get_logger(), "Invalid OCP message: %s", error.what());
                }
                break;
            }

            if (firstMessage) {
                firstMessage = false;
                RCLCPP_INFO(get_logger(), "Connected to the Oden OCP ECU.");
            }

            {
                std::scoped_lock lock(ackMutex);
                acks = { message.ack_time, message.ack_time_mac };
            }

            if (message.controller && !message.telemetry.fault) {
                publish_joy(*message.controller);
            } else {
                publish_neutral_joy();
            }
        }

        connRun.store(false);
    }

    void write_loop(int sock) {
        auto batteryTime = std::chrono::steady_clock::now();
        auto throttleTime = std::chrono::steady_clock::now();

        while (connection_running()) {
            auto now = std::chrono::steady_clock::now();
            if (now - batteryTime > std::chrono::seconds(10)) {
                batteryTime = now;
            }
            if (now - throttleTime > std::chrono::seconds(4)) {
                throttleTime = now;
            }

            OCP::VehicleResponseMessage message {};
            {
                std::scoped_lock lock(ackMutex);
                message.ack_time = acks.ack_time;
                message.ack_time_mac = acks.ack_time_mac;
            }
            auto & data = message.vehicle_user_data.emplace().user_data;
            data.battery_voltage =
                std::fabs(std::chrono::duration<float>(now - batteryTime).count() / 5.0f - 1.0f);
            data.throttle = std::fabs(std::chrono::duration<float>(now - throttleTime).count() / 2.0f - 1.0f);

            std::string payload = json(message).dump();
            char lengthBuffer[LENGTH_HEADER_SIZE];
            write_u32_le(lengthBuffer, static_cast<uint32_t>(payload.size()));

            if (!send_all(sock, lengthBuffer, LENGTH_HEADER_SIZE) ||
                !send_all(sock, payload.data(), payload.size())) {
                break;
            }

            std::this_thread::sleep_for(RESPONSE_INTERVAL);
        }

        connRun.store(false);
    }

    RecvStatus recv_all(
        int sock, char * buffer, size_t length, std::chrono::steady_clock::time_point deadline) {
        size_t total = 0;
        while (total < length) {
            ssize_t result = recv(sock, buffer + total, length - total, 0);
            if (result > 0) {
                total += static_cast<size_t>(result);
                continue;
            }
            if (result == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                return RecvStatus::Closed;
            }
            if (!connection_running()) {
                return RecvStatus::Closed;
            }
            if (std::chrono::steady_clock::now() > deadline) {
                return RecvStatus::TimedOut;
            }
        }
        return RecvStatus::Ok;
    }

    bool send_all(int sock, const char * buffer, size_t length) {
        size_t total = 0;
        while (total < length) {
            ssize_t result = send(sock, buffer + total, length - total, 0);
            if (result > 0) {
                total += static_cast<size_t>(result);
            } else if (
                result == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) ||
                !connection_running()) {
                return false;
            }
        }
        return true;
    }

    void publish_joy(const OCP::Controller & controller) {
        sensor_msgs::msg::Joy message;
        message.header.stamp = now();
        message.header.frame_id = "joy";
        message.axes = {
            controller.axes.left_x,  controller.axes.left_y,       controller.axes.right_x,
            controller.axes.right_y, controller.axes.left_trigger, controller.axes.right_trigger
        };
        message.buttons = {
            controller.buttons.a,          controller.buttons.b,           controller.buttons.x,
            controller.buttons.y,          controller.buttons.left_bumper, controller.buttons.right_bumper,
            controller.buttons.back,       controller.buttons.start,       controller.buttons.guide,
            controller.buttons.left_thumb, controller.buttons.right_thumb, controller.buttons.dpad_up,
            controller.buttons.dpad_right, controller.buttons.dpad_down,   controller.buttons.dpad_left
        };
        try {
            publisher->publish(message);
        } catch (const std::exception & error) {
            RCLCPP_ERROR(get_logger(), "Could not publish Joy message: %s", error.what());
        }
    }

    void publish_neutral_joy() { publish_joy(OCP::Controller {}); }

    rclcpp::Publisher<sensor_msgs::msg::Joy>::SharedPtr publisher;
    std::thread clientThread;
    std::mutex ackMutex;
    AckTimes acks;
    std::string address;
    int port = 0;
    std::atomic_bool stopped = false;
    std::atomic_bool connRun = false;
};

int main(int argc, char * argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<OcpVehicleExample>());
    rclcpp::shutdown();
    return 0;
}
