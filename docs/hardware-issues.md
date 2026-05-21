# Hardware-Issues — Raccoon Calibration Board

Befunde aus dem ersten Firmware-Bringup (BNO086 ansprechen). Alles hier
software-seitig diagnostiziert, ohne Multimeter/Scope, daher Verdachts­liste —
muss mit Elektroniker durchgegangen werden.

## Hintergrund — Software-Status

Firmware steht für Bringup bereit (`Firmware/drivers/bno08x/` + `app/`).
Aktueller Stand nach dem .ioc-Fix (PB2.PinState=GPIO_PIN_SET) und HSI-
Workaround: BNO086 bootet in den App-Mode, `sh2_open` läuft erfolgreich
durch (SHTP-Handshake OK, INT-Leitung funktioniert elektrisch), aber
`sh2_getProdIds` kehrt nicht zurück → siehe Issue 2 (neu gefasst).
Software erkennt den Zustand und blinkt die USER-LED entsprechend:

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

## Issue 2 — BNO086 `sh2_getProdIds` kehrt nicht zurück

**Status:** Reframed nach Bringup-Session 2026-05-21. Vorher als
"INT-Leitung kommt nicht durch" diagnostiziert — das war ein Folge­symptom
von Issue 3 (BNO im Bootloader-Mode). Mit `.ioc`-Fix (PB2.PinState=SET) ist
INT elektrisch nachgewiesen funktional.

**Aktuelle Symptome:**

```
g_bno_stage             = 20      (post sh2_open, in sh2_getProdIds)
g_sh2_open_rc           = 0       (SHTP-Setup erfolgreich)
g_sh2_prodids_rc        = 0xCAFE  (Sentinel — sh2_getProdIds nie zurück)
g_int_after_reset       = 0       (INT korrekt LOW direkt nach Reset)
g_int_ms_to_assert      = 0
g_first_read_buf        = {0,0,…} (saubere Frames, kein Idle-Geklingel)
g_exti4_falling_count   = ~417k bei erstem Halt, danach STILL
```

Heißt: BNO sendet beim Boot den Advertise-Burst (EXTI feuert massiv),
SHTP-Handshake klappt. Sobald die Lib aber `getProdIds` schickt, bekommt
sie keine Antwort mehr (EXTI-Counter steht).

**Plausibelste Restursache: 32 kHz-Clock fehlt (Y401 / R406)**

Die BNO086 braucht die externe 32.768 kHz-Referenz an XIN32, sonst läuft
die SH2-App-Firmware unvollständig — Advertise-Burst kommt aus dem ROM-
Bootloader noch durch, aber sobald die Sensor-/App-Logik selbst antworten
müsste (z. B. auf `getProdIds`), passiert nichts. Klassisches Symptom.

**Was zu prüfen ist:**

| Punkt | Wie | Erwartung |
|---|---|---|
| **R406 (0 Ω) zwischen Y401 Pin 3 und BNO086 Pin 27 (XIN32)** | Lupe + DMM-Durchgang | Best­ückt, < 1 Ω. **Hauptverdacht.** |
| **Y401 Output am Pin 3** | Scope (~32 kHz CMOS-Rechteck) | Wenn tot: Pin 1 (Tri-State EN) prüfen — offen oder VDD. R404 ist DNP, EN sollte intern hochgepullt sein. |
| **Y401 VDD (Pin 4)** | DMM ↔ GND | 3.3 V. |
| **R501–R505 (BNO-Bus-Pullups / Series)** | BOM + Durchgang | Alle best­ückt? Falsche Werte würden zwar nicht erst nach `sh2_open` Probleme machen, aber Vollständigkeit. |
| **U401 VDD + VDDIO (Pin 3 + 28)** | DMM ↔ GND | 3.3 V stabil. |

Wenn der 32k-Clock OK ist und das Symptom bleibt, weiter eingrenzen mit
einer SPI-Trace (Saleae o.ä.) auf MOSI/MISO/CS/CLK — schauen ob der
GetProdIds-Request überhaupt sauber rausgeht und ob der BNO ein NAK/Empty-
Read zurückgibt.

## Issue 3 — `BNO_NBOOT` an PB2 wird im CubeMX-Init auf LOW gezogen ✅ FIXED

**Status:** Behoben in der `.ioc` (`PB2.PinState=GPIO_PIN_SET`) — PB2 ist
ab `MX_GPIO_Init()` HIGH, lange vor `nrst_high()`. Damit sampelt der BNO
beim eigenen Reset-Rising-Edge BOOTN als HIGH und bootet in den App-Mode.
Verifiziert in Bringup-Session 2026-05-21: nach dem Fix lief `sh2_open`
erfolgreich durch, INT-Leitung asserted korrekt.

**Historisches Symptom (jetzt verschwunden):** Vor dem Fix war PB2 nach
Reset LOW (BOOTN active-low → "in Bootloader"). Der BNO sendete dann
nur den Bootloader-Advertise und reagierte nicht auf SH2-Protokoll-Pakete
→ INT blieb dauerhaft HIGH → wurde fälschlich als "INT-Leitung kaputt"
diagnostiziert (siehe Issue 2 alte Fassung).

Schaltplan-Hinweis: R409 (10 kΩ) zieht BNO_NBOOT zwar auf 3V3, das wird
aber von PB2 (push-pull Output) überschrieben — `.ioc`-Default ist
deshalb load-bearing, nicht nur cosmetic.

Backup im `bno08x_init` (`HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET)`
direkt vor `nrst_low()`) bleibt drin, falls das `.ioc`-Setting bei einem
zukünftigen Regen verloren geht.

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

1. **Issue 2 (BNO `getProdIds` hängt)** — verdacht Y401 / 32 kHz-Clock,
   Hardware-Check nötig.
2. **Issue 1 (HSE-Quarz)** — Workaround aktiv (HSI in `main.c`,
   nicht-`.ioc`), sollte trotzdem nachgelötet werden, weil HSE-Genauigkeit
   für Sensor-Timing besser ist als HSI. ⚠️ Workaround geht bei jedem
   CubeMX-Regen verloren — manuell wieder einsetzen oder `.ioc` auf HSI
   umstellen.
3. **Issue 3 (BOOTN)** ✅ behoben in `.ioc` + Firmware-Backup.
4. **Issue 4 (UART-Adapter)** — Quality-of-Life, nicht blockierend.
5. **Issue 5 (USB-C)** — wenn standalone-Betrieb gewünscht.

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
