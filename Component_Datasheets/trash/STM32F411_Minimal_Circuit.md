# STM32F411RETx — Minimalbeschaltung (SWD)

> Quelle: DS10314 Rev 8, §3.13–3.15, §6.3.17, Figure 17, Table 56

---

## 1. Stromversorgung

| Pin | Kondensator | Hinweis |
|---|---|---|
| Jedes VDD/VSS-Paar | 100 nF + 1× 4,7 µF (gesamt) | Keramik, so nah wie möglich |
| VDDA / VSSA | 100 nF + 1 µF | Muss gleiches Potential wie VDD haben |
| **VCAP_1** (Pin 49) | **4,7 µF, ESR < 1 Ω** (X5R/X7R) | Ausgangspin des internen Reglers — **nicht weglassen** |

Betriebsspannung: **1,7 – 3,6 V** (für USB min. 3,0 V)

---

## 2. NRST

- Interner Pull-up: 30–50 kΩ (eingebaut)
- Externer Reset-IC **nicht nötig** — internes POR/BOR übernimmt das
- Empfohlen laut Datenblatt (Figure 32): **100 nF** nach GND (filtert parasitäre Resets)
- Optional: Reset-Taster (NO) zwischen NRST und GND

```
NRST ──[100 nF]── GND
NRST ──[Taster]── GND   (optional)
```

---

## 3. BOOT0

Bestimmt den Boot-Modus beim Reset:

| BOOT0-Pegel | Boot-Modus |
|---|---|
| LOW (GND) | Flash — normaler Betrieb |
| HIGH (VDD) | System Memory (UART/USB-Bootloader) |

```
BOOT0 ──[10 kΩ]── GND        ← normaler Betrieb
BOOT0 ──[Taster]── VDD        ← optional, für Bootloader-Zugang
```

> PDR_ON-Pin existiert beim LQFP64 **nicht** — interner Reset ist immer aktiv.

---

## 4. SWD (Serial Wire Debug)

| Signal | GPIO | LQFP64 Pin | SWD-Connector |
|---|---|---|---|
| SWDIO | PA13 | 46 | Pin 2 |
| SWCLK | PA14 | 37 | Pin 4 |
| SWO (optional) | PB3 | 39 | Pin 6 |
| NRST (optional) | NRST | 7 | Pin 10 |

Kein externer Pull-up/Pull-down auf SWD-Leitungen nötig — interne Beschaltung ausreichend.

**Standard 10-pin ARM Cortex Debug Connector (0,05" / 1,27 mm Raster):**

```
VCC  (1) (2) SWDIO
GND  (3) (4) SWCLK
GND  (5) (6) SWO
Key  (7) (8) NC
GND  (9) (10) NRST
```

---

## 5. Takt

- Interner HSI (16 MHz RC) reicht für Betrieb **ohne USB**
- Kein externer Quarz nötig für reine SWD-Programmierung
- Für USB: externer HSE-Quarz zwingend erforderlich (→ USB-Notes)

---

## 6. Minimale Pinliste (LQFP64)

Alle Pins die zwingend verdrahtet werden müssen:

| Pin | Name | Anschluss |
|---|---|---|
| 1, 19, 28, 50, 75 | VDD | 3,3 V |
| 18, 27, 49, 74 | VSS | GND |
| 32 | VDDA | 3,3 V |
| 20 | VSSA | GND |
| 49 | VCAP_1 | 4,7 µF → GND |
| 7 | NRST | 100 nF → GND |
| 60 | BOOT0 | 10 kΩ → GND |
| 46 | PA13 / SWDIO | Debugger |
| 37 | PA14 / SWCLK | Debugger |
