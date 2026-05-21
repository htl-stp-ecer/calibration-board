# Raccoon Calibration Board

IMU calibration carrier board for the Raccoon Botball robot. Contains an STM32F722RET6 (Cortex-M7) and hosts IMU evaluation hardware (BNO080/085 candidate, ICM-42688-P candidate) for characterising sensor drift and bias before deployment on the main robot.

## Repo layout

| Path | Contents |
|---|---|
| `Firmware/` | STM32CubeMX-generated CMake project. Target: STM32F722RET6. Built with system `arm-none-eabi-gcc`. |
| `Firmware/Core/`, `Firmware/Drivers/` | STM32CubeMX-owned. Regenerated from `Firmware.ioc` — don't hand-edit outside `USER CODE BEGIN/END` markers. |
| `Firmware/app/` | Application logic (state machines, glue). Lowercase = project-owned, survives CubeMX regen. |
| `Firmware/drivers/` | Project-owned hardware drivers (e.g. `drivers/bno08x/`). |
| `Firmware/lib/` | Third-party libraries. `lib/sh2/` is the CEVA BNO08x driver (git submodule). |
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
Firmware/scripts/uart.sh           # stream UART4 printf output (J703, 115200) — start before flash to catch boot
Firmware/scripts/swo.sh            # stream SWO/ITM trace (PB3) — needs genuine STLINK-V3, clones don't route it
```

Scripts work from any CWD and pipe ST tool output through a filter that removes `libusb:` permission warnings and ANSI escapes. Override `BUILD_TYPE`, `SWD_FREQ_KHZ`, or `GDB_PORT` via env vars.

## Debug printf — how it gets out

`__io_putchar` in `Firmware/app/src/swo.c` writes every character to **both** SWO (PB3) and UART4 (PA0). Two sinks, take whichever you can capture:

- **UART4 → CP2102 USB-UART → `/dev/ttyUSB0`** is the **verified working path**. Wire the adapter to J703: adapter-RX ↔ PA0 (MCU TX), adapter-TX ↔ PA1 (MCU RX), **GND shared** (mandatory — without common ground you get 0 bytes silently). Do **not** also bridge VCC; the board is already powered through the ST-LINK. Run `Firmware/scripts/uart.sh` — it auto-detects the CP2102, sets 115200 8N1 raw, and streams to stdout + `build/Debug/uart.log`.
- **SWO** is implemented (`swo_init` configures TPIU/ITM correctly) but **does not work with the green ST-LINK V2 clone** — the `T_JTDO` pin on the clone exists but isn't internally wired to its TRACESWO input. Reflashing the official ST firmware does not fix this; it's a hardware limitation of the clone. Use SWO only with a genuine STLINK-V3.

**Catch boot output**: start `uart.sh` *before* `flash.sh` — the boot `printf` fires within milliseconds of reset, well before you can switch terminals. The recommended two-pane workflow:

```bash
# Pane 1 (left running):
Firmware/scripts/uart.sh
# Pane 2:
Firmware/scripts/flash.sh
```

If a printf you expect doesn't appear, the firmware is more likely hung **before** that line than UART is broken. The init path exposes debug globals (`g_bno_init_status`, `g_paa_init_status`, `g_bno_stage`) — read them via `STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <addr> 4` to see how far init progressed without re-flashing.

## Toolchain dependencies

- `arm-none-eabi-gcc` 14+ (apt: `gcc-arm-none-eabi`) — **OR** Docker (use `build-docker.sh`)
- `cmake` 3.22+, `ninja-build` — only needed for native build
- **STM32CubeCLT 1.21.0** at `/opt/st/stm32cubeclt_1.21.0/` (provides `STM32_Programmer_CLI` and `ST-LINK_gdbserver`, symlinked into `/usr/local/bin/`)
- ST-LINK V2 (or V3) on SWD; user must be in `plugdev` group

Full setup steps for a fresh machine: `docs/toolchain-setup.md`.

## Conventions

- **Don't hand-roll `STM32_Programmer_CLI` calls.** Extend `Firmware/scripts/` instead. New verbs (e.g. SWO trace, UART monitor) should live alongside as `Firmware/scripts/<verb>.sh` and source `_common.sh`.
- The `Firmware.ioc` is owned by STM32CubeMX. Regenerating it overwrites most of `Core/` and `Drivers/`; keep custom code inside the `/* USER CODE BEGIN ... */` markers, or — better — put it in `Firmware/app/` and call it from `app_main()`.
- **Capitalised folders are ST-generated, lowercase are ours.** New C code goes into `app/` or `drivers/<chip>/`, never into `Core/` or `Drivers/`. Full C style guide: `docs/coding-conventions.md`.
- `lib/sh2/` is a git submodule — fresh clones need `git submodule update --init`.
- `CMakeUserPresets.json` is `.gitignore`'d — local-only PATH overrides go there, not in the committed `CMakePresets.json`.

## Further reading

- `docs/toolchain-setup.md` — fresh machine setup (CubeCLT install, udev, symlinks)
- `docs/firmware.md` — build/flash/debug workflow, project structure, CLion integration
- `docs/hardware.md` — board overview, MCU pin map, peripheral assignments
- `docs/coding-conventions.md` — folder layout, naming, header rules for `app/` and `drivers/`
