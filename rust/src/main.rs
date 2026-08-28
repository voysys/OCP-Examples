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

#[derive(Default)]
struct TimeStamp {
    ack_time: u64,
    ack_time_mac: u32,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let (tx, _rx) = tokio::sync::mpsc::channel(16);

    loop {
        match TcpStream::connect("127.0.0.1:4000").await {
            Ok(stream) => {
                if let Err(e) = handle_stream(stream, &tx).await {
                    println!("Error: {e}");
                };
            }
            Err(e) => println!("Error: {e}"),
        }

        tokio::time::sleep(Duration::from_millis(100)).await;
    }
}

async fn handle_stream(
    stream: TcpStream,
    tx: &Sender<VehicleControlMessage>,
) -> Result<(), OcpError> {
    println!("Connected to Oden Streamer");
    let (reader, writer) = stream.into_split();
    let (done_tx, mut done_rx) = oneshot::channel();
    let timestamp = Arc::new(Mutex::new(TimeStamp::default()));

    let tx = tx.clone();
    let read_task = tokio::spawn({
        let timestamp = timestamp.clone();
        async move {
            let res = read_message(reader, tx, timestamp).await;
            done_tx.send(res)
        }
    });

    let res = tokio::select! {
        biased;
        res = &mut done_rx => {
            res.unwrap_or(Ok(()))
        }
        res = write_message(writer, timestamp) => {res}
    };

    read_task.abort();

    if let Err(e) = res {
        return match e {
            OcpError::Io(_) => Ok(()),
            _ => Err(e),
        };
    }

    Ok(())
}

async fn read_message(
    mut reader: OwnedReadHalf,
    tx: Sender<VehicleControlMessage>,
    timestamps: Arc<Mutex<TimeStamp>>,
) -> Result<(), OcpError> {
    loop {
        let length = reader.read_u32_le().await?;

        let mut buf: Vec<u8> = vec![0; length as usize];
        reader.read_exact(buf.as_mut_slice()).await?;

        let res: VehicleControlMessage = serde_json::from_slice(buf.as_slice())?;

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
