# OCP operator Rust example

This client connects to the OCP TCP server in Oden Player at
`127.0.0.1:4001`. It prints the feedback for each vehicle.

The client selects the first controlled vehicle in the feedback. It does not
select monitored vehicles. It keeps the selected vehicle until the TCP
connection closes, so use it with one controlled vehicle only.

Every 10 ms, the client sends a message for the selected vehicle. The message
contains:

- an `example_time` value in `user_data`
- the `ack_time` and `ack_time_mac` from the last feedback
- `ocp_disable_gamepad` set to `true`

Put your own control data in `user_data`. This example shows the integration
only. It does not control a vehicle.

## Build and run

You need a Rust toolchain that supports edition 2024. In this folder, run:

```shell
cargo run --release --locked
```

To use a different address, give it as the first argument:

```shell
cargo run --release --locked -- 127.0.0.1:5001
```

Set the `ocp_player_tcp_port` plugin parameter in Oden Player to the same
port. OCP must be enabled in the Player, and the Player must have a control
or monitor connection to a Streamer. This client does not start Fleet
sessions. When the TCP connection closes, the client connects again after
100 ms.

Each message is a little-endian `u32` length, then a UTF-8 JSON body.
`src/protocol.rs` contains the message types. The example does not need Oden
libraries or private dependencies.

The Player accepts one TCP client at a time. OCP accepts client data from one
source only: TCP, WebView or a plugin. If a second source sends data, OCP
stops all client data until the Player restarts. Do not run this client
together with another source.

If another application uses the port, free the port or change it. Then
restart the Player.
