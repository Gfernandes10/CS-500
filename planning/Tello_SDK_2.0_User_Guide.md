# Tello SDK 2.0 User Guide

**Version:** V1.0  
**Date:** 2018.11  
**Copyright:** © 2018 Ryze Tech. All Rights Reserved.

## Introduction

The Tello SDK connects to the aircraft through a Wi-Fi UDP port, allowing users to control the aircraft with text commands.

After downloading and installing Python, download the `Tello3.py` sample file:

<https://dl-cdn.ryzerobotics.com/downloads/tello/20180222/Tello3.py>

> `Tello3.py` is a Python sample program that establishes a UDP communication port. It demonstrates simple interaction with Tello, including sending SDK instructions and receiving information. It is provided for reference only, and users may develop their own software.

## Architecture

Use Wi-Fi to establish a connection between the Tello and a PC, Mac, or mobile device.

### Send Commands and Receive Responses

- **Tello IP:** `192.168.10.1`
- **UDP port:** `8889`

1. Set up a UDP client on the PC, Mac, or mobile device to send and receive messages from Tello through the same port.
2. Before sending any other commands, send `command` to Tello through UDP port `8889` to initiate SDK mode.

### Receive Tello State

- **Tello IP:** `192.168.10.1`
- **Local UDP server:** `0.0.0.0`
- **UDP port:** `8890`

3. Set up a UDP server on the PC, Mac, or mobile device and receive messages through UDP port `8890`.

Steps 1 and 2 must be completed before attempting step 3.

### Receive the Tello Video Stream

- **Tello IP:** `192.168.10.1`
- **Local UDP server:** `0.0.0.0`
- **UDP port:** `11111`

4. Set up a UDP server on the PC, Mac, or mobile device and receive the video stream through UDP port `11111`.
5. Send `streamon` to Tello through UDP port `8889` to start streaming.

Steps 1 and 2 must be completed before attempting step 5.

## Command Types and Results

The Tello SDK includes three basic command types.

### Control Commands

Format:

```text
xxx
```

- Returns `ok` if the command succeeds.
- Returns `error` or an informational result code if the command fails.

### Set Commands

Format:

```text
xxx a
```

Used to set new sub-parameter values.

- Returns `ok` if the command succeeds.
- Returns `error` or an informational result code if the command fails.

### Read Commands

Format:

```text
xxx?
```

Returns the current value of the requested parameter.

# Tello Commands

## Control Commands

Unless otherwise stated, successful commands return `ok`; failed commands return `error` or an informational error code.

| Command | Description | Parameters |
|---|---|---|
| `command` | Enter SDK mode. | — |
| `takeoff` | Automatic takeoff. | — |
| `land` | Automatic landing. | — |
| `streamon` | Enable the video stream. | — |
| `streamoff` | Disable the video stream. | — |
| `emergency` | Stop the motors immediately. | — |
| `up x` | Ascend by `x` centimeters. | `x = 20-500` |
| `down x` | Descend by `x` centimeters. | `x = 20-500` |
| `left x` | Fly left by `x` centimeters. | `x = 20-500` |
| `right x` | Fly right by `x` centimeters. | `x = 20-500` |
| `forward x` | Fly forward by `x` centimeters. | `x = 20-500` |
| `back x` | Fly backward by `x` centimeters. | `x = 20-500` |
| `cw x` | Rotate clockwise by `x` degrees. | `x = 1-360` |
| `ccw x` | Rotate counterclockwise by `x` degrees. | `x = 1-360` |
| `flip x` | Perform a flip in direction `x`. | `l` = left, `r` = right, `f` = forward, `b` = back |
| `go x y z speed` | Fly to coordinates `x`, `y`, and `z` at the specified speed. | `x,y,z = -500-500` cm; `speed = 10-100` cm/s |
| `stop` | Hover in the air. Works at any time. | — |
| `curve x1 y1 z1 x2 y2 z2 speed` | Fly along a curve defined by two coordinate points. | Coordinates: `-500-500` cm; `speed = 10-60` cm/s; arc radius must be `0.5-10` m |
| `go x y z speed mid` | Fly to coordinates relative to a Mission Pad. | `mid = m1-m8`; `x,y,z = -500-500` cm; `speed = 10-100` cm/s |
| `curve x1 y1 z1 x2 y2 z2 speed mid` | Fly along a curve defined relative to a Mission Pad. | `mid = m1-m8`; coordinates: `-500-500` cm; `speed = 10-60` cm/s; arc radius must be `0.5-10` m |
| `jump x y z speed yaw mid1 mid2` | Fly to coordinates relative to Mission Pad 1, detect Mission Pad 2 at coordinates `0, 0, z`, and rotate to the specified yaw. | `mid1,mid2 = m1-m8`; `x,y,z = -500-500` cm; `speed = 10-100` cm/s |

