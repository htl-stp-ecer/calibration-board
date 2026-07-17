<div align="center">

<img src="https://raw.githubusercontent.com/htl-stp-ecer/.github/main/profile/raccoon-logo.svg" alt="Calibration Board" width="100"/>

# calibration-board

**A companion board for the KIPR Wombat: absolute position from optical flow, and a reference IMU good enough to calibrate the Wombat's own.**

KiCad PCB · STM32F722 firmware · On-board Madgwick fusion · Persistent flash calibration · USB-CDC to LCM bridge

![C](https://img.shields.io/badge/C-STM32F722-00599C?logo=c&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-Host%20bridge-00599C?logo=cplusplus&logoColor=white)
![KiCad](https://img.shields.io/badge/KiCad-7+-314CB0?logo=kicad&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-KIPR%20Wombat-orange)

> 📖 Platform documentation at [raccoon-docs.pages.dev](https://raccoon-docs.pages.dev/)

</div>

---

## What this is

There are two things a Wombat can't tell you well on its own.

The first is where it actually is. Wheel odometry integrates encoder ticks and trusts them, but wheels slip on seams, on poms and on ramps, and every slipped tick stays in the estimate for the rest of the run. Encoders measure the wheels, not the ground.

The second is whether its IMU is accurate. The Wombat's onboard IMU is fine for checking that a turn was roughly 90°. It isn't good enough to calibrate anything against.

This board addresses both, and it rides on the robot rather than sitting on a bench:

- A PAA5100 optical flow sensor tracks the table surface. Counts are scaled through a stored calibration into centimetres, which gives the robot an absolute position that doesn't depend on whether a wheel spun.
- An ICM-42688-P with Madgwick fusion on an M7 with an FPU provides a gyro reference accurate enough to calibrate the Wombat's onboard IMU against.

Hence the name. It isn't a board you calibrate, it's the board that does the calibrating.

### What it's for

- Calibrating the onboard IMU, with this board as the reference
- Motion teach-in, recording the trajectory that actually happened rather than the one the encoders inferred
- Automated setup routines. Botball allows automated routines during setup time, which is when a robot can locate and calibrate itself before the run starts

This repo isn't a template. It's a working board with an honest bring-up history attached. Read [Known limitations](#known-limitations) before fabbing one.

---

## Calibration that survives a power cycle

Most of the interesting work is in `app/src/paa_cal_module.c` and `app/inc/calib_store.h`.

Calibration lives in flash rather than in a startup routine. Sector 7 (0x08060000, 128 KB) holds a single 64-byte block: PAA scaling (`cx_per_cm`, `cy_per_cm`, `height_mm`), the gyro bias, a magic number, a schema version and a CRC32. The firmware fits in sectors 0 to 3, so 4 to 7 are free. A bad CRC or empty flash falls back to defaults and reports `valid = false`, so the caller knows it has never been calibrated instead of trusting garbage.

The board boots already calibrated. At startup the stored gyro bias is loaded straight into `imu_fusion_init()`, so there's no warm-up and no need to hold the robot still before a match.

Bias is learned continuously and persisted carefully. While the board is at rest, an exponential moving average runs on the raw gyro, and that average is the bias. It's subtracted before Madgwick and before anything is sent over USB. Auto-save only fires when the learned bias drifts more than 0.5 dps from the stored value, only while at rest so the value is trustworthy, and at most every 2 minutes. Each save costs a full sector erase, roughly 1 s and one P/E cycle, so the threshold and interval keep writes down to a few per large temperature swing. Gyro bias drifts around 0.05 dps/°C, which is what the thresholds are sized for.

Flash writes are deferred out of interrupt context into the main loop via a pending flag, because `HAL_FLASHEx_Erase` blocks.

At-rest detection uses a max-min spread across the gyro axes and accel magnitude, with time dwell and hysteresis. It takes about 0.5 s to declare the board still, and exits immediately on motion. Madgwick runs without a magnetometer at the ICM's 1 kHz ODR, so yaw drifts slowly while roll and pitch stay corrected against gravity. A filter step costs about 1 µs on the M7 at 216 MHz.

Commands over USB: `CMD_SET_PAA_CAL` stores new PAA scaling, `CMD_SAVE_GYRO_BIAS` persists the current at-rest average, `CMD_RESET_GYRO_BIAS` clears it. Telemetry goes the other way, with ORIENTATION frames (quaternion, bias, at-rest flag) at 100 Hz and PAA calibration telemetry at 1 Hz.

---

## How it fits together

```
PAA5100 ─┐                                              ┌─ raccoon/calib_board/paa/delta_x
         ├─ SPI ─► STM32F722 ──► USB-CDC ──► host ──────┤─ raccoon/calib_board/icm/accel
ICM-42688-P ─┘     fusion 1 kHz   binary     bridge     ├─ raccoon/calib_board/icm/gyro
                   cal in flash   frames                └─ raccoon/calib_board/status/*
```

`host/` is a C++ bridge (`raccoon-calib-bridge`) that reads binary frames from `/dev/ttyACM*` and republishes them as typed [raccoon-transport](https://github.com/htl-stp-ecer/raccoon-transport) messages. Calibration data flows on the same LCM channels as everything else in RaccoonOS, so BotUI or any other subscriber can plot it live. Status channels republish every second, which means a subscriber that starts late still picks up the current state instead of waiting for an event. A systemd unit is included.

### Firmware architecture

The module registry is Arduino-style. Each sensor is a `module_t` with `setup()` and `loop()`, registered in one array in `app/src/app.c`. An `enabled` flag parks a module without deleting it, which is how the dead BNO is handled.

Modules are started 25 ms apart rather than all at once. Every peripheral draws current at bring-up, and simultaneous init is a step load that can pull a marginal supply into brownout. Staggering flattens the ramp. The capacitor inrush at plug-in happens before this phase and isn't affected.

---

## Bring-up history

The board was designed around a Bosch BNO086, which does sensor fusion on-chip and outputs a quaternion directly. It never came up. After reset the INT line asserts, but the BNO never actively drives MISO, so every packet we read was a threshold artefact from a floating line. SPI mode, clock speed, pull-ups, boot delay and pin states were all ruled out in software, which left a suspected broken CS path. It's parked in the firmware with `.enabled = false /* BNO08x defekt */`.

The ICM-42688-P took over instead: raw 6-axis, with fusion left to us. That's why `imu_fusion.c` exists. Losing the chip that would have done fusion internally is what moved Madgwick onto the STM32, where the bias estimator could be added and the whole pipeline is visible rather than hidden behind a sensor hub.

The HSE crystal also refused to oscillate at first, leaving `HSEON=1, HSERDY=0` and dropping into `Error_Handler` on every boot. That turned out to be an oscillator ground problem and was fixed in the schematic. The firmware runs on HSE now.

---

## Repo layout

| Path | Contents |
|:-----|:---------|
| `Firmware/` | STM32CubeMX CMake project, target STM32F722RET6. Capitalised folders are ST-generated, lowercase are ours |
| `Firmware/app/` | Module registry, fusion, calibration store, USB framing |
| `Firmware/drivers/` | Our chip drivers: `bno08x/`, `icm42688p/`, `paa5100/` |
| `Firmware/lib/sh2/` | CEVA BNO08x driver (git submodule) |
| `Firmware/scripts/` | build / flash / debug / uart / swo. Call these rather than ST tools directly |
| `host/` | C++ USB-CDC to raccoon-transport bridge, plus systemd unit |
| `IMU_Extendor_Board/` | KiCad 7+ project: schematic, PCB, BOM, gerbers in `production/` |
| `enclosure/` | OpenSCAD tray and lid, STL/STEP exports (M2 self-tapping, ~55x55 mm PCB) |
| `Component_Datasheets/` | Datasheets plus hand-written analysis (`external_clock_analysis.md`) |
| `docs/` | Toolchain setup, firmware workflow, hardware overview, coding conventions |

---

## Quick start

```bash
git submodule update --init --recursive   # sh2 + raccoon-transport

Firmware/scripts/build.sh          # cmake + ninja
Firmware/scripts/build-docker.sh   # same, in a container, no host arm-gcc needed
Firmware/scripts/flash.sh          # build + flash + verify + start over ST-LINK SWD
Firmware/scripts/uart.sh           # stream the debug console (J703, 115200 8N1)
```

Full setup for a fresh machine: [`docs/toolchain-setup.md`](docs/toolchain-setup.md).

### Catching boot output

Start `uart.sh` before `flash.sh`. The boot `printf` fires within milliseconds of reset, well before you can switch terminals. Two panes:

```bash
# Pane 1, left running:
Firmware/scripts/uart.sh
# Pane 2:
Firmware/scripts/flash.sh
```

If an expected printf never appears, the firmware is more likely hung before that line than the UART is broken. Init exposes debug globals (`g_bno_init_status`, `g_paa_init_status`, `g_bno_stage`, `g_paa_cal_loaded`) that you can read over SWD without re-flashing:

```bash
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <addr> 4
```

### The USER LED is a status channel

With no UART adapter attached, the LED is the only thing reporting anything:

| Pattern | Meaning |
|:--------|:--------|
| 100 ms on / 100 ms off (fast) | init OK, sensor answered, product IDs read |
| 500 ms on / 500 ms off (slow) | setup failed, INT line or clock path suspect |
| 50 ms on / 950 ms off (pulse) | unexpected init return code |

---

## Known limitations

Worth reading before you fab a board or trust a number.

- The BNO086 is dead and disabled. A broken CS path is the suspicion. Software causes were ruled out, so it needs a DMM on `PA4` to `BNO086 CSN` before anyone touches the firmware again.
- USB-C (J101) is not populated. The board runs off the ST-LINK debug cable, which caps its current budget and means it isn't standalone. J101 and the USBLC6 ESD diode need populating per the schematic before it can ride on a robot properly.
- SWO doesn't work with ST-LINK V2 clones. `swo_init` configures TPIU/ITM correctly, but the green clones expose a `T_JTDO` pin that isn't internally wired to their TRACESWO input, and reflashing official ST firmware doesn't fix it. Use UART4 instead, or a genuine STLINK-V3. `__io_putchar` writes to both sinks, so take whichever you can capture.
- PAA scaling depends on mount height. The defaults assume 19 mm. Changing the height changes counts-per-cm, so recalibrate via `CMD_SET_PAA_CAL` after any remount or the absolute position will be confidently wrong.
- The flash layout assumes small firmware. The calibration block sits in sector 7 because the code fits in sectors 0 to 3. Firmware growing past about 448 KB breaks that.
- Chip-select assignments aren't fully captured in the firmware. Cross-reference the KiCad schematic before wiring up a new sensor.
- `Firmware.ioc` is owned by CubeMX. Regenerating overwrites most of `Core/` and `Drivers/`, so keep code inside the `USER CODE BEGIN/END` markers, or put it in `app/` and call it from `app_main()`.
- Parts of the docs and code comments are in German. This was an internal engineering repo and we haven't translated it.

---

## Part of RaccoonOS

| Repository | What it is |
|:-----------|:-----------|
| [raccoon-transport](https://github.com/htl-stp-ecer/raccoon-transport) | The LCM messaging layer this board publishes into |
| [stm32-data-reader](https://github.com/htl-stp-ecer/stm32-data-reader) | Pi to STM32 SPI bridge on the robot itself |
| [raccoon-lib](https://github.com/htl-stp-ecer/raccoon-lib) | Core robotics library, odometry and heading control downstream of this |
| [raccoon-robots](https://github.com/htl-stp-ecer/raccoon-robots) | Real competition robots built on the platform |
| [botui](https://github.com/htl-stp-ecer/botui) | Wombat dashboard, can subscribe to these channels live |
| [documentation](https://raccoon-docs.pages.dev/) | Full platform docs |

---

Built by team `axht-3085`, the Botball team at HTL St. Pölten.
