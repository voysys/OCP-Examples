# OCP vehicle examples

Vehicle-side example clients for the Oden Control Pipeline (OCP), in three
flavors: [`rust/`](rust/), [`c++/`](c++/), and [`ros2/`](ros2/).

Each connects to the Oden OCP ECU on `127.0.0.1:4000`, receives control
commands, and replies with vehicle telemetry. See each folder's README for
build and run instructions, and [docs.voysys.dev](https://docs.voysys.dev)
for the protocol reference.

## Troubleshooting

If a client connects but reports no OCP data or invalid frames, another
application is most likely occupying the OCP port, so the OCP plugin could
not bind it. Free the port, or set the OCP plugin parameter `tcp_port` to a
free port and give the same address to the client. For all OCP plugin
parameters, see [the OCP documentation](https://docs.voysys.dev).

Licensed under [0BSD](../LICENSE). Questions: support@voysys.se
