# Toolchain Setup

Fresh-machine setup for building, flashing, and debugging the calibration board firmware on Linux (Ubuntu/Debian-based). For day-to-day workflow see `firmware.md`.

## 1. Compiler + build tools

**Option A — native (apt):**

```bash
sudo apt install gcc-arm-none-eabi cmake ninja-build
```

**Option B — Docker (skip this step entirely):** if you have Docker installed, use `Firmware/scripts/build-docker.sh` and you don't need any of the above on the host. The first invocation builds a `debian:trixie-slim` image with the C toolchain baked in (~400 MB, ~2 min one-time cost). See `firmware.md` for details. **You still need steps 2–4** for flashing — Docker only solves the build side.

Verify:

```bash
arm-none-eabi-gcc --version    # expect 14.x
cmake --version                 # expect 3.22+
```

## 2. STM32CubeCLT

The flash/debug stack (`STM32_Programmer_CLI` + `ST-LINK_gdbserver`) ships as part of STM32CubeCLT. **Not in apt** — download manually:

1. Get the Linux `.deb` bundle from https://www.st.com/en/development-tools/stm32cubeclt.html (requires free ST account).
2. Unpack and install:
   ```bash
   unzip st-stm32cubeclt_*_amd64.deb_bundle.sh.zip
   chmod +x st-stm32cubeclt_*_amd64.deb_bundle.sh
   ./st-stm32cubeclt_*_amd64.deb_bundle.sh --target /tmp/clt --noexec --quiet --noprogress
   sudo apt-get install -y \
     /tmp/clt/st-stlink-server-*.deb \
     /tmp/clt/st-stlink-udev-rules-*.deb \
     /tmp/clt/st-stm32cubeclt-*.deb
   ```
   Accept the default install path `/opt/st/stm32cubeclt_1.21.0/` — the helper scripts and CLion debug-server config assume it.

## 3. System-wide ST tool symlinks

The helper scripts and any AI agent expect `STM32_Programmer_CLI` / `ST-LINK_gdbserver` on `PATH`. Symlinks in `/usr/local/bin/` work for all shell types (including non-login shells):

```bash
sudo ln -sf /opt/st/stm32cubeclt_1.21.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI /usr/local/bin/
sudo ln -sf /opt/st/stm32cubeclt_1.21.0/STLink-gdb-server/bin/ST-LINK_gdbserver /usr/local/bin/
```

Verify from a new shell in `/tmp`:

```bash
STM32_Programmer_CLI --version
ST-LINK_gdbserver --help
```

## 4. ST-LINK permissions

The `st-stlink-udev-rules` package installs `/etc/udev/rules.d/49-stlinkv*.rules` granting `plugdev` group access. Add yourself if needed:

```bash
sudo usermod -aG plugdev "$USER"
# log out and back in for group change to take effect
```

If you see `libusb: error [get_usbfs_fd] libusb requires write access to USB device nodes`:

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
# unplug and re-plug the ST-LINK
```

## 5. Smoke test

ST-LINK plugged in, target board powered:

```bash
STM32_Programmer_CLI -l st-link             # should list probe + serial
STM32_Programmer_CLI -c port=SWD -r32 0x08000000 4   # reads first 4 bytes of flash
```

Expected target ID: **0x452** (STM32F72x/F73x family). If you get `Error: No STM32 target found`, check SWD wiring (SWCLK→PA14, SWDIO→PA13, GND, 3V3 ref) and that target voltage reads ~3.3 V.

## 6. Build + flash test

```bash
Firmware/scripts/flash.sh
```

Should report `Download verified successfully` and `Application is running`. If yes — setup is complete; see `firmware.md` for workflow.

## CLion users (optional)

CLion picks up `.idea/debugServers/ST_LINK.xml` automatically — it already points at the installed CubeCLT paths. In **Settings → Toolchains** set:

- C compiler: `/usr/bin/arm-none-eabi-gcc`
- C++ compiler: `/usr/bin/arm-none-eabi-g++`
- Debugger: `/opt/st/stm32cubeclt_1.21.0/GNU-tools-for-STM32/bin/arm-none-eabi-gdb`

For run-configs see `firmware.md`.
