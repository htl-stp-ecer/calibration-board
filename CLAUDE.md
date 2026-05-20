# Raccoon Calibration Board

IMU calibration carrier board for the Raccoon Botball robot. Contains an STM32F722RET6 (Cortex-M7) and hosts IMU evaluation hardware (BNO080/085 candidate, ICM-42688-P candidate) for characterising sensor drift and bias before deployment on the main robot.

## Repo layout

| Path | Contents |
|---|---|
| `Firmware/` | STM32CubeMX-generated CMake project. Target: STM32F722RET6. Built with system `arm-none-eabi-gcc`. |
| `Firmware/scripts/` | Build / flash / debug helpers — call these instead of invoking ST tools directly. |
| `IMU_Extendor_Board/` | KiCad 7+ project for the carrier PCB. BOM in `IMU_Extendor_Board.csv`, production gerbers in `production/`. |
| `Component_Datasheets/` | Datasheets for STM32, IMUs, oscillators, regulator. Also includes hand-written analysis docs (e.g. `external_clock_analysis.md`). |
| `docs/` | Setup, workflow, hardware overview. **Read these first.** |

## Quick start

```bash
Firmware/scripts/build.sh          # cmake configure (if needed) + ninja build
Firmware/scripts/build-docker.sh   # same, but inside Debian container — no host arm-gcc needed
Firmware/scripts/flash.sh          # build + flash + verify + start on ST-LINK SWD
Firmware/scripts/debug.sh          # interactive GDB session
```

Scripts work from any CWD and pipe ST tool output through a filter that removes `libusb:` permission warnings and ANSI escapes. Override `BUILD_TYPE`, `SWD_FREQ_KHZ`, or `GDB_PORT` via env vars.

## Toolchain dependencies

- `arm-none-eabi-gcc` 14+ (apt: `gcc-arm-none-eabi`) — **OR** Docker (use `build-docker.sh`)
- `cmake` 3.22+, `ninja-build` — only needed for native build
- **STM32CubeCLT 1.21.0** at `/opt/st/stm32cubeclt_1.21.0/` (provides `STM32_Programmer_CLI` and `ST-LINK_gdbserver`, symlinked into `/usr/local/bin/`)
- ST-LINK V2 (or V3) on SWD; user must be in `plugdev` group

Full setup steps for a fresh machine: `docs/toolchain-setup.md`.

## Conventions

- **Don't hand-roll `STM32_Programmer_CLI` calls.** Extend `Firmware/scripts/` instead. New verbs (e.g. SWO trace, UART monitor) should live alongside as `Firmware/scripts/<verb>.sh` and source `_common.sh`.
- The `Firmware.ioc` is owned by STM32CubeMX. Regenerating it overwrites most of `Core/` and `Drivers/`; keep custom code inside the `/* USER CODE BEGIN ... */` markers.
- `CMakeUserPresets.json` is `.gitignore`'d — local-only PATH overrides go there, not in the committed `CMakePresets.json`.

## Further reading

- `docs/toolchain-setup.md` — fresh machine setup (CubeCLT install, udev, symlinks)
- `docs/firmware.md` — build/flash/debug workflow, project structure, CLion integration
- `docs/hardware.md` — board overview, MCU pin map, peripheral assignments
