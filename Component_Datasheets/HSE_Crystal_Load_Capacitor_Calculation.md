# HSE Lastkondensator-Berechnung — ABM8G-12.000MHZ-18-D2Y-T an STM32F411

## Teilenummer-Dekodierung

```
ABM8G - 12.000MHz - 18 - D - 2 - Y - T
         │           │    │   │   │   └─ Tape & Reel (1k/reel)
         │           │    │   │   └───── Freq.-Stabilität: ±30 ppm
         │           │    │   └───────── Freq.-Toleranz: ±20 ppm
         │           │    └───────────── Betriebstemp.: D = -40 … +85 °C
         │           └────────────────── Lastkapazität: 18 pF
         └────────────────────────────── Frequenz: 12,000 MHz
```

---

## Kristall-Parameter (aus Datenblatt)

| Parameter | Wert |
|---|---|
| Frequenz | 12,000 MHz |
| Betriebsmodus | Fundamental |
| Lastkapazität C_L | **18 pF** |
| Shunt-Kapazität C_0 (max) | 5,0 pF |
| Serienersatzwiderstand R_1 (max @ 12 MHz) | **120 Ω** |
| Antriebspegel | 10 … **100 µW** |
| Betriebstemperatur (Option D) | -40 … +85 °C |
| Frequenztoleranz (Option 2) | ±20 ppm |
| Frequenzstabilität (Option Y) | ±30 ppm |
| Gehäuse | 3,2 × 2,5 × 1,0 mm |

---

## Schaltungstopologie — Pierce-Oszillator

```
OSC_IN ──┬── C1 ──┬── GND
         │        │
        [Quarz]  C_stray (parasitär)
         │        │
OSC_OUT ─┴── C2 ──┴── GND
```

Beide Kondensatoren C1 und C2 werden gleich gewählt (C1 = C2 = C_ext).

---

## Formel

Die effektive Lastkapazität, die der Quarz „sieht":

```
C_L = (C1 × C2) / (C1 + C2) + C_stray
```

Mit C1 = C2 = C_ext vereinfacht zu:

```
C_L = C_ext / 2 + C_stray
```

Aufgelöst nach C_ext:

```
C_ext = 2 × (C_L − C_stray)
```

---

## Streukapazität C_stray

| Quelle | Typischer Wert |
|---|---|
| STM32F411 OSC_IN Pin-Kapazität | ~2 pF |
| STM32F411 OSC_OUT Pin-Kapazität | ~2 pF |
| PCB-Leiterbahn / Pad-Streukapazität | ~1–2 pF |
| **Summe C_stray** | **~3 … 5 pF** |

> ST AN2867 gibt ~2 pF pro Pin als typischen Wert für STM32-Familien an.
> Für ein 2-Lagen-PCB wird **C_stray = 4 pF** als realistischer Mittelwert angenommen.

---

## Berechnung

### Nennwert-Berechnung (C_stray = 4 pF)

```
C_ext = 2 × (18 pF − 4 pF)
C_ext = 2 × 14 pF
C_ext = 28 pF
```

Nächster E12-Normwert: **27 pF**

### Verifikation mit C_ext = 27 pF

```
C_L_eff = 27 pF / 2 + 4 pF = 13,5 pF + 4 pF = 17,5 pF  (−2,8 % vom Sollwert)
```

### Sensitivitätsanalyse

| C_stray | C_ext (berechnet) | Nächster E12-Wert | C_L_eff | Abweichung |
|---|---|---|---|---|
| 3 pF | 30 pF | 30 pF | 18,0 pF | 0 % ✓ |
| **4 pF** | **28 pF** | **27 pF** | **17,5 pF** | **−2,8 %** |
| 5 pF | 26 pF | 27 pF | 18,5 pF | +2,8 % |

27 pF liegt im Zentrum der C_stray-Unsicherheit und ist der robusteste Normwert.
Die frequenzmäßige Auswirkung einer ±3 %-Abweichung bei C_L ist kleiner als die
Frequenztoleranz des Quarzes (±20 ppm) — unkritisch.

---

## Anlaufbedingung (Negativwiderstand-Prüfung)

Der Pierce-Oszillator startet sicher, wenn gilt:

```
gm_vorhanden > gm_min = SF × R1 × (ω × C_ext)²
```

Mit Sicherheitsfaktor SF = 5, R1 = 120 Ω, C_ext = 27 pF, ω = 2π × 12 MHz:

```
ω = 2π × 12 × 10⁶ = 75,40 × 10⁶ rad/s

ω × C_ext = 75,40 × 10⁶ × 27 × 10⁻¹² = 2,036 × 10⁻³ S

gm_min = 5 × 120 Ω × (2,036 × 10⁻³)²
       = 600 × 4,144 × 10⁻⁶
       = 2,49 × 10⁻³ A/V
       ≈ 2,5 mA/V
```

