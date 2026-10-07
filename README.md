# OCP examples

Example clients for the Oden Control Pipeline (OCP):

| Side | Examples | Connects to |
| --- | --- | --- |
| Vehicle | [Rust](vehicle/rust/), [C++](vehicle/c++/), [ROS 2](vehicle/ros2/) | Oden Streamer, `127.0.0.1:4000` |
| Operator | [Rust](operator/) | Oden Player, `127.0.0.1:4001` |

A vehicle client receives control commands and sends telemetry. The operator
client receives vehicle feedback and sends custom client data. Each example
has its own build and run instructions. You do not need the Oden source code
to build them.

Each message is a little-endian `u32` length, then a UTF-8 JSON body. For
integration details, see [the OCP documentation](https://docs.voysys.dev).

Licensed under [0BSD](LICENSE). Questions: support@voysys.se
