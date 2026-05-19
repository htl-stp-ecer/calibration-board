# External Clock vs. Internal Clock — Impact Analysis
## BNO08X & ICM-42688-P for Dual-IMU Robotics

---

## 1. ICM-42688-P

### 1.1 Clock Source Specifications (from Table 4, section 3.3.2)

| Parameter | Internal RCOSC (CLK_SEL=00) | Internal PLL (CLK_SEL=01, gyro active) | External CLKIN |
|---|---|---|---|
| Initial tolerance at 25 °C | ±3 % | ±1.5 % | — |
| Variation over temp (−40 to +85 °C) | ±3 % | ±2 % | — |
| ODR uncertainty (section 4.10) | **±8 %** (±80 000 ppm) | **±1 %** (±10 000 ppm) | **±50 ppm** (with 50 ppm source) |
| Clock frequency | internal | internal | 31–50 kHz, typ. 32 kHz |

> The datasheet (section 4.10) explicitly states ODR uncertainty can be as high as ±8 % in RCOSC mode and ±1 % in PLL mode. A 50 ppm CLKIN source reduces this to ±50 ppm in both modes.

---

### 1.2 Calculations — ICM-42688-P

**Assumptions:** nominal ODR = 1000 Hz, robotics run of T = 10 s, host uses sensor sample count as time reference (no external timestamping).

#### 1.2.1 Sample count drift

| Clock source | Actual ODR range | Samples in 10 s | Error vs. 10 000 samples |
|---|---|---|---|
| RCOSC (±8 %) | 920–1080 Hz | 9 200–10 800 | **±800 samples** |
| PLL (±1 %) | 990–1010 Hz | 9 900–10 100 | ±100 samples |
| CLKIN 50 ppm | 999.95–1000.05 Hz | 9 999.5–10 000.5 | **±0.5 samples** |

#### 1.2.2 Timing error (dead-reckoning with sample count × nominal dt)

Formula: `time_error = T × clock_error_fraction`

| Clock source | Time error after 10 s | Time error after 60 s |
|---|---|---|
| RCOSC (±8 %) | **±800 ms** | **±4.8 s** |
| PLL (±1 %) | ±100 ms | ±600 ms |
| CLKIN 50 ppm | **±0.5 ms** | **±3 ms** |

#### 1.2.3 Gyroscope angle integration error

Dead-reckoning angle: if the sensor's dt is wrong, integrated heading is wrong by the same fraction.

Formula: `angle_error = total_angle_turned × clock_error_fraction`

**Scenario A — single 360° turn taking 1 s:**

| Clock source | Angle error |
|---|---|
| RCOSC (±8 %) | **±28.8°** |
| PLL (±1 %) | ±3.6° |
| CLKIN 50 ppm | **±0.018°** |

**Scenario B — 90° turn (typical Botball maneuver) at 180 °/s:**

| Clock source | Angle error |
|---|---|
| RCOSC (±8 %) | **±7.2°** |
| PLL (±1 %) | ±0.9° |
| CLKIN 50 ppm | **±0.0045°** |

#### 1.2.4 Sensitivity (scale factor) error from clock

From section 4.10: "Any clock uncertainty directly impacts gyroscope sensitivity at the system level."  
The scale factor error is proportional to ODR error.

| Clock source | Gyro sensitivity error |
|---|---|
| RCOSC | ±8 % |
| PLL | ±1 % |
| CLKIN 50 ppm | ±0.005 % |

#### 1.2.5 ODR Frequency Scaling with CLKIN

When CLKIN ≠ 32 kHz, actual ODR scales linearly (section 12.5):

```
ODR_actual = ODR_nominal × (f_CLKIN_kHz / 32)
```

Example: CLKIN = 32.768 kHz → ODR at nominal 1000 Hz = 1000 × (32.768/32) = **1024 Hz**  
This is predictable and compensatable; internal clock drift is not.

---

### 1.3 Improvement Factor — CLKIN vs. Internal

| Comparison | ODR improvement | Timing improvement |
|---|---|---|
| CLKIN over RCOSC | **1 600×** (80 000 ppm → 50 ppm) | **1 600×** |
| CLKIN over PLL | **200×** (10 000 ppm → 50 ppm) | **200×** |

---

## 2. BNO08X

### 2.1 Clock Source Options (section 1.2.1, Figures 1-7 to 1-10)

| Source | CLKSEL0 | CLKSEL1 | Notes |
|---|---|---|---|
| Crystal 32.768 kHz | 0 or NC | Connected to crystal | Recommended: 50 ppm, 12.5 pF load |
| External clock signal | 1 | 1 | Input on XIN32 (pin 27) |
| Internal oscillator | 1 | 0 or NC | **Not usable with UART-SHTP or UART-RVC** |

> CEVA recommends 50 ppm tolerance for external clock/crystal.

### 2.2 Performance Specifications

The performance table (Figure 6-14, section 6.7) is explicitly stated to apply **when using an external clock or crystal**.  
No separate performance specification is provided for the internal oscillator.

