use serde::{Deserialize, Serialize};
use std::collections::HashMap;

// --- Data received from OCP (vehicle feedback) ---

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VehicleOcpShared {
    pub vehicle_feedback: HashMap<String, VehicleFeedbackData>,
    pub last_input: Option<Controller>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VehicleFeedbackData {
    pub ping_latency_ms: u64,
    pub message_latency_roundtrip: Option<u64>,
    pub ms_since_last_vehicle_data: u64,
    pub missing_cameras: Option<Vec<String>>,
    pub sender_fault: Vec<String>,
    pub receiver_fault: Vec<String>,
    pub vehicle_data: Option<VehicleUserData>,
    pub last_remote_input: Option<Controller>,
    pub ack_time: u64,
    pub ack_time_mac: u32,
    #[serde(default)]
    pub is_monitor: bool,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct VehicleUserData {
    pub pos: Option<LatLong>,
    pub user_data: serde_json::Value,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct LatLong {
    pub lat: f64,
    pub lon: f64,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Controller {
    pub buttons: Buttons,
    pub axes: Axes,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
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

impl Default for Axes {
    fn default() -> Self {
        Self {
            left_x: 0.0,
            left_y: 0.0,
            right_x: 0.0,
            right_y: 0.0,
            left_trigger: -1.0,
            right_trigger: -1.0,
        }
    }
}

// --- Data sent to OCP (client commands) ---

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct ClientOcpShared {
    pub active_vehicle: Option<String>,
    pub client_user_data: HashMap<String, InputClientUserData>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct InputClientUserData {
    pub user_data: serde_json::Value,
    pub ocp_disable_gamepad: Option<bool>,
    pub ack_time_returned: Option<u64>,
    pub ack_time_mac_returned: Option<u32>,
}
