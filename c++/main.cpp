// Copyright (C) 2026 Voysys AB
// SPDX-License-Identifier: 0BSD

#include "protocol.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#endif

using json = nlohmann::json;

constexpr const char * SERVER_IP = "127.0.0.1";
constexpr int PORT = 4000;
constexpr size_t LENGTH_HEADER_SIZE = 4;
constexpr auto RECONNECT_DELAY = std::chrono::milliseconds(100);
constexpr auto RESPONSE_INTERVAL = std::chrono::milliseconds(10);

struct AckTimes {
    uint64_t ack_time = 0;
    uint32_t ack_time_mac = 0;
};

static void close_socket(SOCKET sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

static void stop_connection(std::atomic_bool & run, SOCKET sock) {
    run.store(false);
#ifdef _WIN32
    shutdown(sock, SD_BOTH);
#else
    shutdown(sock, SHUT_RDWR);
#endif
}

static bool retryable_error() {
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEINTR || error == WSAEWOULDBLOCK;
#else
    return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static bool recv_all(SOCKET sock, char * buffer, size_t length) {
    size_t total = 0;
    while (total < length) {
        int chunk = static_cast<int>(std::min<size_t>(length - total, 1 << 20));
        auto result = recv(sock, buffer + total, chunk, 0);
        if (result > 0) {
            total += static_cast<size_t>(result);
        } else if (result == 0 || !retryable_error()) {
            return false;
        }
    }
    return true;
}

static bool send_all(SOCKET sock, const char * buffer, size_t length) {
    size_t total = 0;
    while (total < length) {
        int chunk = static_cast<int>(std::min<size_t>(length - total, 1 << 20));
        auto result = send(sock, buffer + total, chunk, 0);
        if (result > 0) {
            total += static_cast<size_t>(result);
        } else if (result == 0 || !retryable_error()) {
            return false;
        }
    }
    return true;
}

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

static void reader_thread(std::atomic_bool & run, SOCKET sock, AckTimes & acks, std::mutex & ackMutex) {
    while (run.load()) {
        char lengthBuffer[LENGTH_HEADER_SIZE];
        if (!recv_all(sock, lengthBuffer, LENGTH_HEADER_SIZE)) {
            break;
        }

        try {
            std::string payload(read_u32_le(lengthBuffer), '\0');
            if (!recv_all(sock, payload.data(), payload.size())) {
                break;
            }

            OCP::VehicleControlMessage message = json::parse(payload).get<OCP::VehicleControlMessage>();
            printf("%s\n", payload.c_str());

            std::scoped_lock lock(ackMutex);
            acks = { message.ack_time, message.ack_time_mac };
        } catch (const std::exception & error) {
            printf("Error: Invalid OCP message: %s\n", error.what());
            break;
        }
    }

    stop_connection(run, sock);
}

static void writer_thread(std::atomic_bool & run, SOCKET sock, AckTimes & acks, std::mutex & ackMutex) {
    auto batteryTime = std::chrono::steady_clock::now();
    auto throttleTime = std::chrono::steady_clock::now();

    while (run.load()) {
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

    stop_connection(run, sock);
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Error: WSAStartup() failed\n");
        return 1;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif

    printf("OCP TCP client at %s:%d\n", SERVER_IP, PORT);

    while (true) {
        SOCKET sock = socket(PF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            printf("Error: socket() failed.\n");
            std::this_thread::sleep_for(RECONNECT_DELAY);
            continue;
        }

        sockaddr_in servAddr = {};
        servAddr.sin_family = AF_INET;
        servAddr.sin_port = htons(PORT);
        inet_pton(AF_INET, SERVER_IP, &servAddr.sin_addr);

        if (connect(sock, reinterpret_cast<sockaddr *>(&servAddr), sizeof(servAddr)) != 0) {
            close_socket(sock);
            std::this_thread::sleep_for(RECONNECT_DELAY);
            continue;
        }

        printf("Connected to the Oden OCP ECU.\n");

        AckTimes acks;
        std::mutex ackMutex;
        std::atomic_bool run = true;

        std::thread readThread(reader_thread, std::ref(run), sock, std::ref(acks), std::ref(ackMutex));
        std::thread writeThread(writer_thread, std::ref(run), sock, std::ref(acks), std::ref(ackMutex));
        readThread.join();
        writeThread.join();

        close_socket(sock);
        printf("Disconnected from the Oden OCP ECU.\n");
        std::this_thread::sleep_for(RECONNECT_DELAY);
    }
}
