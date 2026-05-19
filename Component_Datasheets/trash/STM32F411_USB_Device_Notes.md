# STM32F411RETx — USB Full-Speed Device: Hardware Notes

> **Sources:** STM32F411xC/xE Datasheet DS10314 Rev 8 (Jan 2024) · ST AN4879 (USB hardware & PCB guidelines) · USB Type-C Spec Rev 2.0 · ST Community / Hackaday / web cross-check.

---

## 1. USB Peripheral Overview

The STM32F411RE embeds a **USB OTG Full-Speed (OTG_FS)** peripheral with an **on-chip PHY (transceiver)** — no external USB transceiver IC is required.

| Feature | Value |
|---|---|
| Standard | USB 2.0 Full-Speed (12 Mbit/s), OTG 1.0 |
| PHY | On-chip integrated — no external component |
| Endpoints | 4 bidirectional |
| FIFO | 320 × 35 bit, dynamically configurable |
| Suspend / Resume | Supported |
| D+ pull-up | Internal ≈ 1.5–2.1 kΩ, software-controlled |
| D± pull-down | Internal ≈ 17–24 kΩ, hardware-controlled |
| Required clock | **48 MHz from PLL — HSE crystal mandatory** |

> **Datasheet §3.27:** *"HNP/SNP/IP inside (no need for any external resistor)"* — the internal D+ pull-up for Full-Speed device enumeration is built into the chip. **No external 1.5 kΩ pull-up on D+ is needed.**

---

## 2. MCU Pin Connections

All USB signals use **Alternate Function 10 (AF10 = OTG1_FS)**.

| Signal | GPIO | LQFP64 pin | Direction | Notes |
|---|---|---|---|---|
| USB_FS_DM | PA11 | 44 | Bidir. | D− data line |
| USB_FS_DP | PA12 | 45 | Bidir. | D+ data line |
| OTG_FS_VBUS | PA9 | 42 | Input | VBUS sense — see §5 |
| USB_FS_ID | PA10 | 43 | Input | OTG ID pin — leave unconnected for device-only |
| USB_FS_SOF | PA8 | 41 | Output | Optional: Start-of-Frame pulse |

> For **device-only** operation leave PA10 (USB_FS_ID) **unconnected**. The OTG controller treats an open/high ID pin as device mode.

---

## 3. External Components

### 3.1 USB Data Lines — Series Resistors (optional)

**Per ST AN4879:** *"The matching output impedance is already embedded in the pad transceiver and in line with the USB specification."* — external series resistors on D+/D− are **not required** on the STM32F411.

However, 22 Ω series resistors are still commonly added on compact boards as an extra EMI precaution, especially when USB traces exceed 30–40 mm. They will not harm the design if added.

| Component | Value | Status |
|---|---|---|
| R_DM (PA11, D−) | 22 Ω | Optional — EMI damping only |
| R_DP (PA12, D+) | 22 Ω | Optional — EMI damping only |

> **Do not add an external 1.5 kΩ pull-up on D+.** The internal pull-up is calibrated; an extra parallel resistor lowers the combined value and can break enumeration.

### 3.2 ESD Protection (strongly recommended)

Place as close as possible to the USB connector.

| Component | Suggested Part | Protects |
|---|---|---|
| D+/D− ESD clamp | USBLC6-2SC6 (SOT-23-6) | D+, D− signal lines |
| VBUS TVS diode | ESDA7P60-1U1M or SMBJ5.0A | VBUS overvoltage |

The USBLC6-2SC6 is a rail-to-rail steering clamp — it handles IEC 61000-4-2 Level 4 ESD events on both data lines with very low capacitance (~1 pF) so it does not distort the USB signal.

### 3.3 Crystal / HSE Oscillator — **Mandatory for USB**

The USB clock must be **exactly 48 MHz**, derived from the PLL locked to the external crystal. The internal HSI RC oscillator is not stable enough (USB 2.0 requires ≤ 500 ppm clock accuracy; the HSI is ±1%).

