# Hardware-Issues — Raccoon Calibration Board

Befunde aus dem ersten Firmware-Bringup (BNO086 ansprechen). Alles hier
software-seitig diagnostiziert, ohne Multimeter/Scope, daher Verdachts­liste —
muss mit Elektroniker durchgegangen werden.

## Hintergrund — Software-Status

Firmware steht für Bringup bereit (`Firmware/drivers/bno08x/` + `app/`).
Aktueller Stand nach umfassender SW-Diagnose (Session 2026-05-21):
BNO086 wird vom STM32 elektrisch zwar gepingt (INT geht LOW nach Reset),
aber **die BNO treibt MISO nie aktiv** — alle „gelesenen Pakete" sind
Threshold-Artefakte einer floatenden MISO-Leitung. Heißt: SPI-Kommunikation
kommt nicht zustande. Hauptverdacht: **CS-Pfad zur BNO unterbrochen**
(siehe Issue 2 — neu fokussiert). Mehrere SW-Hypothesen wurden eliminiert
(SPI-Mode, Clock-Speed, Pullups, Boot-Delay) — nur Hardware bleibt übrig.

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

## Issue 2 — BNO086 SPI: CS-Pfad zur BNO unterbrochen (Hauptverdacht)

**Status:** Stark reframed nach Bringup-Session 2026-05-21 (zweite Iteration).
Vorher als „INT-Leitung kaputt" → dann „sh2_getProdIds hängt" → jetzt
**„BNO treibt MISO nie aktiv = CS sieht der BNO nie als LOW"** diagnostiziert.
32k-Clock wurde vom Elektroniker gemessen und ist nicht das Problem.

**Aktuelle Symptome:**

```
g_bno_stage             = 20      (in sh2_getProdIds steckend)
g_sh2_open_rc           = 0
g_sh2_prodids_rc        = 0xCAFE  (Sentinel — getProdIds nie zurück)
g_int_after_reset       = 0       (INT geht LOW nach Reset → BNO bootet
                                   elektrisch und WILL senden)
g_reads_with_data       = 0–82    (variabel über Runs — unstable)
g_writes_ok             = 0–1     (eigentlich kein Write durchgekommen)
g_real_pkt_hdr          = {0xF8|0xFC|0xC0|0xE0|0x80, 0, 0, 0}
                                  (über alle Runs: hdr[0] hat nur HIGH-Bits
                                   gesetzt, LOW-Bits immer 0 — kein 0x55, kein
                                   0xAA, kein echter SHTP-Header je gesehen)
g_real_pkt_payload      = {0,0,…} (Payload immer ALL ZEROS)
_sh2.resetComplete      = false   (Lib hat nie einen SH2_RESET-Event gesehen)
```

**Was wir software­seitig eliminiert haben:**

| Hypothese | Test | Ergebnis |
|---|---|---|
| SPI-Mode falsch | Mode 0 statt Mode 3 | Garbage genauso |
| SPI-Clock zu schnell | 750 kHz statt 3 MHz | gleiches Pattern |
| NRST/BOOTN-Sequenz falsch | `.ioc` PinState=SET für PB0 + PB2 | half nicht für das eigentliche Problem |
| Boot-Delay zu kurz/lang | 1 ms vs 500 ms post-INT | kein Unterschied |
| MISO floated → interner Pullup | PA6 PULLUP statt NOPULL | mit Pullup alles 0xFF → **BNO treibt MISO einfach nicht** |
| WAKE-Pin nicht durch | g_writes_ack_int wurde 1× true | WAKE-Leitung funktioniert |
| PS1/PS0 falsch verdrahtet | PCB-Agent Audit | R407+R408 = 10 kΩ Pullups beide auf 3V3, korrekt |
| BNO-Firmware kaputt | — | Advertise-Bursts kommen elektrisch durch (INT toggelt) → BNO lebt |

**Schlüssel-Beobachtung — Pullup-Test ist beweisend:**
Mit STM32-internem Pullup auf MISO (PA6) lasen alle Reads 0xFF (12558×
hdr_ff in 12k Calls). Wenn die BNO aktiv MISO treiben würde, würde
sie als push-pull-Treiber den schwachen internen Pullup überstimmen.
Tut sie nicht. Heißt: **die BNO sieht CS NIE als LOW** und tri-staatet
MISO durchgehend. Ohne Pullup floated die Leitung dann durch den
Threshold und produziert die „high-bits set, low-bits 0"-Pattern, die
das SHTP zufällig als Header mit großer Länge interpretiert →
„Payload" sind dann die nachfolgenden Null-Reads vom Tri-State.

**Was zu prüfen ist (HARDWARE, software ist durch):**

| Punkt | Wie | Erwartung |
|---|---|---|
| **DMM-Durchgang STM32 PA4 ↔ BNO086 Pin 18 (CSN)** | Multimeter Ω | < 1 Ω. **Hauptverdacht — Lötstelle am BNO-CSN-Pad oder Riss in der Leiterbahn.** |
| **R401** (laut BOM als DNP markiert, aber im BNO-Bereich platziert) | Bestückung + Lage prüfen | Falls R401 doch im CS-Pfad sitzt und nicht bestückt → CS unterbrochen. PCB-Agent hat R401 als „in der BNO oszillator/cap region, nicht auf SPI" eingestuft — **aber bitte am echten Schaltplan verifizieren**. |
| **Lupe über U401 Pin 18 (CSN)** | optisch | Kein Lift, keine kalte Lötstelle, kein Solder-Brücken-Lift. |
| **U401 VDD (Pin 3) + VDDIO (Pin 28)** | DMM ↔ GND | 3.3 V stabil. Wenn VDDIO kaputt → I/O-Buffer (inkl. MISO-Driver) tot. |
| **U401 Pin 25 (DSACT) + andere I/O** | Lupe / Durchgang | Allgemeiner Pin-Sanity. |

**Falls CS-Durchgang nachgewiesen OK ist:** weiter mit Logic Analyzer auf
MOSI/MISO/CS/CLK — direkt sehen ob CS wirklich beim BNO-Pin LOW ankommt
beim ersten SPI-Read. Mit Saleae oder ähnlichem.

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
diagnostiziert.

**Sibling-Fix (gleicher Patch):** PB0 (BNO_NRST) hat in der `.ioc` ebenfalls
`PinState=GPIO_PIN_SET` bekommen. Sonst hielt MX_GPIO_Init die BNO bis
`nrst_high()` in `bno08x_init` aktiv im Reset, was zu fragmentierten
Boot-States führte.

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

1. **Issue 2 (BNO SPI: CS-Pfad)** — Hauptverdacht: PA4 ↔ BNO086 Pin 18 (CSN)
   unterbrochen. DMM-Durchgang zwingend bevor weitergebastelt wird. Software
   ist durch (Mode, Clock, Pullups, Boot-Delay, Pin-States, WAKE alle
   eliminiert).
2. **Issue 1 (HSE-Quarz)** — Workaround aktiv (HSI in `main.c`,
   nicht-`.ioc`), sollte trotzdem nachgelötet werden, weil HSE-Genauigkeit
   für Sensor-Timing besser ist als HSI. ⚠️ Workaround geht bei jedem
   CubeMX-Regen verloren — manuell wieder einsetzen oder `.ioc` auf HSI
   umstellen.
3. **Issue 3 (BOOTN + NRST)** ✅ behoben in `.ioc` (PB0+PB2 PinState=SET)
   + Firmware-Backup.
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
