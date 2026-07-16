<div align="center">

<img src="https://raw.githubusercontent.com/htl-stp-ecer/.github/main/profile/raccoon-logo.svg" alt="Calibration Board" width="100"/>

# calibration-board

**A companion board for the KIPR Wombat — absolute position from optical flow, and a reference IMU good enough to calibrate the Wombat's own.**

KiCad PCB · STM32F722 firmware · On-board Madgwick fusion · Persistent flash calibration · USB-CDC → LCM bridge

![C](https://img.shields.io/badge/C-STM32F722-00599C?logo=c&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-Host%20bridge-00599C?logo=cplusplus&logoColor=white)
![KiCad](https://img.shields.io/badge/KiCad-7+-314CB0?logo=kicad&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-KIPR%20Wombat-orange)

> 📖 **Platform documentation at [raccoon-docs.pages.dev](https://raccoon-docs.pages.dev/)**

</div>

---

## What This Is

There are two things a Wombat can't tell you well on its own.

**Where it actually is.** Wheel odometry integrates encoder ticks and believes them. But wheels slip — on a seam, on a pom, on a ramp — and every slipped tick is a lie the robot carries for the rest of the run. Encoders measure *the wheels*, not the ground.

**Whether its IMU is telling the truth.** The Wombat's onboard IMU is fine for "did I turn roughly 90°". It is not a reference you'd want to calibrate anything against.

This board fixes both, and it rides *on the robot* to do it:

- A **PAA5100 optical flow sensor** watches the actual table surface. Counts get scaled through a stored calibration into centimetres — so the robot gets **absolute position** that doesn't care whether a wheel spun
- An **ICM-42688-P** plus Madgwick fusion on an M7 with an FPU gives a gyro reference **good enough to calibrate the Wombat's onboard IMU against**

That's the name: it isn't a board you calibrate, it's the board that *does the calibrating*.

### What it unlocks

- **Onboard IMU calibration** — the board is the reference the Wombat's own sensor gets corrected against
- **Motion teach-in** — move the robot through a path and record the trajectory that actually happened, rather than the one the encoders imagined
- **Automated setup routines** — Botball permits automated routines during setup time, which is exactly the window where a robot can locate and calibrate itself before the run starts

> **This repo is not a template.** It's a working board with an honest bring-up history attached. Read [Known Limitations](#known-limitations) before you fab one.

---

## Calibration That Survives a Power Cycle

The interesting engineering is here, in `app/src/paa_cal_module.c` and `app/inc/calib_store.h`.

**Calibration lives in flash, not in a startup ritual.** Sector 7 (0x08060000, 128 KB) holds one 64-byte block: PAA scaling (`cx_per_cm`, `cy_per_cm`, `height_mm`), the gyro bias, magic, schema version and a CRC32. Firmware code fits in sectors 0–3, so 4–7 are free. Bad CRC or empty flash falls back to defaults and reports `valid = false` — the caller *knows* it has never been calibrated rather than trusting garbage.

**The board boots already calibrated.** On startup the stored gyro bias is loaded straight into `imu_fusion_init()`. No warm-up, no "hold still for ten seconds" at match time.

**Bias is learned continuously and persisted intelligently.** While the board is at rest, an exponential moving average runs on the raw gyro — *that average is the bias*. It's subtracted before Madgwick and before anything goes out over USB. Auto-save only fires when the learned bias drifts more than **0.5 dps** from what's stored, only while at rest (so it's trustworthy), and at most every **2 minutes**. Each save costs a full sector erase — ~1 s and one P/E cycle — so the threshold and interval throttle writes down to a handful per big temperature swing. Gyro bias drifts ~0.05 dps/°C; the design accounts for that instead of ignoring it.

Flash writes are deferred out of interrupt context into the main loop via a pending flag, because `HAL_FLASHEx_Erase` blocks.

**At-rest detection** uses a max-min spread across the gyro axes and accel magnitude, with time dwell and hysteresis: about 0.5 s to declare "still", instant exit on motion. Madgwick runs without a magnetometer at the ICM's 1 kHz ODR — yaw drifts slowly, roll/pitch stay corrected against gravity. A filter step costs ≈1 µs on the M7 at 216 MHz.

**Commands over USB:** `CMD_SET_PAA_CAL` (store new PAA scaling), `CMD_SAVE_GYRO_BIAS` (persist the current at-rest average), `CMD_RESET_GYRO_BIAS`. Telemetry goes the other way: ORIENTATION frames (quaternion + bias + at-rest flag) at 100 Hz, PAA calibration telemetry at 1 Hz.

---

## How It Fits Together

```
PAA5100 ─┐                                              ┌─ raccoon/calib_board/paa/delta_x
         ├─ SPI ─► STM32F722 ──► USB-CDC ──► host ──────┤─ raccoon/calib_board/icm/accel
ICM-42688-P ─┘     fusion 1 kHz   binary     bridge     ├─ raccoon/calib_board/icm/gyro
                   cal in flash   frames                └─ raccoon/calib_board/status/*
```

`host/` is a C++ bridge (`raccoon-calib-bridge`) that reads binary frames off `/dev/ttyACM*` and republishes them as typed [raccoon-transport](https://github.com/htl-stp-ecer/raccoon-transport) messages. Calibration data therefore flows on the same LCM channels as everything else in RaccoonOS — BotUI or any subscriber can plot it live. Status channels republish every second, so a subscriber that starts late still learns current state instead of waiting for an event. Ships with a systemd unit.

### Firmware architecture

**A module registry, Arduino-style.** Each sensor is a `module_t` with `setup()` and `loop()`, registered in one array in `app/src/app.c`. An `enabled` flag parks a module without deleting it — which is exactly how the dead BNO is handled.

**Soft-start staggering.** Modules come up 25 ms apart instead of all at once. Every peripheral draws current at bring-up, and simultaneous init is a step load that can drag a marginal supply into brownout. Staggering flattens the ramp. (Capacitor inrush at plug-in happens *before* this phase and isn't helped — that's pure hardware.)

---

## Bring-Up History

The board was meant to carry a **Bosch BNO086** — it does sensor fusion on-chip and hands you a quaternion, no maths required. It never came up. After reset the INT line asserts, but the BNO never actively drives MISO; every "packet" was a threshold artefact off a floating line. SPI mode, clock speed, pull-ups, boot delay and pin states were all eliminated in software, and suspicion landed on a broken CS path. It's parked in the firmware: `.enabled = false /* BNO08x defekt */`.

So the **ICM-42688-P** took over: raw 6-axis, fusion is your problem. Which is why `imu_fusion.c` exists at all — losing the easy chip is what moved Madgwick onto the STM32, where the bias estimator could be built in and the whole pipeline is visible instead of hidden behind a sensor hub. Not the plan, but the better outcome.

The HSE crystal also refused to oscillate at first (`HSEON=1, HSERDY=0` → `Error_Handler` every boot). That one was real, and got fixed in the schematic — an oscillator ground problem. The firmware runs on HSE today.

---

## Repo Layout

| Path | Contents |
|:-----|:---------|
| `Firmware/` | STM32CubeMX CMake project, target STM32F722RET6. **Capitalised folders are ST-generated, lowercase are ours** |
| `Firmware/app/` | Module registry, fusion, calibration store, USB framing |
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

If an expected printf never shows up, the firmware is far more likely hung *before* that line than the UART is broken. Init exposes debug globals (`g_bno_init_status`, `g_paa_init_status`, `g_bno_stage`, `g_paa_cal_loaded`) — read them over SWD without re-flashing:

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
- **USB-C (J101) is not populated.** The board currently runs off the ST-LINK debug cable, which caps its current budget and means it is **not standalone**. Populating J101 + the USBLC6 ESD diode per schematic is required before it can ride on a robot properly.
- **SWO does not work with ST-LINK V2 clones.** `swo_init` configures TPIU/ITM correctly, but the green clones expose a `T_JTDO` pin that isn't internally wired to their TRACESWO input — reflashing official ST firmware does not fix it. Use UART4 instead, or a genuine STLINK-V3. `__io_putchar` writes to both sinks, so take whichever you can capture.
- **PAA scaling is height-dependent.** Defaults assume 19 mm sensor height. Mount height changes the counts-per-cm — recalibrate via `CMD_SET_PAA_CAL` after any remount, or your "absolute position" is confidently wrong.
- **The flash layout assumes small firmware.** The calibration block sits in sector 7 because the code fits in sectors 0–3. Firmware growing past ~448 KB breaks that assumption.
- **Chip-select assignments aren't fully captured in firmware.** Cross-reference the KiCad schematic before wiring up a new sensor.
- **`Firmware.ioc` is owned by CubeMX.** Regenerating overwrites most of `Core/` and `Drivers/`. Keep code inside `USER CODE BEGIN/END` markers — or better, put it in `app/` and call it from `app_main()`.
- **Parts of the docs and code comments are in German.** This was an internal engineering repo; we haven't translated it.

---

## Part of RaccoonOS

| Repository | What it is |
|:-----------|:-----------|
| [raccoon-transport](https://github.com/htl-stp-ecer/raccoon-transport) | The LCM messaging layer this board publishes into |
| [stm32-data-reader](https://github.com/htl-stp-ecer/stm32-data-reader) | Pi ↔ STM32 SPI bridge on the robot itself |
| [raccoon-lib](https://github.com/htl-stp-ecer/raccoon-lib) | Core robotics library — odometry and heading control downstream of this |
| [botui](https://github.com/htl-stp-ecer/botui) | Wombat dashboard — can subscribe to these channels live |
| [documentation](https://raccoon-docs.pages.dev/) | Full platform docs |

---

Built by the Botball team at **HTL St. Pölten**.