| Component | Typical Value | Notes |
|---|---|---|
| HSE crystal | 8 MHz or 12 MHz | Both easily produce 48 MHz via PLL |
| Crystal load capacitors | 2 × 18–22 pF | Match crystal C_L spec exactly |
| Series resistor (optional) | 0–470 Ω | Only if crystal datasheet recommends it |

**PLL example — 8 MHz HSE → 48 MHz USB clock:**
```
PLLM=4, PLLN=192, PLLP=2  →  SYSCLK = 96 MHz
PLLQ=4                     →  USB clock = (192/4) × (8/4) = 48 MHz ✓
```
CubeMX calculates this automatically when USB_OTG_FS is enabled.

### 3.4 Power Supply Decoupling

Required per datasheet Figure 17 (§6.1.6):

| Pin(s) | Capacitor | Notes |
|---|---|---|
| Each VDD / VSS pair | 100 nF ceramic + one 4.7 µF ceramic on board | Place as close to pins as possible |
| VCAP_1 (pin 49) | **2.2–4.7 µF**, ESR < 1 Ω (X5R/X7R) | Internal regulator output — **must not be omitted** |
| VDDA / VSSA | 1 µF + 100 nF ceramic | Analog supply |

> The LQFP64 has **one VCAP pin** only. Use a single 4.7 µF (preferred) or 2.2 µF low-ESR ceramic. Do **not** connect VCAP to VDD — it is the regulator's output, not an input.

### 3.5 VBUS Power Supply

For a **bus-powered device** (board powered from USB host):

```
USB-C VBUS (5 V)
    │
    ├── [Ferrite bead, e.g. Murata BLM21AG121SN1D, 120 Ω @ 100 MHz]
    │       └── 2.2 µF ceramic to GND  (VBUS filtering, near connector)
    │
    └── [3.3 V LDO, e.g. AMS1117-3.3 or LD39015]
            ├── 10 µF + 100 nF ceramic on output
            └── → VDD, VDDA of STM32
```

- LDO must be rated ≥ 500 mA (USB 2.0 high-power device limit)
- Total VBUS capacitance (including LDO input cap) should stay **below 4.7 µF** to avoid triggering the host's short-circuit detection during hot-plug

---

## 4. USB Electrical Specifications (Datasheet Table 63)

| Symbol | Parameter | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| V_DD | USB OTG FS operating voltage | — | 3.0 | 3.6 | V |
| R_PD (DM/DP) | Internal pull-down on PA11/PA12 | 17 | 21 | 24 | kΩ |
| R_PU (DM/DP) | Internal D+ pull-up on PA12 | 1.5 | 1.8 | 2.1 | kΩ |
| V_OL | Output low (R_L = 1.5 kΩ to 3.6 V) | — | — | 0.3 | V |
| V_OH | Output high (R_L = 15 kΩ to GND) | 2.8 | — | 3.6 | V |
| t_STARTUP | PHY startup time | — | — | 1 | µs |
| t_r / t_f | Rise / fall time (C_L = 50 pF) | 4 | — | 20 | ns |

> Full USB FS specs guaranteed at VDD ≥ 3.0 V. Functional down to 2.7 V but D+/D− characteristics are degraded between 2.7–3.0 V (DS Table 15, note 6).

---

## 5. VBUS Sensing (PA9)

PA9 is an **FT-type (5 V-tolerant) GPIO**. Its absolute maximum input is VDD + 4.0 V, so at VDD = 3.3 V the limit is 7.3 V — 5 V is safe when the MCU is powered.

**Problem with direct connection:** When the MCU is unpowered (VDD = 0 V) and VBUS = 5 V is present, the limit drops to 0 + 4.0 = 4.0 V — meaning 5 V directly on PA9 violates the absolute maximum rating.

### Recommended: Resistor divider (safe in all states)

