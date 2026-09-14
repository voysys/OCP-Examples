# OCP vehicle C++ example

The C++ example sets up a TCP client that connects to the Oden OCP ECU on
`127.0.0.1:4000`, parses received control commands as JSON, and replies with
vehicle telemetry. Messages are framed as a little-endian `u32` length prefix
followed by a UTF-8 JSON body.

The client accepts optional controller and user-data fields, echoes the latest
acknowledgement values, sends the same example telemetry as the Rust client
every 10 ms, and reconnects after 100 ms when a connection ends.

## Build and run

```shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

By default the client connects to `127.0.0.1:4000`. A different address can be
given as the first argument:

```shell
./OcpVehicleExample 127.0.0.1:5000
```