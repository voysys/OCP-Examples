// Copyright (C) 2026 Voysys AB
// SPDX-License-Identifier: 0BSD

use std::io;
use tokio::sync::mpsc;

use crate::protocol::VehicleControlMessage;

#[allow(clippy::enum_variant_names)]
#[derive(thiserror::Error, Debug)]
pub enum OcpError {
    #[error("Io Error: `{0}`")]
    Io(#[from] io::Error),
    #[error("Serde Json Error: `{0}`")]
    SerdeJson(#[from] serde_json::Error),
    #[error("MPSC Error: `{0}`")]
    Mpcs(#[from] mpsc::error::SendError<VehicleControlMessage>),
    #[error("Other Error: `{0}`")]
    Other(String),
}

impl From<String> for OcpError {
    fn from(value: String) -> Self {
        Self::Other(value)
    }
}

impl From<&str> for OcpError {
    fn from(value: &str) -> Self {
        Self::Other(value.to_string())
    }
}
