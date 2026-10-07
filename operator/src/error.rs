use std::io;

use crate::protocol::VehicleOcpShared;

#[allow(clippy::enum_variant_names)]
#[derive(thiserror::Error, Debug)]
pub enum OcpError {
    #[error("Io Error: `{0}`")]
    Io(#[from] io::Error),
    #[error("Serde Json Error: `{0}`")]
    SerdeJson(#[from] serde_json::Error),
    #[error("MPSC Error: `{0}`")]
    Mpsc(#[from] tokio::sync::mpsc::error::SendError<VehicleOcpShared>),
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
