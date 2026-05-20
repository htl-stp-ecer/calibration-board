# Hardware-Issues — Raccoon Calibration Board

Befunde aus dem ersten Firmware-Bringup (BNO086 ansprechen). Alles hier
software-seitig diagnostiziert, ohne Multimeter/Scope, daher Verdachts­liste —
muss mit Elektroniker durchgegangen werden.

## Hintergrund — Software-Status

Firmware steht für Bringup bereit (`Firmware/drivers/bno08x/` + `app/`).
Solange die Hardware-Punkte unten nicht abgehakt sind, läuft BNO086 nicht
zuverlässig. Software erkennt den Zustand und blinkt die USER-LED entsprechend:

| LED-Muster | Bedeutung |
|---|---|
| 100 ms an / 100 ms aus (schnell) | `bno08x_init` OK — BNO antwortet auf SHTP, Product IDs gelesen |
| 500 ms an / 500 ms aus (langsam) | SHTP-Setup gescheitert — INT-Leitung oder Clock-Pfad verdächtig |
| 50 ms an / 950 ms aus (Puls) | Unerwarteter Init-Returncode |

GDB-Inspektion: globale Variablen `g_bno_stage`, `g_sh2_open_rc`,
`g_sh2_prodids_rc`, `g_int_after_reset`, `g_int_ms_to_assert`,
`g_first_read_buf` und `g_first_read_skipped` halten Diagnose-Werte fest.
Auslesen z. B. mit `ATTACH=1 Firmware/scripts/gdbserver.sh` +
`arm-none-eabi-gdb -ex "p g_bno_stage"`.

## Issue 1 — HSE-Quarz (Y202, 12 MHz) schwingt nicht an

**Symptom:** Direkt nach Reset bleibt der STM32 in `Error_Handler`, aufgerufen
aus `SystemClock_Config` (main.c:155 — `HAL_RCC_OscConfig` returnt
`HAL_TIMEOUT`). Live ausgelesen am laufenden Chip:

```
RCC_CR (0x40023800) = 0x00017883
  → HSEON = 1, HSERDY = 0   ← Quarz schaltet sich nicht ein
```

**Aktueller Workaround:** SystemClock_Config in `Firmware/Core/Src/main.c`
wurde umgestellt auf HSI (interner 16 MHz RC), PLL × 192 / 8 / 2 → 192 MHz
SYSCLK. Block ist mit Kommentar markiert. Bei nächstem CubeMX-Regenerate muss
das händisch wieder auf HSE umgestellt werden (oder im `.ioc` umkonfiguriert)
sobald die Hardware sauber ist.

**Verdachtspunkte am Board (Schaltplan-Topologie ist Standard-Pierce):**

| Refdes | Wert / Funktion | Was prüfen |
|---|---|---|
| **Y202** | ABM8G-12.000 MHz Crystal | Best­ückt? Löt­stellen warm + ohne Brücken? Mit Lupe ansehen. |
| C210, C211 | je 27 pF (Load-Caps gegen GND, an PH0 bzw. PH1) | Beide drauf? Werte stimmen (27 pF, evtl. 22 pF besser wenn CL=18 pF)? |
| **R203** | 0 Ω series zwischen PH1 und Y202 Pin 3 | Best­ückt? Durchgang messen — wenn unterbrochen, PH1 ist isoliert vom Quarz. |
| 3V3-Versor­gung | XC6210B Regler U101 | Da das USB-C nicht best­ückt ist, kommt VDD nur über ST-LINK. Wenn das mal über USB-C laufen soll, USB-C-Buchse nachbe­stücken. |

Wahrscheinlichste Ursache: kalte/fehlende Löt­stelle an Y202 oder R203.

## Issue 2 — BNO086 INT-Leitung kommt nicht durch (Hauptproblem)

**Symptom:** BNO086 reagiert elektrisch auf SPI (RAW-Probe direkt nach Reset
liefert nicht-trivialer MISO-Output), aber **PC4 (BNO_INT) bleibt durchgehend
HIGH**, auch nach Reset, auch nach mehreren hundert Millisekunden Warten.
Damit funktioniert der SH2/SHTP-Protokoll-Handshake nicht — INT ist die einzige
Flow-Control der CEVA `sh2`-Lib.

Diagnose-Werte nach Init-Versuch:

```
g_int_after_reset    = 0   (INT high = nicht asserted)
g_int_ms_to_assert   = 0   (INT ging in 300 ms Timeout nie low)
g_first_read_buf[0]  = F0, F8, FC oder ähnlich, gefolgt von Nullen
g_first_read_skipped = variabel (0–32) leading 0xFF-Bytes
```

Das Muster — erstes Byte `Fx`, dann nur Nullen — ist konsistent mit MISO,
das gerade von Idle-High (hi-Z + Pullup) auf Aktiv-Low übergeht **während des
ersten Bytes**: obere Bits noch `1`, untere `0`. Keine konsistente SHTP-Länge,
keine valide Channel-ID — kein echtes SH2-Paket. Damit ist das Frame-Sync von
SHTP nie zu erreichen und SHTP läuft nie an.

**Was zu prüfen ist:**

