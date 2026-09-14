// Copyright (C) 2026 Voysys AB
// SPDX-License-Identifier: 0BSD

#include "protocol.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
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

constexpr const char * DEFAULT_ADDRESS = "127.0.0.1:4000";
constexpr size_t LENGTH_HEADER_SIZE = 4;
constexpr uint32_t MAX_MESSAGE_SIZE = 16 * 1024;
constexpr auto RECONNECT_DELAY = std::chrono::milliseconds(100);
constexpr auto RESPONSE_INTERVAL = std::chrono::milliseconds(10);
constexpr auto READ_TIMEOUT = std::chrono::seconds(2);

struct AckTimes {
    uint64_t ack_time = 0;
    uint32_t ack_time_mac = 0;
};

enum class RecvStatus { Ok, Closed, TimedOut };

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

static int last_socket_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static std::string last_socket_error_string() {
    std::string message = std::system_category().message(last_socket_error());
    while (!message.empty() && (message.back() == '.' || message.back() == ' ')) {
        message.pop_back();
    }
    return message;
}

static bool timed_out_error() {
#ifdef _WIN32
    return WSAGetLastError() == WSAETIMEDOUT;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static bool retryable_error() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

static bool set_read_timeout(SOCKET sock) {
#ifdef _WIN32
    DWORD timeout =
        static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(READ_TIMEOUT).count());
    return setsockopt(
               sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout)) == 0;
#else
    timeval timeout {};
    timeout.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(READ_TIMEOUT).count();
    timeout.tv_usec = 0;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
}

static RecvStatus recv_all(SOCKET sock, char * buffer, size_t length) {
    size_t total = 0;
    while (total < length) {
        int chunk = static_cast<int>(std::min<size_t>(length - total, 1 << 20));
        auto result = recv(sock, buffer + total, chunk, 0);
        if (result > 0) {
            total += static_cast<size_t>(result);
        } else if (result == 0) {
            return RecvStatus::Closed;
        } else if (timed_out_error()) {
            return RecvStatus::TimedOut;
        } else if (!retryable_error()) {
            return RecvStatus::Closed;
        }
    }
    return RecvStatus::Ok;
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

static void print_if_changed(std::string & last, const std::string & status) {
    if (last != status) {
        printf("%s\n", status.c_str());
        last = status;
    }
}

static void reader_thread(
    std::atomic_bool & run,
    SOCKET sock,
    const std::string & addr,
    AckTimes & acks,
    std::mutex & ackMutex,
    std::string & endReason) {
    bool firstMessage = true;

    while (run.load()) {
        char lengthBuffer[LENGTH_HEADER_SIZE];
        RecvStatus status = recv_all(sock, lengthBuffer, LENGTH_HEADER_SIZE);
        if (status != RecvStatus::Ok) {
            if (status == RecvStatus::TimedOut) {
                endReason = firstMessage
                                ? "Connected to " + addr +
                                      " but received no OCP data within 2s - another application may be "
                                      "listening on this port instead of OCP."
                                : "Connection to OCP timed out (no data in 2s)";
            }
            break;
        }

        uint32_t length = read_u32_le(lengthBuffer);
        if (length > MAX_MESSAGE_SIZE) {
            endReason = "Received an invalid frame (length " + std::to_string(length) + " bytes, max " +
                        std::to_string(MAX_MESSAGE_SIZE) + ") from " + addr +
                        " - the service on this port does not appear to be OCP.";
            break;
        }

        std::string payload(length, '\0');
        status = recv_all(sock, payload.data(), payload.size());
        if (status != RecvStatus::Ok) {
            if (status == RecvStatus::TimedOut) {
                endReason = firstMessage
                                ? "Connected to " + addr +
                                      " but did not receive a complete OCP message within 2s - another "
                                      "application may be listening on this port instead of OCP."
                                : "Connection to OCP timed out (incomplete message)";
            }
            break;
        }

        try {
            OCP::VehicleControlMessage message = json::parse(payload).get<OCP::VehicleControlMessage>();

            if (firstMessage) {
                firstMessage = false;
                printf("Connected to the Oden OCP ECU.\n");
            }
            printf("%s\n", payload.c_str());

            std::scoped_lock lock(ackMutex);
            acks = { message.ack_time, message.ack_time_mac };
        } catch (const std::exception & error) {
            endReason = firstMessage
                            ? "Data from " + addr +
                                  " does not match the OCP protocol - another application may be listening "
                                  "on this port instead of OCP."
                            : std::string("Invalid OCP message: ") + error.what();
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

int main(int argc, char * argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const std::string addr = argc > 1 ? argv[1] : DEFAULT_ADDRESS;
    const size_t colon = addr.rfind(':');
    const std::string ip = colon == std::string::npos ? "" : addr.substr(0, colon);
    const char * portBegin = colon == std::string::npos ? addr.data() + addr.size() : addr.data() + colon + 1;
    const char * portEnd = addr.data() + addr.size();
    int port = 0;
    const auto [parseEnd, parseError] = std::from_chars(portBegin, portEnd, port);
    if (ip.empty() || parseError != std::errc() || parseEnd != portEnd || port <= 0 || port > 65535) {
        printf("Usage: %s [ip:port]  (default %s)\n", argv[0], DEFAULT_ADDRESS);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Error: WSAStartup() failed\n");
        return 1;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif

    printf("OCP TCP client at %s\n", addr.c_str());

    std::string lastConnectStatus;
    std::string lastEndStatus;

    while (true) {
        SOCKET sock = socket(PF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            printf("Error: socket() failed.\n");
            std::this_thread::sleep_for(RECONNECT_DELAY);
            continue;
        }

        sockaddr_in servAddr = {};
        servAddr.sin_family = AF_INET;
        servAddr.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, ip.c_str(), &servAddr.sin_addr) != 1) {
            printf("Error: invalid address '%s'.\n", ip.c_str());
            close_socket(sock);
            return 1;
        }

        if (connect(sock, reinterpret_cast<sockaddr *>(&servAddr), sizeof(servAddr)) != 0) {
            const std::string error = last_socket_error_string();
            print_if_changed(
                lastConnectStatus,
                "Failed to connect to OCP at " + addr + ": " + error +
                    ". Is the Oden Streamer running with the OCP plugin enabled?");
            close_socket(sock);
            std::this_thread::sleep_for(RECONNECT_DELAY);
            continue;
        }

        if (!set_read_timeout(sock)) {
            const std::string error = last_socket_error_string();
            print_if_changed(
                lastConnectStatus,
                "Failed to set receive timeout on OCP socket: " + error + ". Reconnecting...");
            close_socket(sock);
            std::this_thread::sleep_for(RECONNECT_DELAY);
            continue;
        }
        print_if_changed(
            lastConnectStatus, "TCP connection established to " + addr + ", waiting for OCP data...");

        AckTimes acks;
        std::mutex ackMutex;
        std::atomic_bool run = true;
        std::string endReason;

        std::thread readThread(
            reader_thread,
            std::ref(run),
            sock,
            std::cref(addr),
            std::ref(acks),
            std::ref(ackMutex),
            std::ref(endReason));
        std::thread writeThread(writer_thread, std::ref(run), sock, std::ref(acks), std::ref(ackMutex));
        readThread.join();
        writeThread.join();

        close_socket(sock);
        print_if_changed(
            lastEndStatus, endReason.empty() ? "Disconnected from the Oden OCP ECU." : endReason);
        std::this_thread::sleep_for(RECONNECT_DELAY);
    }
}
