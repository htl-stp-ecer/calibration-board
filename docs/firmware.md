# Firmware Workflow

Day-to-day build, flash, and debug for the STM32F722RET6 firmware. For one-time setup see `toolchain-setup.md`.

## Helper scripts

All workflow goes through `Firmware/scripts/`. They work from any CWD, source `_common.sh` for paths, and filter `libusb:` noise + ANSI escapes out of ST tool output.

| Script | What it does |
|---|---|
| `build.sh [Debug\|Release]` | Configure (if needed) + ninja build. Default: Debug. |
| `build-docker.sh [Debug\|Release]` | Same as `build.sh` but inside a Debian builder container — no host arm-none-eabi-gcc / cmake / ninja required. Image is auto-built on first run. |
| `flash.sh [Debug\|Release]` | `build.sh` then flash + verify + start on SWD. |
| `reset.sh` | Software-reset target. No re-flash. |
| `erase.sh` | Full chip erase under reset. Use when target is bricked / unresponsive. |
| `gdbserver.sh` | Foreground `ST-LINK_gdbserver` on `:61234`. For IDE external-server debug configs. |
| `debug.sh` | `build.sh` + gdbserver in background + interactive `arm-none-eabi-gdb` with `load` and `tbreak main`. |

### Env overrides

| Var | Default | Effect |
|---|---|---|
| `BUILD_TYPE` | `Debug` | Selects CMake preset and build dir |
| `SWD_FREQ_KHZ` | `4000` | ST-LINK SWD clock |
| `GDB_PORT` | `61234` | Port for `gdbserver.sh` / `debug.sh` |
| `PROG_CLI`, `GDB_SERVER`, `GDB` | binaries on PATH | Override tool binaries |

Example: release build, flashed at slower clock:

```bash
SWD_FREQ_KHZ=1800 Firmware/scripts/flash.sh Release
```

### Docker builder

`Firmware/Dockerfile` defines a `debian:trixie-slim` image with `gcc-arm-none-eabi` 14.2 + cmake + ninja. The host source tree is bind-mounted at **the same absolute path** inside the container, so the paths CMake bakes into `CMakeCache.txt` are valid both inside and outside the container. Consequence: you can freely alternate `build-docker.sh` and `build.sh` — the build cache is shared and reused, no clean-and-reconfigure dance.

Override the image tag with `BUILD_DOCKER_IMAGE=…`. Rebuild the image after editing the Dockerfile with `docker build -t raccoon-calibration-firmware:latest Firmware/`.

Flashing still happens host-side (`flash.sh`) — the container deliberately does **not** contain `STM32_Programmer_CLI`. ST's license forbids redistributing it, and USB passthrough for ST-LINK is fragile across hosts.

## Project structure

```
Firmware/
├── CMakeLists.txt             # User-editable
├── CMakePresets.json          # Debug/Release presets, Ninja, gcc-arm-none-eabi toolchain
├── Firmware.ioc               # STM32CubeMX project — regenerates Core/ + Drivers/
├── STM32F722XX_FLASH.ld       # Linker script (512 KB flash, 256 KB RAM)
├── startup_stm32f722xx.s      # Reset handler + vector table
├── cmake/
│   ├── gcc-arm-none-eabi.cmake     # Toolchain file (cortex-m7 + fpv5-sp-d16)
│   └── stm32cubemx/                # CubeMX-generated HAL driver list
├── Core/
│   ├── Inc/{main,stm32f7xx_hal_conf,stm32f7xx_it}.h
│   └── Src/{main,stm32f7xx_hal_msp,stm32f7xx_it,syscalls,sysmem,system_stm32f7xx}.c
├── Drivers/
│   ├── CMSIS/                       # ARM CMSIS + STM32F7xx device headers
│   └── STM32F7xx_HAL_Driver/        # ST HAL sources
└── scripts/                         # See above
```

**Touch-zone rules:**

- `Core/Src/main.c` and friends: only edit inside `/* USER CODE BEGIN ... */` … `/* USER CODE END ... */` markers. CubeMX overwrites everything outside on regeneration.
- `Firmware.ioc`: edit only in STM32CubeMX. Regenerate with **Project → Generate Code** from the CubeMX UI (or `stm32cubemx` CLI). Commit the regenerated diff together with the `.ioc` change.
- `cmake/stm32cubemx/`: auto-generated. Do not hand-edit.
- `CMakeLists.txt`: user-owned. Add your sources to `target_sources()`, includes to `target_include_directories()`.

## CLion integration

The `.idea/debugServers/ST_LINK.xml` already points at the installed CubeCLT paths. Recommended run-configs:

**1. Flash (Shell Script)**
- Script path: `$ProjectFileDir$/scripts/flash.sh`
- Working dir: `$ProjectFileDir$`
- Before launch: *(none — `flash.sh` builds itself)*

**2. Debug (Embedded GDB Server)**
- Target: `Firmware` (CMake)
- Executable binary: `Firmware`
- `'target remote' args`: `tcp:localhost:61234`
- GDB Server: `$ProjectFileDir$/scripts/gdbserver.sh`
- Reset command: `monitor reset`

With these two, "Run" flashes, "Debug" attaches GDB through the same gdbserver the script starts.

## Headless / AI agent workflow

Everything runs without CLion. Example one-shot edit→flash cycle for an agent:

```bash
# edit code
$EDITOR Firmware/Core/Src/main.c
# build + flash + run, single command, works from any CWD
Firmware/scripts/flash.sh
# observe via UART (UART4 on PA0/PA1) or SWO (PB3) — TODO: add monitor script
```

For a brick (target won't enumerate on SWD under normal connect), use connect-under-reset path:

```bash
Firmware/scripts/erase.sh    # uses mode=UR (connect-under-reset) + full erase
Firmware/scripts/flash.sh    # then re-flash
```

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Error: No STM32 target found` | Target unpowered, SWD wiring wrong, or BOOT0 stuck | Check 3V3 rail, SWCLK/SWDIO continuity (PA14/PA13), tie BOOT0 low |
| `libusb: error … requires write access` | udev rules not applied | `sudo udevadm control --reload-rules && sudo udevadm trigger` + re-plug ST-LINK |
| `Duplicate preset: "Debug"` from cmake | Local `CMakeUserPresets.json` collides with `CMakePresets.json` | Rename local presets (e.g. `Debug-local`) or delete the user file |
| Verify fails after download | Wrong target / corrupted flash sector | `erase.sh` then retry |
| GDB server: `Port 61234 already in use` | Previous `gdbserver.sh` still running | `pkill -f ST-LINK_gdbserver` or set `GDB_PORT=61235` |