| Punkt | Wie | Erwartung |
|---|---|---|
| **Durchgang PC4 ↔ U401 Pin 14 (H_INT)** | Multimeter Durchgang/Widerstand zwischen STM32-Pin PC4 und BNO086-Pin 14 | < 1 Ω. Wenn unterbrochen: kalte Lötstelle oder Riss in Leiterbahn. |
| **U401 Pin 14 selbst** | Lupe über das BGA-/QFN-Pad | Kein Lift, kein Bridge, nicht ausgetrocknet. |
| **U401 VDD / VDDIO** | Multimeter Pin 3 + Pin 28 ↔ GND | 3.3 V stabil. |
| **U401 Reset (Pin 11) und NBOOT (Pin 4)** | Multimeter im Betrieb messen | Pin 11 = 3.3 V (nicht im Reset), Pin 4 = 3.3 V (nicht im Bootloader). |
| **R406 (0 Ω) zwischen Y401 Pin 3 und BNO086 Pin 27 (XIN32)** | Lupe + Durchgang | Best­ückt und durchgängig. Ohne diesen Widerstand bekommt der BNO keinen 32 kHz-Clock und der SH2-Boot scheitert oder läuft instabil. |
| **Y401 Output am Pin 3** | Scope nötig (~32 kHz CMOS-Rechteck) | Wenn Y401 nicht läuft: Pin 1 (Tri-State EN) prüfen — sollte offen oder VDD sein. R404 ist DNP, also sollte EN floaten und damit per internem Pull-up enabled sein. |
| **R501–R505 (BNO-Bus-Pullups / Series)** | BOM checken | Alle best­ückt? Falsche Werte könnten SPI verzerren. |

Wenn der INT-Pfad elektrisch unterbrochen ist, gibt es keinen robusten
Software-Workaround — die `sh2`-Lib braucht INT zwingend für Frame-Sync.

## Issue 3 — `BNO_NBOOT` an PB2 wird im CubeMX-Init auf LOW gezogen

**Symptom:** CubeMX setzt im Reset-State alle GPIOB-Output-Pins inklusive
PB2 auf LOW. BNO086 Pin 4 (`BOOTN`) ist active-low — LOW heißt "in
Bootloader". Solange wir den BNO nicht aktiv aus dem Bootloader rausholen,
sendet er kein normales Advertise.

**Status:** Im aktuellen Firmware-Stand wird PB2 in `bno08x_init` (HAL-Open)
explizit auf HIGH gezogen. Wenn die `.ioc`-Konfiguration jemals geändert wird,
sollte PB2 dort default-HIGH bekommen, damit der BNO direkt korrekt bootet.

Schaltplan-Hinweis: R409 (10 kΩ) zieht BNO_NBOOT zwar auf 3V3, das wird
aber von PB2 (push-pull Output) überschrieben.

## Issue 4 — UART4 (PA0/PA1) ist nur über externen USB-UART-Adapter nutzbar

**Symptom:** Der eingesetzte ST-LINK V2 hat keinen virtuellen COM-Port.
UART4 ist über J703 (Pin 2 = UART_RX = STM32 PA1, Pin 3 = UART_TX = STM32 PA0)
nach außen geführt, braucht aber einen externen USB-UART-Adapter
(z. B. FTDI/CP2102) für Debug-Output. Solange nichts angeschlossen ist,
fungiert die USER-LED als einziger Statuskanal.

**Empfehlung:** USB-UART-Adapter zulegen (USD ~3) und auf J703 mit
115200 Baud 8N1 mitlaufen lassen. Firmware ist vorbereitet, aktuelle Version
nutzt UART aber nicht aktiv (Output entfernt, weil ohne Adapter zwecklos).

## Issue 5 — USB-C-Buchse fehlt

Aktuell ist die USB-C-Buchse (J101) nicht best­ückt. Stromversorgung läuft
ausschließlich über das ST-LINK-Debug-Cable. Das limitiert max. Strombedarf
und macht das Board nicht standalone betreibbar. Für späteren Einbau ins
Robot­system muss J101 + USBLC6 ESD-Diode nach Schaltplan best­ückt werden.

## Issue 6 (offen) — ICM-42688-P (U501) nicht best­ückt

Soft­ware­seitig keine ICM-Unterstützung implementiert. Erst nachstecken wenn
BNO sauber läuft, sonst verteilt sich die Fehlersuche auf zwei IMUs.

## Status / Reihenfolge

1. **Issue 2 (INT-Leitung BNO)** — größter Showstopper, ohne Fix kein BNO.
2. **Issue 1 (HSE-Quarz)** — Workaround aktiv, sollte trotzdem nachgelötet
   werden, weil HSE-Genauigkeit für Sensor-Timing besser ist als HSI.
3. **Issue 4 (UART-Adapter)** — Quality-of-Life, nicht blockierend.
4. **Issue 5 (USB-C)** — wenn standalone-Betrieb gewünscht.

Sobald Issue 2 behoben ist, sollte die BNO-LED schnell blinken (gleiche Firmware
flashen mit `Firmware/scripts/flash.sh`). Dann Product IDs via GDB lesen:

```bash
ATTACH=1 Firmware/scripts/gdbserver.sh &
arm-none-eabi-gdb Firmware/build/Debug/Firmware.elf \
  -ex "target extended-remote :61234" \
  -ex "monitor halt" \
  -ex "p g_bno_stage" \
  -ex "p g_sh2_prodids_rc"
```

`g_bno_stage == 30` und `g_sh2_prodids_rc == 0` → BNO sauber initialisiert.
