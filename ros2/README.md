# OCP vehicle ROS 2 example

This node connects to the Oden OCP ECU at `127.0.0.1:4000`, publishes the
received controls on the ROS 2 topic `/ocp` as `sensor_msgs/msg/Joy`, and sends
example vehicle telemetry back to Oden.


## Prerequisites
ROS 2 installed

```shell
sudo apt update
sudo apt install nlohmann-json3-dev
```

## Build and run

```shell
cd plugins/ocp_vehicle_example/ros2
source /opt/ros/<distro>/setup.bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/OcpVehicleExample
```

## Topic commands

```shell
ros2 topic list                 # List all topics
ros2 topic info /ocp --verbose  # Show the message type and publisher
ros2 topic hz /ocp              # Show the publication rate
```

Source `/opt/ros/<distro>/setup.bash` first in every new terminal.

## Joy message mapping

The `/ocp` message uses these `axes` indices:

| Index | Control |
| ---: | --- |
| 0 | `left_x` |
| 1 | `left_y` |
| 2 | `right_x` |
| 3 | `right_y` |
| 4 | `left_trigger` (`-1.0` at rest, `1.0` fully pressed) |
| 5 | `right_trigger` (`-1.0` at rest, `1.0` fully pressed) |

The `buttons` indices are, in order:

```text
a, b, x, y, left_bumper, right_bumper, back, start, guide,
left_thumb, right_thumb, dpad_up, dpad_right, dpad_down, dpad_left
```

When the controller is missing, the connection ends, or invalid data is
received, the node publishes a neutral Joy message. Consumers should still
enforce their own command-freshness watchdog.

## Protocol details

OCP messages are UTF-8 JSON preceded by a little-endian `u32` payload length.
The node echoes the latest acknowledgement values, sends example telemetry
every 10 ms, and waits 100 ms before attempting to reconnect.
