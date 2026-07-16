<div align="center">

<img src="https://raw.githubusercontent.com/htl-stp-ecer/.github/main/profile/raccoon-logo.svg" alt="Calibration Board" width="100"/>

# calibration-board

**A custom STM32 carrier board for characterising IMU drift and optical flow — before the sensor ever reaches the robot.**

KiCad PCB · STM32F722 firmware · On-board Madgwick fusion · USB-CDC → LCM bridge · 3D-printed enclosure

![C](https://img.shields.io/badge/C-STM32F722-00599C?logo=c&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-Host%20bridge-00599C?logo=cplusplus&logoColor=white)
![KiCad](https://img.shields.io/badge/KiCad-7+-314CB0?logo=kicad&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-RaccoonOS-orange)

> 📖 **Platform documentation at [raccoon-docs.pages.dev](https://raccoon-docs.pages.dev/)**

</div>

---

## What This Is

Every robot on RaccoonOS leans on its IMU. Heading control, `turn_to_heading_right(90)`, odometry fusion — all of it is only as good as the gyro underneath. So the obvious question is: **which IMU, and how much does it actually drift?**

You can't answer that from a datasheet. Datasheets quote bias stability under conditions your robot will never see. So we built a board to measure it.

This is **bench instrumentation, not a robot.** It is a carrier PCB that hosts IMU candidates on independent SPI buses, streams their raw output to a laptop, and lets you watch the drift accumulate in real time. It exists to make a sensor decision with evidence instead of vibes.

> **This repo is not a template.** It's a hardware bring-up log with working firmware attached. Read it for how we approach a hardware question — the module architecture, the fusion pipeline, the bridge into `raccoon-transport` — not as a board to fab as-is. See [Known Limitations](#known-limitations) first.

---

## What Actually Happened

The board was built to run a bake-off between two IMUs. It didn't go to plan, and that's the interesting part.

| Candidate | Outcome |
|:----------|:--------|
| **Bosch BNO086** — on-chip sensor fusion, quaternion straight out | ❌ **Never came up.** After reset the INT line asserts, but the BNO never actively drives MISO — every "packet" we read was a threshold artefact off a floating line. SPI mode, clock speed, pull-ups, boot delay and pin states were all eliminated in software; the suspicion landed on a broken CS path. It's disabled in the firmware: `.enabled = false /* BNO08x defekt */` |
| **TDK InvenSense ICM-42688-P** (U501, SPI) — 6-axis raw output, fusion is your problem | ✅ **Works.** Became the sensor the board actually characterises |
| **PAA5100** optical flow — surface motion tracking | ✅ **Works.** Added during bring-up; wasn't part of the original question, turned out useful for odometry |

The BNO was supposed to be the easy option — it does fusion for you. Instead the "harder" chip won by being the one that responded, and the fusion moved onto the STM32 where we could see inside it. Losing the BNO is why `imu_fusion.c` exists at all.

The HSE crystal also refused to oscillate at first (`HSEON=1, HSERDY=0` → `Error_Handler` on every boot). That one was real and got fixed in the schematic — an oscillator ground problem. The firmware runs on HSE today.

---

## How It Works

```
ICM-42688-P ─┐                                          ┌─ raccoon/calib_board/icm/accel
             ├─ SPI ─► STM32F722 ──► USB-CDC ──► host ──┤─ raccoon/calib_board/icm/gyro
PAA5100 ─────┘        (fusion,      (binary    (bridge) ├─ raccoon/calib_board/paa/delta_x
                       1 kHz)        frames)            └─ raccoon/calib_board/status/*
```

### On the STM32

**A module registry, Arduino-style.** Each sensor is a `module_t` with a `setup()` and a `loop()`, registered in one array in `app/src/app.c`. An `enabled` flag switches a module off without deleting the entry — which is exactly how the dead BNO is parked instead of being ripped out.

**Soft-start staggering.** Modules come up 25 ms apart rather than all at once. Every peripheral pulls current at bring-up, and simultaneous init is a step load that drags a marginal supply into brownout. Staggering flattens the ramp. (The capacitor inrush at plug-in happens *before* this and isn't helped — that's pure hardware.)

**Fusion on-chip** (`app/src/imu_fusion.c`) — the part worth reading:

- **Madgwick quaternion filter**, no magnetometer — yaw drifts slowly, roll/pitch get corrected against the gravity vector
- **At-rest detection** via a max-min spread over the gyro axes and accel magnitude, with time dwell and hysteresis: ~0.5 s to declare "still", instant exit on motion
- **Live gyro bias estimation** — while at rest, an exponential moving average runs on the raw gyro. *That average is the bias.* It's subtracted before the data enters Madgwick and before it goes out over USB
- Runs at the ICM's 1 kHz ODR; a filter step costs ≈1 µs on the M7 at 216 MHz with the FPU

That last point is the whole reason the board exists: **drift you can measure is drift you can subtract.**

### On the host

`host/` is a C++ bridge (`raccoon-calib-bridge`) that reads binary frames from `/dev/ttyACM*` and republishes them as typed [raccoon-transport](https://github.com/htl-stp-ecer/raccoon-transport) messages — so calibration data flows on the same LCM channels as everything else in RaccoonOS, and BotUI or any subscriber can plot it live. Status channels republish every second, so a subscriber that starts late still learns the current state instead of waiting for an event. Ships with a systemd unit.

---

## Repo Layout

| Path | Contents |
|:-----|:---------|
| `Firmware/` | STM32CubeMX CMake project, target STM32F722RET6. **Capitalised folders are ST-generated, lowercase are ours** |
| `Firmware/app/` | Application logic — module registry, fusion, USB framing |
| `Firmware/drivers/` | Our chip drivers: `bno08x/`, `icm42688p/`, `paa5100/` |
| `Firmware/lib/sh2/` | CEVA BNO08x driver (git submodule) |
| `Firmware/scripts/` | build / flash / debug / uart / swo — **call these, not ST tools directly** |
| `host/` | C++ USB-CDC → raccoon-transport bridge + systemd unit |
| `IMU_Extendor_Board/` | KiCad 7+ project — schematic, PCB, BOM, gerbers in `production/` |
| `enclosure/` | OpenSCAD tray + lid, STL/STEP exports (M2 self-tapping, ~55×55 mm PCB) |
| `Component_Datasheets/` | Datasheets plus hand-written analysis (`external_clock_analysis.md`) |
| `docs/` | Toolchain setup, firmware workflow, hardware overview, coding conventions |

---

## Quick Start

```bash
git submodule update --init --recursive   # sh2 + raccoon-transport

Firmware/scripts/build.sh          # cmake + ninja
Firmware/scripts/build-docker.sh   # same, in a container — no host arm-gcc needed
Firmware/scripts/flash.sh          # build + flash + verify + start over ST-LINK SWD
Firmware/scripts/uart.sh           # stream the debug console (J703, 115200 8N1)
```

Full setup for a fresh machine: [`docs/toolchain-setup.md`](docs/toolchain-setup.md).

### Catching boot output

Start `uart.sh` **before** `flash.sh`. The boot `printf` fires within milliseconds of reset — long before you can switch terminals. Two panes:

```bash
# Pane 1, left running:
Firmware/scripts/uart.sh
# Pane 2:
Firmware/scripts/flash.sh
```

If an expected printf never shows up, the firmware is far more likely hung *before* that line than the UART is broken. Init exposes debug globals (`g_bno_init_status`, `g_paa_init_status`, `g_bno_stage`) — read them over SWD without re-flashing:

```bash
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <addr> 4
```

### The USER LED is a status channel

With no UART adapter attached, the LED is the *only* thing telling you anything:

| Pattern | Meaning |
|:--------|:--------|
| 100 ms on / 100 ms off (fast) | init OK — sensor answered, product IDs read |
| 500 ms on / 500 ms off (slow) | setup failed — INT line or clock path suspect |
| 50 ms on / 950 ms off (pulse) | unexpected init return code |

---

## Known Limitations

Read this before you fab a board or trust a number.

- **BNO086 is dead and disabled.** Suspected broken CS path. Software causes were eliminated; it needs a DMM on `PA4 ↔ BNO086 CSN` before anyone touches firmware again.
- **USB-C (J101) is not populated.** The board is powered through the ST-LINK debug cable, which caps its current budget and means it is **not standalone**. Populating J101 + the USBLC6 ESD diode per schematic is required before it could live on a robot.
- **SWO does not work with ST-LINK V2 clones.** `swo_init` configures TPIU/ITM correctly, but the green clones expose a `T_JTDO` pin that isn't internally wired to their TRACESWO input — reflashing official ST firmware does not fix it. Use UART4 instead, or a genuine STLINK-V3. `__io_putchar` writes to both sinks, so take whichever you can capture.
- **Chip-select assignments aren't fully captured in firmware.** Cross-reference the KiCad schematic before wiring up a new sensor.
- **`Firmware.ioc` is owned by CubeMX.** Regenerating overwrites most of `Core/` and `Drivers/`. Keep code inside `USER CODE BEGIN/END` markers — or better, put it in `app/` and call it from `app_main()`.
- **Parts of the docs and code comments are in German.** This was an internal engineering repo; we haven't translated it.

---

## Part of RaccoonOS

| Repository | What it is |
|:-----------|:-----------|
| [raccoon-transport](https://github.com/htl-stp-ecer/raccoon-transport) | The LCM messaging layer this board publishes into |
| [stm32-data-reader](https://github.com/htl-stp-ecer/stm32-data-reader) | Pi ↔ STM32 SPI bridge on the robot itself |
| [raccoon-lib](https://github.com/htl-stp-ecer/raccoon-lib) | Core robotics library — consumer of the IMU this board evaluated |
| [botui](https://github.com/htl-stp-ecer/botui) | Wombat dashboard — can subscribe to these channels live |
| [documentation](https://raccoon-docs.pages.dev/) | Full platform docs |

---

Built by the Botball team at **HTL St. Pölten**.