```
VBUS (5 V) ──[4.7 kΩ]──┬── PA9 (floating input, no pull)
                        │
                      [10 kΩ]
                        │
                        GND
```

- MCU powered (VDD = 3.3 V), VBUS = 5 V: V_PA9 ≈ 3.4 V — detects HIGH ✓, well below 7.3 V limit ✓
- MCU unpowered (VDD = 0 V), VBUS = 5 V: V_PA9 ≈ 3.4 V — safely below 4.0 V limit ✓

> **Datasheet Table 63 Note:** *"When VBUS sensing feature is enabled, PA9 should be left at their default state (floating input), not as alternate function."*

### Alternative: Disable VBUS sensing entirely

If the board is always bus-powered, you can skip the PA9 connection entirely and set `OTG_GCCFG_NOVBUSSENS = 1` in firmware (or the equivalent HAL setting). The device enumerates as soon as it powers up.

---

## 6. USB Type-C Connector — CC1, CC2, SBU1, SBU2

### 6.1 CC1 and CC2 — Required Rd Termination

CC pins carry the Configuration Channel signal. A host (DFP) applies a pull-up (Rp) to each CC pin. When it detects a pull-down (Rd) on a CC pin it knows a device is connected and enables VBUS.

**For a device with no Power Delivery:**

| Pin | Connection | Component |
|---|---|---|
| CC1 | **5.1 kΩ → GND** | 5.1 kΩ ±10 %, 1/16 W |
| CC2 | **5.1 kΩ → GND** | 5.1 kΩ ±10 %, 1/16 W |

The 5.1 kΩ value is the **Rd** resistance defined in USB Type-C Spec Table 4-36.

**Why both CC pins need independent resistors:**
1. USB-C is reversible — the cable can be plugged in either orientation
2. The host detects cable orientation by which CC pin sees the Rd pull-down
3. The 5.1 kΩ also signals to the host how much current the device wants (5.1 kΩ = default USB current, up to 900 mA)

> **Critical:** Do **not** share one resistor by connecting CC1 to CC2 and using a single pull-down. If an e-marked (active) cable is used, its internal Ra resistor (~1 kΩ) appears in parallel, reducing the combined resistance below the spec threshold, and some hosts will not recognise the device. The Raspberry Pi 4 had exactly this bug in its first revision.

Without the 5.1 kΩ Rd resistors, the host will never enable VBUS and the device will not power up.

> To add USB Power Delivery later: replace both 5.1 kΩ resistors with a PD controller IC (e.g. FUSB302, STUSB4500) connected to CC1 and CC2.

### 6.2 SBU1 and SBU2 — Leave Unconnected

SBU (Sideband Use) pins are only used for alternate modes (DisplayPort, Thunderbolt, UART accessories). For a plain USB 2.0 device:

| Pin | Treatment |
|---|---|
| SBU1 (A8) | Leave **unconnected** |
| SBU2 (B8) | Leave **unconnected** |

---

## 7. USB Type-C Receptacle — Complete Pin Summary

| USB-C Pin(s) | Signal | Connection |
|---|---|---|
| A1, B12 | GND | Board GND |
| A4, B9 | VBUS | 5 V rail → ferrite → LDO |
| A5 | CC1 | 5.1 kΩ → GND |
| B5 | CC2 | 5.1 kΩ → GND |
| A6, B6 | D+ | Tie together → (22 Ω opt.) → PA12 |
| A7, B7 | D− | Tie together → (22 Ω opt.) → PA11 |
| A8 | SBU1 | Leave unconnected |
| B8 | SBU2 | Leave unconnected |
| A2, A3, B10, B11 | TX±/RX± | Leave unconnected (USB 3.x SuperSpeed, not used) |

> **Both D+ pins (A6, B6) and both D− pins (A7, B7) must be tied together.** USB 2.0 differential pairs appear on both sides of the connector — which side is active depends on cable orientation. Shorting each pair together ensures the device works regardless of how the cable is inserted.