> For the `go`, `curve`, and `jump` commands, `x`, `y`, and `z` cannot all be within the range `-20` to `20` simultaneously.

## Set Commands

| Command | Description | Parameters / Notes |
|---|---|---|
| `speed x` | Set the flight speed. | `x = 10-100` cm/s |
| `rc a b c d` | Set remote-controller input through four channels. | `a`: left/right, `b`: forward/backward, `c`: up/down, `d`: yaw; each value is `-100` to `100` |
| `wifi ssid pass` | Set the Tello Wi-Fi SSID and password. | `ssid`: new Wi-Fi name; `pass`: new password |
| `mon` | Enable Mission Pad detection using both forward and downward cameras. | — |
| `moff` | Disable Mission Pad detection. | — |
| `mdirection x` | Select the Mission Pad detection direction. | `0`: downward only; `1`: forward only; `2`: both |
| `ap ssid pass` | Put Tello into station mode and connect it to an access point. | `ssid`: access-point name; `pass`: access-point password |

### Mission Pad Detection Frequency

Run `mon` before using `mdirection`.

- Forward-only or downward-only detection: **20 Hz**
- Forward and downward detection together: **10 Hz**

## Read Commands

| Command | Description | Response |
|---|---|---|
| `speed?` | Obtain the current speed. | `10-100` cm/s |
| `battery?` | Obtain the current battery percentage. | `0-100` |
| `time?` | Obtain the current flight time. | Flight time |
| `wifi?` | Obtain the Wi-Fi signal-to-noise ratio. | SNR |
| `sdk?` | Obtain the Tello SDK version. | SDK version |
| `sn?` | Obtain the Tello serial number. | Serial number |

# Tello State

State information is sent as a semicolon-separated string through UDP port `8890`.

## State String Without Mission Pad Detection

```text
pitch:%d;roll:%d;yaw:%d;vgx:%d;vgy:%d;vgz:%d;
templ:%d;temph:%d;tof:%d;h:%d;bat:%d;baro:%.2f;
time:%d;agx:%.2f;agy:%.2f;agz:%.2f;
```

## State String With Mission Pad Detection

When Mission Pad detection is active, the state string also includes:

```text
mid:%d;x:%d;y:%d;z:%d;
```

followed by the normal attitude, velocity, temperature, distance, battery, barometer, flight-time, and acceleration fields.

## State Field Descriptions

| Field | Description |
|---|---|
| `mid` | ID of the detected Mission Pad. Returns `-1` if no Mission Pad is detected. |
| `x` | Detected x-coordinate relative to the Mission Pad. Returns `0` if no pad is detected. |
| `y` | Detected y-coordinate relative to the Mission Pad. Returns `0` if no pad is detected. |
| `z` | Detected z-coordinate relative to the Mission Pad. Returns `0` if no pad is detected. |
| `pitch` | Pitch attitude in degrees. |
| `roll` | Roll attitude in degrees. |
| `yaw` | Yaw attitude in degrees. |
| `vgx` | Velocity along the x-axis. |
| `vgy` | Velocity along the y-axis. |
| `vgz` | Velocity along the z-axis. |
| `templ` | Lowest measured temperature in degrees Celsius. |
| `temph` | Highest measured temperature in degrees Celsius. |
| `tof` | Time-of-flight distance in centimeters. |
| `h` | Height in centimeters. |
| `bat` | Current battery percentage. |
| `baro` | Barometer measurement in centimeters. |
| `time` | Total time for which the motors have been running. |
| `agx` | Acceleration along the x-axis. |
| `agy` | Acceleration along the y-axis. |
| `agz` | Acceleration along the z-axis. |

# Mission Pad Commands

Mission Pad commands include:

```text
mon
moff
mdirection x
go x y z speed mid
curve x1 y1 z1 x2 y2 z2 speed mid
jump x y z speed yaw mid1 mid2
```

For additional details, consult the Mission Pad User Guide on the official Ryze website.

# Safety Feature

If Tello receives no command for **15 seconds**, it automatically lands.

# Resetting Tello Wi-Fi

1. Turn on Tello.
2. Press and hold the power button for five seconds.
3. The indicators turn off and then blink yellow slowly.
4. When the Wi-Fi SSID and password have been reset to their default settings, the indicator blinks yellow quickly.

By default, no Wi-Fi password is configured.

# Support

Ryze Tech Support:

<http://www.ryzerobotics.com/support>

Latest documentation:

<http://www.ryzerobotics.com>

> This content is subject to change.
