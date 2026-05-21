# Hardware Overview

The calibration board is a carrier PCB for the Raccoon Botball robot, designed to characterise IMU drift and bias prior to deployment. KiCad sources live in `IMU_Extendor_Board/`; BOM in `IMU_Extendor_Board.csv`, production gerbers and pick&place in `IMU_Extendor_Board/production/`.

## MCU

**STM32F722RET6** — Cortex-M7 @ up to 216 MHz, FPU (fpv5-sp-d16), 512 KB Flash, 256 KB RAM, LQFP-64. Powered via 3.3 V (linear regulator on board; datasheet in `Component_Datasheets/3V3_Linear_Regulator_files/`).

### Pin assignments (from `Firmware.ioc`)

| Function | Pins |
|---|---|
| **SWD** | PA13 (SWDIO), PA14 (SWCLK), PB3 (SWO) |
| **UART4** (debug console) | PA0 (TX → adapter RX), PA1 (RX ← adapter TX), 115200 8N1, broken out on J703. Stream with `Firmware/scripts/uart.sh`. Common GND required, **do not** also bridge VCC if board is powered via ST-LINK. |
| **SPI1** | PA5 (SCK), PA6 (MISO), PA7 (MOSI) — IMU #1 candidate bus |
| **SPI2** | PB10 (SCK), PC2 (MISO), PC3 (MOSI) — IMU #2 candidate bus |
| **SPI3** | PC10 (SCK), PC11 (MISO), PC12 (MOSI) — IMU #3 candidate bus |
| **USB OTG FS** | PA9 (VBUS), PA11 (D-), PA12 (D+) |
| **External interrupts** | PA15 (EXTI15), PC4 (EXTI4) — likely IMU INT lines |
| **HSE** | PH0/PH1 (main oscillator) |
| **LSE** | PC14/PC15 (RTC oscillator) |
| **GPIO outputs** | PA2, PA3, PA4, PB0–B2, PB11–B15, PC6–C9, PD2 — CS lines, LEDs, IMU reset/boot |

Three independent SPI buses suggest the board is wired for simultaneous evaluation of multiple IMUs; chip-select assignments are not yet captured in the firmware and need cross-referencing with the KiCad schematic.

## Candidate IMUs

Datasheets present in `Component_Datasheets/` for both options:

- **Bosch BNO080 / BNO085** — sensor fusion built in (quaternion output over SPI/I2C/UART, "Sensor Hub"). Crystal: ECS-327KE 32.768 kHz (datasheet present).
- **TDK InvenSense ICM-42688-P** — 6-axis (3-axis accel + 3-axis gyro), low-noise. Raw output, fusion done on host.

Both can be populated alongside each other on the same carrier (one per SPI bus), enabling side-by-side bias/drift comparison.

## Oscillators

- **ABM8G-12.000MHZ-18-D2Y-T** — 12 MHz HSE for the STM32 (datasheet: `ABM8G-12.000MHZ-...pdf`)
- **ABM8-272-T3** — additional crystal, role TBD from schematic (likely IMU clock)
- **ECS-327KE** — 32.768 kHz, used by BNO080/085 per `external_clock_analysis.md`

The hand-written analysis in `Component_Datasheets/external_clock_analysis.md` documents the clocking strategy — read it before changing any RCC config in `Firmware.ioc`.

## Power

3.3 V linear regulator (see `3V3_Linear_Regulator_files/`); USB OTG FS handles bus-powered operation.

## KiCad project

Open `IMU_Extendor_Board/IMU_Extendor_Board.kicad_pro` in KiCad 7+. Custom symbols and footprints in `Component_Lib/`; design rules in `IMU_Extendor_Board.kicad_dru`. Production drop is `production/` (zipped as `production.zip`).

For agent-friendly inspection of the schematic, install `kicad-cli` (`sudo apt install kicad-cli`) and export to PDF/netlist:

```bash
cd IMU_Extendor_Board
kicad-cli sch export pdf -o schematic.pdf IMU_Extendor_Board.kicad_sch
kicad-cli sch export netlist -o schematic.net IMU_Extendor_Board.kicad_sch
kicad-cli sch export bom -o bom.csv IMU_Extendor_Board.kicad_sch
```

The raw `.kicad_sch` is S-expression text (~21k lines) — parseable but verbose; prefer the exports for any structural analysis.
