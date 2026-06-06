# raccoon-calib-bridge

USB-CDC → `raccoon-transport` bridge for the calibration-board firmware
(`../Firmware`).  Reads binary frames from `/dev/ttyACM*`, publishes
typed messages on `raccoon/calib_board/...` channels, and reports board
+ per-sensor status on dedicated status channels.

## Channels

| Channel                                    | Type            | Units / payload      |
|--------------------------------------------|-----------------|----------------------|
| `raccoon/calib_board/icm/accel`            | `vector3f_t`    | g (x,y,z)            |
| `raccoon/calib_board/icm/gyro`             | `vector3f_t`    | dps (x,y,z)          |
| `raccoon/calib_board/icm/temperature`      | `scalar_f_t`    | °C                   |
| `raccoon/calib_board/paa/delta_x`          | `scalar_i32_t`  | raw counts           |
| `raccoon/calib_board/paa/delta_y`          | `scalar_i32_t`  | raw counts           |
| `raccoon/calib_board/paa/squal`            | `scalar_i32_t`  | 0..169 surface qual. |
| `raccoon/calib_board/paa/shutter`          | `scalar_i32_t`  | raw 16-bit           |
| `raccoon/calib_board/paa/motion`           | `scalar_i32_t`  | bitfield             |
| `raccoon/calib_board/status/board`         | `string_t`      | `connected`/`disconnected` |
| `raccoon/calib_board/status/port`          | `string_t`      | `/dev/ttyACMn` or `(none)` |
| `raccoon/calib_board/status/icm`           | `string_t`      | `ok` / `init_failed:<reason>` |
| `raccoon/calib_board/status/paa`           | `string_t`      | `connected` / `absent` / `init_failed:<reason>` |
| `raccoon/calib_board/status/stats`         | `string_t`      | JSON (counters)      |

All status channels are republished every 1 s so a subscriber that comes
up after the bridge can pick up current state without having to wait
for an event.

## Build

```bash
git submodule update --init --recursive   # first time only
scripts/build.sh                          # native build → build/raccoon-calib-bridge
CMAKE_BUILD_TYPE=Debug scripts/build.sh   # debug build
```

`scripts/build.sh` will auto-init the submodule and also accepts the
sibling `../../raccoon-transport` directory as a fallback when the
workspace is checked out flat.

## Run (foreground)

```bash
build/raccoon-calib-bridge                          # auto-detect /dev/ttyACM*
build/raccoon-calib-bridge --port /dev/ttyACM0      # explicit
CALIB_BRIDGE_LOG=debug build/raccoon-calib-bridge   # noisy
```

The bridge **runs even when the board is not connected** — it just
publishes `status/board = "disconnected"` and retries the open every
500 ms.  When the board appears it auto-attaches; when it disappears it
goes back to retrying.  Same for the PAA5100 (status frames from the
firmware report PAA connect/disconnect events mid-run).

## Install as systemd service

```bash
scripts/build.sh
scripts/install.sh           # asks for sudo
systemctl status   raccoon-calib-bridge
journalctl -u      raccoon-calib-bridge -f
```

Override defaults in `/etc/default/raccoon-calib-bridge`, then
`systemctl restart raccoon-calib-bridge`.

## Status semantics

* `status/board = connected`     — port open, bytes flowing
* `status/board = disconnected`  — port closed (either never opened, lost, or silence-timeout fired)
* `status/icm   = ok`            — ICM init reported OK (from firmware STATUS frame, or inferred from incoming ICM frames)
* `status/icm   = init_failed:…` — firmware reported a specific failure
* `status/paa   = connected`     — firmware STATUS frame reports PAA init OK
* `status/paa   = absent`        — firmware STATUS frame reports no PAA present
* `status/paa   = board_disconnected` — board itself is down; PAA state unknown

## Protocol

Frames are documented in `shared/calib_bridge/Framing.h` and **must stay
byte-identical** to `../Firmware/app/inc/framing.h`.  Same CRC, same
endianness, same payload layout.  If you change one, change the other.

## Layout

```
host/
├── CMakeLists.txt
├── README.md
├── scripts/
│   ├── build.sh
│   └── install.sh
├── shared/
│   └── calib_bridge/Framing.h     # frame format + decoders
├── src/
│   ├── main.cpp
│   └── calib_bridge/
│       ├── Application.{h,cpp}    # main loop, reconnect, status
│       ├── Channels.h             # channel name constants
│       ├── Config.{h,cpp}         # CLI + ENV config
│       ├── FrameDecoder.{h,cpp}   # sync/CRC/dispatch
│       ├── Publisher.{h,cpp}      # raccoon::Transport adapter
│       └── SerialPort.{h,cpp}     # /dev/ttyACM* RAII + autodetect
├── systemd/
│   └── raccoon-calib-bridge.service
└── third_party/
    └── raccoon-transport/   # git submodule
```
