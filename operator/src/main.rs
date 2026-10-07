use std::{
    collections::HashMap,
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
    protocol::{ClientOcpShared, InputClientUserData, VehicleOcpShared},
};

mod error;
mod protocol;

#[derive(Default)]
struct SharedState {
    ack_time: u64,
    ack_time_mac: u32,
    /// Discovered from the first controlled vehicle in the feedback.
    vehicle_name: Option<String>,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let address = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "127.0.0.1:4001".to_owned());
    let (tx, mut rx) = tokio::sync::mpsc::channel::<VehicleOcpShared>(16);

    // Spawn a task that processes received vehicle feedback
    tokio::spawn(async move {
        while let Some(feedback) = rx.recv().await {
            for (name, data) in &feedback.vehicle_feedback {
                if data.is_monitor {
                    println!(
                        "[{name}] monitor session, faults: {:?}",
                        data.receiver_fault
                    );
                } else {
                    println!(
                        "[{name}] ping: {}ms, latency: {:?}ms, faults: {:?}",
                        data.ping_latency_ms, data.message_latency_roundtrip, data.receiver_fault
                    );
                }

                if let Some(vehicle_data) = &data.vehicle_data {
                    println!("  vehicle_data: {}", vehicle_data.user_data);
                }
            }
        }
    });

    loop {
        match TcpStream::connect(&address).await {
            Ok(stream) => {
                println!("Connected to Oden Player");
                if let Err(e) = handle_stream(stream, &tx).await {
                    println!("Error: {e}");
                };
                println!("Disconnected from Oden Player");
            }
            Err(e) => println!("Error: {e}"),
        }

        tokio::time::sleep(Duration::from_millis(100)).await;
    }
}

async fn handle_stream(stream: TcpStream, tx: &Sender<VehicleOcpShared>) -> Result<(), OcpError> {
    let (reader, writer) = stream.into_split();
    let (done_tx, mut done_rx) = oneshot::channel();
    let state = Arc::new(Mutex::new(SharedState::default()));

    let tx = tx.clone();
    let read_task = tokio::spawn({
        let state = state.clone();
        async move {
            let res = read_message(reader, tx, state).await;
            done_tx.send(res)
        }
    });

    let res = tokio::select! {
        biased;
        res = &mut done_rx => {
            res.unwrap_or(Ok(()))
        }
        res = write_message(writer, state) => {res}
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
    tx: Sender<VehicleOcpShared>,
    state: Arc<Mutex<SharedState>>,
) -> Result<(), OcpError> {
    loop {
        let length = reader.read_u32_le().await?;

        let mut buf: Vec<u8> = vec![0; length as usize];
        reader.read_exact(buf.as_mut_slice()).await?;

        let feedback: VehicleOcpShared = serde_json::from_slice(buf.as_slice())?;

        // Extract ack_time and vehicle name from the first controlled vehicle
        if let Some((name, data)) = feedback
            .vehicle_feedback
            .iter()
            .find(|(_, data)| !data.is_monitor)
        {
            let mut lock = state.lock().unwrap();
            lock.ack_time = data.ack_time;
            lock.ack_time_mac = data.ack_time_mac;
            if lock.vehicle_name.is_none() {
                println!("Discovered vehicle: {name}");
                lock.vehicle_name = Some(name.clone());
            }
        }

        tx.try_send(feedback).ok();
    }
}

async fn write_message(
    mut writer: OwnedWriteHalf,
    state: Arc<Mutex<SharedState>>,
) -> Result<(), OcpError> {
    let start = Instant::now();

    loop {
        let response = {
            let lock = state.lock().unwrap();

            // Use the vehicle name discovered from feedback, or None if not yet known
            let active_vehicle = lock.vehicle_name.clone();

            let mut client_user_data = HashMap::new();
            if let Some(name) = &active_vehicle {
                client_user_data.insert(
                    name.clone(),
                    InputClientUserData {
                        user_data: serde_json::json!({
                            "example_time": start.elapsed().as_secs_f32(),
                        }),
                        ocp_disable_gamepad: Some(true),
                        ack_time_returned: Some(lock.ack_time),
                        ack_time_mac_returned: Some(lock.ack_time_mac),
                    },
                );
            }

            ClientOcpShared {
                active_vehicle,
                client_user_data,
            }
        };

        let message_json = serde_json::to_vec(&response)?;
        let message_length = message_json.len() as u32;
        writer.write_all(&message_length.to_le_bytes()).await?;
        writer.write_all(&message_json).await?;
        writer.flush().await?;

        tokio::time::sleep(Duration::from_millis(10)).await;
    }
}
