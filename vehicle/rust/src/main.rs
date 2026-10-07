// Copyright (C) 2026 Voysys AB
// SPDX-License-Identifier: 0BSD

use std::{
    error::Error,
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{
        TcpStream,
        tcp::{OwnedReadHalf, OwnedWriteHalf},
    },
    sync::{mpsc::Sender, oneshot},
};

use crate::{
    error::OcpError,
    protocol::{VehicleControlMessage, VehicleData, VehicleResponseMessage, VehicleUserData},
};

mod error;
mod protocol;

const DEFAULT_ADDRESS: &str = "127.0.0.1:4000";
const READ_TIMEOUT: Duration = Duration::from_secs(2);
const MAX_MESSAGE_SIZE: u32 = 16 * 1024;

#[derive(Default)]
struct TimeStamp {
    ack_time: u64,
    ack_time_mac: u32,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let addr = std::env::args()
        .nth(1)
        .unwrap_or_else(|| DEFAULT_ADDRESS.to_string());
    if !valid_address(&addr) {
        let program = std::env::args().next().unwrap_or_default();
        eprintln!("Usage: {program} [host:port]  (default {DEFAULT_ADDRESS})");
        std::process::exit(1);
    }

    let (tx, _rx) = tokio::sync::mpsc::channel(16);

    let mut last_connect_status = String::new();
    let mut last_end_status = String::new();

    loop {
        match TcpStream::connect(&addr).await {
            Ok(stream) => {
                print_if_changed(
                    &mut last_connect_status,
                    format!("TCP connection established to {addr}, waiting for OCP data..."),
                );
                match handle_stream(stream, &tx, &addr).await {
                    Ok(()) => {
                        print_if_changed(&mut last_end_status, format!("Disconnected from {addr}"))
                    }
                    Err(OcpError::Other(reason)) => print_if_changed(&mut last_end_status, reason),
                    Err(e) => print_if_changed(
                        &mut last_end_status,
                        format!("Disconnected from {addr}: {e}"),
                    ),
                }
            }
            Err(e) => print_if_changed(
                &mut last_connect_status,
                format!(
                    "Failed to connect to OCP at {addr}: {e}. Is the Oden Streamer running with the OCP plugin enabled?"
                ),
            ),
        }

        tokio::time::sleep(Duration::from_millis(100)).await;
    }
}

fn valid_address(addr: &str) -> bool {
    if let Ok(socket_addr) = addr.parse::<std::net::SocketAddr>() {
        return socket_addr.port() != 0;
    }

    addr.rsplit_once(':').is_some_and(|(host, port)| {
        !host.is_empty()
            && !host.contains(':')
            && port
                .parse::<u16>()
                .is_ok_and(|parsed_port| parsed_port != 0)
    })
}

fn print_if_changed(last: &mut String, status: String) {
    if *last != status {
        println!("{status}");
        *last = status;
    }
}

async fn handle_stream(
    stream: TcpStream,
    tx: &Sender<VehicleControlMessage>,
    addr: &str,
) -> Result<(), OcpError> {
    let (reader, writer) = stream.into_split();
    let (done_tx, mut done_rx) = oneshot::channel();
    let timestamp = Arc::new(Mutex::new(TimeStamp::default()));

    let tx = tx.clone();
    let read_task = tokio::spawn({
        let timestamp = timestamp.clone();
        let addr = addr.to_string();
        async move {
            let res = read_message(reader, tx, timestamp, &addr).await;
            done_tx.send(res)
        }
    });

    let res = tokio::select! {
        biased;
        res = &mut done_rx => {
            res.unwrap_or_else(|_| {
                Err(OcpError::Other("read task terminated unexpectedly".to_string()))
            })
        }
        res = write_message(writer, timestamp) => {res}
    };

    read_task.abort();

    res
}

async fn read_message(
    mut reader: OwnedReadHalf,
    tx: Sender<VehicleControlMessage>,
    timestamps: Arc<Mutex<TimeStamp>>,
    addr: &str,
) -> Result<(), OcpError> {
    let mut first_message = true;

    loop {
        let length = tokio::time::timeout(READ_TIMEOUT, reader.read_u32_le())
            .await
            .map_err(|_| {
                OcpError::Other(if first_message {
                    format!(
                        "Connected to {addr} but received no OCP data within 2s - another application may be listening on this port instead of OCP."
                    )
                } else {
                    "Connection to OCP timed out (no data in 2s)".to_string()
                })
            })??;

        if length > MAX_MESSAGE_SIZE {
            return Err(OcpError::Other(format!(
                "Received an invalid frame (length {length} bytes, max {MAX_MESSAGE_SIZE}) from {addr} - the service on this port does not appear to be OCP."
            )));
        }

        let mut buf: Vec<u8> = vec![0; length as usize];
        tokio::time::timeout(READ_TIMEOUT, reader.read_exact(buf.as_mut_slice()))
            .await
            .map_err(|_| {
                OcpError::Other(if first_message {
                    format!(
                        "Connected to {addr} but did not receive a complete OCP message within 2s - another application may be listening on this port instead of OCP."
                    )
                } else {
                    "Connection to OCP timed out (incomplete message)".to_string()
                })
            })??;

        let res: VehicleControlMessage = match serde_json::from_slice(buf.as_slice()) {
            Ok(res) => res,
            Err(_) if first_message => {
                return Err(OcpError::Other(format!(
                    "Data from {addr} does not match the OCP protocol - another application may be listening on this port instead of OCP."
                )));
            }
            Err(e) => return Err(e.into()),
        };

        if first_message {
            first_message = false;
            println!("Connected to Oden Streamer at {addr}");
        }

        println!("{:#?}", res);

        {
            let mut lock = timestamps.lock().unwrap();
            lock.ack_time = res.ack_time;
            lock.ack_time_mac = res.ack_time_mac;
        }

        tx.try_send(res).ok();
    }
}

async fn write_message(
    mut writer: OwnedWriteHalf,
    timestamps: Arc<Mutex<TimeStamp>>,
) -> Result<(), OcpError> {
    let mut battery_time = Instant::now();
    let mut throttle_time = Instant::now();

    loop {
        if battery_time.elapsed() > Duration::from_millis(10000) {
            battery_time = Instant::now();
        }

        if throttle_time.elapsed() > Duration::from_millis(4000) {
            throttle_time = Instant::now();
        }

        let battery = (battery_time.elapsed().as_secs_f32() / 5.0 - 1.0).abs();
        let throttle = (throttle_time.elapsed().as_secs_f32() / 2.0 - 1.0).abs();

        let vehicle_response = {
            let lock = timestamps.lock().unwrap();
            VehicleResponseMessage {
                ack_time: lock.ack_time,
                ack_time_mac: lock.ack_time_mac,
                vehicle_user_data: Some(VehicleUserData {
                    pos: None,
                    user_data: VehicleData {
                        battery_voltage: battery,
                        throttle,
                        one: 1.0,
                        half: 0.5,
                    },
                }),
            }
        };

        let message_json = serde_json::to_vec(&vehicle_response)?;
        let message_length = message_json.len() as u32;
        writer.write_all(&message_length.to_le_bytes()).await?;
        writer.write_all(&message_json).await?;

        tokio::time::sleep(Duration::from_millis(10)).await;
    }
}