| Sensor output | Metric | Value (external clock/crystal) |
|---|---|---|
| Rotation Vector | Dynamic rotation error | 3.5° |
| Rotation Vector | Static rotation error | 2.0° |
| Gaming Rotation Vector | Dynamic non-heading error | 2.5° |
| Gaming Rotation Vector | Static non-heading error | 1.5° |
| Gaming Rotation Vector | **Heading drift** | **0.5°/min** |
| Geomagnetic Rotation Vector | Dynamic rotation error | 4.5° |
| Gyroscope | Dynamic accuracy | 3.1°/s |
| Accelerometer | Dynamic accuracy | 0.35 m/s² |

### 2.3 Calculations — BNO08X

The BNO08X runs its SH-2 fusion firmware off its reference clock. The clock feeds both sensor sampling timing and the algorithm's internal time reference.

#### 2.3.1 Heading drift over time

With external 50 ppm clock (spec):

| Time | Max heading drift |
|---|---|
| 1 min | 0.5° |
| 2 min (Botball match) | **1.0°** |
| 5 min | 2.5° |

With internal oscillator — no official figure given. Typical RC oscillators drift ±1–3 % from nominal. Using conservative ±1 % as an estimate (consistent with what similar CEVA/Bosch parts specify):

| Time | Estimated heading drift (internal, ±1 %) |
|---|---|
| 1 min | ~30–150× worse, estimated **15–75°/min** |
| 2 min (Botball match) | estimated **30–150°** cumulative |

> These internal clock figures are **estimated** — the BNO08X datasheet does not publish internal oscillator accuracy. The only official performance numbers are for external clock/crystal.

#### 2.3.2 Timestamp accuracy

The BNO08X attaches timestamps to sensor reports (SHTP protocol). With a 50 ppm external clock:

| Scenario | Timestamp error after 10 s | After 60 s |
|---|---|---|
| External 50 ppm | ±0.5 ms | ±3 ms |
| Internal oscillator (est. ±1 %) | ±100 ms | ±600 ms |

---

## 3. Dual-IMU Fusion — Synchronization Impact

Your setup uses both IMUs simultaneously to combine their strengths (BNO08X: stable fused orientation; ICM-42688-P: low-noise raw 6-axis at high rate). Synchronization between them is critical for correct data fusion.

### 3.1 Without shared external clock

Each sensor runs on its own independent clock:
- ICM-42688-P internal RCOSC: up to ±8 % ODR
- BNO08X internal oscillator: unknown, estimated ±1 %
- Relative clock error: up to ±9 % combined

**Sample alignment error between IMUs (no shared clock, RCOSC worst case):**

| ODR | Nominal sample period | Max alignment error | After 1 s |
|---|---|---|---|
| 100 Hz | 10 ms | ±9 % × 10 ms = ±0.9 ms per sample | Samples can differ by up to 90 ms in accumulated timing |
| 1000 Hz | 1 ms | ±0.09 ms per sample | up to 90 ms accumulated |

After just 1 second, the two sensors' sample clocks can be 90 ms apart from each other. Any data fusion algorithm that assumes "simultaneous" samples will be correlating data that is up to 90 ms apart.

### 3.2 With shared external 32.768 kHz clock

Both IMUs locked to the same reference:
- ICM-42688-P: ODR error ±50 ppm
- BNO08X: timestamp error ±50 ppm
- Relative alignment error: < ±100 ppm combined

| ODR | Nominal sample period | Max alignment error (shared clock) |
|---|---|---|
| 100 Hz | 10 ms | ±1 µs |
| 1000 Hz | 1 ms | ±0.1 µs |

Samples from both IMUs are effectively simultaneous.

### 3.3 Synchronization improvement summary

| Scenario | Sample alignment error (1 s) | Improvement |
|---|---|---|
| No external clock (RCOSC) | ~90 ms | — |
| No external clock (PLL) | ~10 ms | 9× |
| Shared 50 ppm CLKIN | **< 0.1 ms** | **900×** |

---

## 4. Verdict for Botball Robotics

| Aspect | Without external clock | With external 32.768 kHz clock |
|---|---|---|
| ICM-42688-P ODR accuracy | ±8 % (RCOSC) or ±1 % (PLL) | **±0.005 %** |
| Dead-reckoning heading error per 90° turn | ±7.2° (RCOSC) | **±0.0045°** |
| BNO08X heading drift (2-min run) | unknown / possibly >30° | **≤1.0°** |
| Dual-IMU sample alignment after 1 s | up to 90 ms offset | **< 0.1 ms** |
| Sensor fusion quality | severe interpolation required | essentially perfect alignment |

**Conclusion:** For a dual-IMU robotics application using dead-reckoning and sensor fusion, an external 32.768 kHz clock (50 ppm) shared between both IMUs makes a very large practical difference:
- ICM-42688-P ODR accuracy improves by **200–1600×**
- Sample alignment between the two IMUs improves by ~**900×**
- BNO08X performance specs are only guaranteed with external clock/crystal
- The BNO08X internal oscillator is explicitly forbidden for UART-SHTP and UART-RVC use cases

The external clock hardware cost is a single oscillator (e.g., SiT1532 or similar 32.768 kHz TCXO) plus routing — a very worthwhile investment given the performance gains.

---

*Sources: BNO08X Datasheet Rev. 1.17 (CEVA/Hillcrest), ICM-42688-P Datasheet Rev. 1.6 (TDK InvenSense)*
