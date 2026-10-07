# OCP vehicle Rust example

Reference vehicle-side OCP client. Connects to the Oden OCP ECU on
`127.0.0.1:4000`, receives control commands, and replies with vehicle telemetry.
Messages are framed as a little-endian `u32` length prefix followed by a UTF-8
JSON body.

The client accepts optional controller and user-data fields, echoes the latest
acknowledgement values, sends example telemetry every 10 ms, and reconnects
automatically.

## Build and run

```shell
cargo run --release
```

By default the client connects to `127.0.0.1:4000`. A different address can be
given as the first argument:

```shell
cargo run --release -- 127.0.0.1:5000
```

Requires a Rust toolchain with edition 2024 support.