Der STM32F411 HSE-Oszillator liefert typisch **10–25 mA/V** (gem. AN2867),
also einen Sicherheitsfaktor von ca. 4–10× über dem Minimum. Anlauf ist gesichert.

> Hinweis: Der R_1 von 120 Ω ist höher als beim vorherigen Kristall (50 Ω),
> daher ist das gm_min hier ~12× größer. Der Anlaufspielraum ist ausreichend,
> aber geringer als bei Quarzen mit niedrigerem ESR.

---

## Antriebspegel-Abschätzung (kritisch)

```
P_drive ≈ ½ × R1 × (ω × C_L × V_pp)²
```

| V_pp | P_drive | Bewertung |
|---|---|---|
| 0,7 V | 43 µW | ✓ innerhalb Spec (10–100 µW) |
| 0,9 V | 90 µW | ✓ knapp innerhalb Spec |
| 1,0 V | **111 µW** | **✗ überschreitet 100 µW max** |
| 1,5 V | 250 µW | ✗ deutlich zu hoch |

**Rechenweg für V_pp = 1,0 V:**
```
ω × C_L = 75,40 × 10⁶ × 18 × 10⁻¹² = 1,357 × 10⁻³ S

P_drive = ½ × 120 × (1,357 × 10⁻³ × 1,0)²
        = 60 × 1,841 × 10⁻⁶
        = 110,5 µW  → überschreitet Max. von 100 µW
```

> **Achtung:** Der maximale Antriebspegel dieses Quarzes beträgt nur 100 µW.
> Der STM32F411-Oszillator kann bei 3,3 V Versorgung Amplituden von 1–2 V
> erzeugen, was den Quarz übersteuert und seine Lebensdauer verkürzt.
>
> **Empfehlung:** Einen Serienwiderstand R_S = 100 … 200 Ω in Serie mit
> OSC_IN schalten (zwischen MCU-Pin und Quarz). Dies dämpft die Amplitude
> und begrenzt den Antriebspegel. Typischer Startwert: **R_S = 100 Ω**.

### Antriebspegel mit R_S = 100 Ω (bei V_pp = 1,0 V)

Der Strom durch den Kristall-Zweig:

```
I_peak = V_pp / (R1 + R_S) × (ω × C_L) / ω  ... (vereinfacht für Abschätzung)
P_crystal ≈ ½ × R1 × I_peak²
          = ½ × R1 / (R1 + R_S)² × (V_pp × ω × C_L)²
          = ½ × 120 / (120 + 100)² × (1,357 × 10⁻³)²
          = 60 / 48400 × 1,841 × 10⁻⁶  ... (inkl. Faktor V²)
```

Vereinfachte Näherung: P skaliert mit (R1 / (R1+R_S)²) × V_pp²:

```
P_drive_mit_RS ≈ P_drive_ohne_RS × R1 / (R1 + R_S)
               = 110,5 µW × 120 / 220
               ≈ 60 µW  ✓
```

Mit R_S = 100 Ω liegt der Antriebspegel bei ~60 µW — sicher im Spec-Fenster.

---

## Ergebnis

| Bauteil | Wert | Toleranz | Dielektrikum |
|---|---|---|---|
| C1 (OSC_IN nach GND) | **27 pF** | ±5 % | C0G/NP0 |
| C2 (OSC_OUT nach GND) | **27 pF** | ±5 % | C0G/NP0 |
| R_S (in Serie mit OSC_IN) | **100 Ω** | ±5 % | — |

**C0G/NP0 zwingend erforderlich** — X5R/X7R haben spannungs- und
temperaturabhängige Kapazität und verschlechtern die Frequenzgenauigkeit.

**Layout-Hinweise:**
- C1 und C2 so nah wie möglich an die Quarz-Pads platzieren
- R_S direkt am OSC_IN-Pin des STM32 (nicht am Quarz-Pin) platzieren
- Leiterbahnen zwischen Quarz und MCU-Pins so kurz wie möglich halten
- Kein Ground-Pour direkt unter den Quarz-Leiterbahnen (erhöht C_stray)
- Quarz und Kondensatoren mit eigenem GND-Polygon abschirmen,
  verbunden am AGND-Pin des STM32

---

## Referenzen

- ABM8G Datenblatt, Abracon, Rev. 08.13.15
- STM32F411xC/xE Datenblatt DS10314, ST Microelectronics
- AN2867 "Oscillator design guide for STM32 microcontrollers", ST
- RM0383 STM32F411 Reference Manual
