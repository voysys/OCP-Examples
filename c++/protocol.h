// Copyright (C) 2026 Voysys AB
// SPDX-License-Identifier: 0BSD

#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace OCP {

struct Buttons {
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
    bool left_bumper = false;
    bool right_bumper = false;
    bool back = false;
    bool start = false;
    bool guide = false;
    bool left_thumb = false;
    bool right_thumb = false;
    bool dpad_up = false;
    bool dpad_right = false;
    bool dpad_down = false;
    bool dpad_left = false;
};

inline void from_json(const json & j, Buttons & b) {
    j.at("a").get_to(b.a);
    j.at("b").get_to(b.b);
    j.at("x").get_to(b.x);
    j.at("y").get_to(b.y);
    j.at("left_bumper").get_to(b.left_bumper);
    j.at("right_bumper").get_to(b.right_bumper);
    j.at("back").get_to(b.back);
    j.at("start").get_to(b.start);
    j.at("guide").get_to(b.guide);
    j.at("left_thumb").get_to(b.left_thumb);
    j.at("right_thumb").get_to(b.right_thumb);
    j.at("dpad_up").get_to(b.dpad_up);
    j.at("dpad_right").get_to(b.dpad_right);
    j.at("dpad_down").get_to(b.dpad_down);
    j.at("dpad_left").get_to(b.dpad_left);
}

struct Axes {
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
    float left_trigger = -1.0f;
    float right_trigger = -1.0f;
};

inline void from_json(const json & j, Axes & a) {
    j.at("left_x").get_to(a.left_x);
    j.at("left_y").get_to(a.left_y);
    j.at("right_x").get_to(a.right_x);
    j.at("right_y").get_to(a.right_y);
    j.at("left_trigger").get_to(a.left_trigger);
    j.at("right_trigger").get_to(a.right_trigger);
}

struct Controller {
    Buttons buttons;
    Axes axes;
};

inline void from_json(const json & j, Controller & c) {
    j.at("buttons").get_to(c.buttons);
    j.at("axes").get_to(c.axes);
}

struct Telemetry {
    uint32_t latency = 0;
    bool fault = false;
    std::string fault_reason;
    std::vector<std::string> missing_cameras;
};

inline void from_json(const json & j, Telemetry & t) {
    j.at("latency").get_to(t.latency);
    j.at("fault").get_to(t.fault);
    j.at("fault_reason").get_to(t.fault_reason);
    t.missing_cameras = j.value("missing_cameras", std::vector<std::string> {});
}

struct VehicleControlMessage {
    std::optional<Controller> controller;
    Telemetry telemetry;
    uint64_t ack_time = 0;
    uint32_t ack_time_mac = 0;
    std::optional<json> client_user_data;
};

inline void from_json(const json & j, VehicleControlMessage & m) {
    auto controller = j.find("controller");
    if (controller != j.end() && !controller->is_null()) {
        m.controller = controller->get<Controller>();
    }

    j.at("telemetry").get_to(m.telemetry);
    j.at("ack_time").get_to(m.ack_time);
    j.at("ack_time_mac").get_to(m.ack_time_mac);

    auto client_user_data = j.find("client_user_data");
    if (client_user_data != j.end() && !client_user_data->is_null()) {
        m.client_user_data = *client_user_data;
    }
}

struct LatLong {
    double lat = 0.0;
    double lon = 0.0;
};

inline void to_json(json & j, const LatLong & l) {
    j = json { { "lat", l.lat }, { "lon", l.lon } };
}

struct VehicleData {
    float battery_voltage = 0.0f;
    float throttle = 0.0f;
    float one = 1.0f;
    float half = 0.5f;
};

inline void to_json(json & j, const VehicleData & v) {
    j = json { { "battery_voltage", v.battery_voltage },
               { "throttle", v.throttle },
               { "one", v.one },
               { "half", v.half } };
}

struct VehicleUserData {
    std::optional<LatLong> pos;
    VehicleData user_data;
};

inline void to_json(json & j, const VehicleUserData & v) {
    j = json { { "pos", v.pos ? json(*v.pos) : json() }, { "user_data", v.user_data } };
}

struct VehicleResponseMessage {
    uint64_t ack_time = 0;
    uint32_t ack_time_mac = 0;
    std::optional<VehicleUserData> vehicle_user_data;
};

inline void to_json(json & j, const VehicleResponseMessage & v) {
    j = json { { "ack_time", v.ack_time },
               { "ack_time_mac", v.ack_time_mac },
               { "vehicle_user_data", v.vehicle_user_data ? json(*v.vehicle_user_data) : json() } };
}

} // namespace OCP
