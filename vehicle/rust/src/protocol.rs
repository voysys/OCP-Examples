// Copyright (C) 2026 Voysys AB
// SPDX-License-Identifier: 0BSD

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Controller {
    pub buttons: Buttons,
    pub axes: Axes,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Buttons {
    pub a: bool,
    pub b: bool,
    pub x: bool,
    pub y: bool,
    pub left_bumper: bool,
    pub right_bumper: bool,
    pub back: bool,
    pub start: bool,
    pub guide: bool,
    pub left_thumb: bool,
    pub right_thumb: bool,
    pub dpad_up: bool,
    pub dpad_right: bool,
    pub dpad_down: bool,
    pub dpad_left: bool,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Axes {
    pub left_x: f32,
    pub left_y: f32,
    pub right_x: f32,
    pub right_y: f32,
    pub left_trigger: f32,
    pub right_trigger: f32,
}
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Telemetry {
    pub latency: u32,
    pub fault: bool,
    pub fault_reason: String,
    #[serde(default)]
    pub missing_cameras: Vec<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct VehicleControlMessage {
    pub controller: Option<Controller>,
    pub telemetry: Telemetry,
    pub ack_time: u64,
    pub ack_time_mac: u32,
    pub client_user_data: Option<serde_json::Value>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct LatLong {
    pub lat: f64,
    pub lon: f64,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VehicleResponseMessage {
    pub ack_time: u64,
    pub ack_time_mac: u32,
    pub vehicle_user_data: Option<VehicleUserData>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VehicleUserData {
    pub pos: Option<LatLong>,
    pub user_data: VehicleData,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VehicleData {
    pub battery_voltage: f32,
    pub throttle: f32,
    pub one: f32,
    pub half: f32,
}
