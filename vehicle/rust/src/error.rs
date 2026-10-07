// Copyright (C) 2026 Voysys AB
// SPDX-License-Identifier: 0BSD

use std::io;

#[allow(clippy::enum_variant_names)]
#[derive(thiserror::Error, Debug)]
pub enum OcpError {
    #[error("Io Error: `{0}`")]
    Io(#[from] io::Error),
    #[error("Serde Json Error: `{0}`")]
    SerdeJson(#[from] serde_json::Error),
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
