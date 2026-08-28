# OCP vehicle examples

Vehicle-side example clients for the Oden Control Pipeline (OCP), in three
flavors: [`rust/`](rust/), [`c++/`](c++/), and [`ros2/`](ros2/).

Each connects to the Oden OCP ECU on `127.0.0.1:4000`, receives control
commands, and replies with vehicle telemetry. See each folder's README for
build and run instructions, and [docs.voysys.dev](https://docs.voysys.dev)
for the protocol reference.

Licensed under [0BSD](LICENSE). Questions: support@voysys.se