---

## 8. Schematic Checklist

```
STM32F411RE (LQFP64)                USB-C Receptacle
                                  ┌──────────────────┐
VDD 3.3V ◄─ LDO ◄─ [Ferrite] ◄── VBUS A4/B9 (5 V)  │
                                  │                  │
                            [2.2 µF to GND near conn.]
                                  │                  │
PA11 ──[22 Ω, opt.]────────────── D− A7+B7          │
PA12 ──[22 Ω, opt.]────────────── D+ A6+B6          │
                                  │                  │
           USBLC6-2SC6 ─── between D+, D−, and GND  │ ← ESD
           SMBJ5.0A    ─── VBUS to GND               │ ← TVS
                                  │                  │
PA9 ──[4.7 kΩ]──┬── (sense)       │                  │
               [10 kΩ]           │                  │
                │                │                  │
               GND ──────────── GND A1/B12          │
                                  │                  │
GND ──[5.1 kΩ]────────────────── CC1 A5             │
GND ──[5.1 kΩ]────────────────── CC2 B5             │
                                  │                  │
          (unconnected) ───────── SBU1 A8            │
          (unconnected) ───────── SBU2 B8            │
                                  └──────────────────┘

VCAP_1 (pin 49) ──[4.7 µF, ESR < 1 Ω, X5R]── GND
VDDA (pin 32)   ──[1 µF + 100 nF]──────────── GND
Each VDD pin    ──[100 nF + one 4.7 µF total]─ GND

HSE Crystal ── OSC_IN (PH0 or pin 5) / OSC_OUT (PH1 or pin 6)
  Each crystal pin ── [18–22 pF ceramic] ── GND
```

---

## 9. Firmware / CubeMX Setup

1. **Enable USB_OTG_FS** in Device mode
2. **Clock tree:** Set HSE as PLL source; configure PLLQ to output 48 MHz for USB
3. **PA9:** Leave as floating input (no pull), or disable VBUS sensing with `OTG_GCCFG_NOVBUSSENS = 1`
4. **PA10 (ID):** Leave unconfigured / as floating input — the peripheral reads it HIGH = device mode
5. **USB middleware:** STM32 USB Device Library (CDC, HID, MSC, etc.) via CubeMX middleware tab

---

## 10. Common Mistakes

| Mistake | Consequence |
|---|---|
| Forgetting 5.1 kΩ on CC1 / CC2 | Host never enables VBUS — device has no power |
| Sharing one resistor between CC1 and CC2 | Fails with e-marked cables (Raspberry Pi 4 bug) |
| Using HSI instead of HSE for USB clock | Intermittent disconnects and failed enumeration |
| Connecting VBUS directly to PA9 when MCU is unpowered | PA9 absolute max exceeded (5 V > VDD+4 V = 4 V) |
| Omitting VCAP_1 capacitor | Internal regulator unstable — unpredictable MCU behaviour |
| Not tying both D+/D− pairs of USB-C together | Device only enumerates in one cable orientation |
| Adding external 1.5 kΩ D+ pull-up | Lowers combined resistance → enumeration failure |
| VBUS capacitance > 4.7 µF total | Host short-circuit protection may trip on hot-plug |

---

## 11. References

- STM32F411xC/xE Datasheet DS10314 Rev 8 (Jan 2024) — §3.27, Table 63, Figure 17
- ST AN4879 — *Introduction to USB hardware and PCB guidelines using STM32 MCUs*
- USB Type-C Specification Rev 2.0 — Table 4-36 (Rd/Rp/Ra values)
- ST RM0383 — STM32F411 Reference Manual (OTG_FS registers)
- [Hackaday — All About USB-C: Resistors and Emarkers](https://hackaday.com/2023/01/04/all-about-usb-c-resistors-and-emarkers/)
- ST Community — Management of VBUS sensing for USB device design
