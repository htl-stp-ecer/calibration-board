#!/usr/bin/env python3
"""USB-CDC Binär-Frame-Decoder für die Raccoon-Calibration-Board.

Verbindet sich mit /dev/ttyACM* (Default) und parst den binären Stream
vom STM32 (ICM-42688-P + PAA5100).  Synct auf das 0xA5-SYNC-Byte,
verifiziert CRC-8/SMBUS, dekodiert in physikalische Einheiten und gibt
sie zeilenweise auf stdout aus.

Verwendung:
    python3 Firmware/scripts/usb_stream.py
    python3 Firmware/scripts/usb_stream.py --port /dev/ttyACM0 --csv out.csv
    python3 Firmware/scripts/usb_stream.py --stats   # nur Rate + Drops anzeigen

Frame-Format siehe Firmware/app/inc/framing.h.
"""

from __future__ import annotations

import argparse
import csv
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial nicht installiert.  pip install pyserial", file=sys.stderr)
    sys.exit(1)


SYNC          = 0xA5
TYPE_ICM      = 0x01
TYPE_PAA      = 0x02

PAYLOAD_LEN = {TYPE_ICM: 14, TYPE_PAA: 10}
TYPE_NAMES  = {TYPE_ICM: "ICM", TYPE_PAA: "PAA"}

# Scaling exakt aus icm42688p.c — bei Änderung dort hier mitführen.
ICM_GYRO_LSB_PER_DPS = 16.384
ICM_ACCEL_LSB_PER_G  = 8192.0


def crc8_smbus(data: bytes) -> int:
    """CRC-8/SMBUS: poly 0x07, init 0x00. Muss bitexakt mit framing.c sein."""
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
    return c


@dataclass
class IcmSample:
    t_ms: int
    ax_g: float; ay_g: float; az_g: float
    gx_dps: float; gy_dps: float; gz_dps: float
    temp_c: float
    ax_lsb: int; ay_lsb: int; az_lsb: int
    gx_lsb: int; gy_lsb: int; gz_lsb: int

    @classmethod
    def parse(cls, t_ms: int, payload: bytes) -> "IcmSample":
        ax, ay, az, gx, gy, gz, temp = struct.unpack("<7h", payload)
        return cls(
            t_ms=t_ms,
            ax_g=ax / ICM_ACCEL_LSB_PER_G,
            ay_g=ay / ICM_ACCEL_LSB_PER_G,
            az_g=az / ICM_ACCEL_LSB_PER_G,
            gx_dps=gx / ICM_GYRO_LSB_PER_DPS,
            gy_dps=gy / ICM_GYRO_LSB_PER_DPS,
            gz_dps=gz / ICM_GYRO_LSB_PER_DPS,
            temp_c=temp / 132.48 + 25.0,
            ax_lsb=ax, ay_lsb=ay, az_lsb=az,
            gx_lsb=gx, gy_lsb=gy, gz_lsb=gz,
        )


@dataclass
class PaaSample:
    t_ms: int
    dx: int; dy: int
    squal: int
    shutter: int
    motion: int

    @classmethod
    def parse(cls, t_ms: int, payload: bytes) -> "PaaSample":
        dx, dy, squal, sh_hi, sh_lo, motion, _, _ = struct.unpack("<hhBBBBBB", payload)
        return cls(
            t_ms=t_ms,
            dx=dx, dy=dy,
            squal=squal,
            shutter=(sh_hi << 8) | sh_lo,
            motion=motion,
        )


class FrameDecoder:
    """Stream-Decoder mit Resync.  Füttere `feed(bytes)`, hol Frames aus
    dem Generator."""

    def __init__(self):
        self.buf = bytearray()
        self.bytes_seen = 0
        self.frames_ok = 0
        self.crc_errors = 0
        self.resyncs = 0

    def feed(self, chunk: bytes):
        self.buf.extend(chunk)
        self.bytes_seen += len(chunk)

        while True:
            # Auf Sync vorrücken
            sync_idx = self.buf.find(bytes([SYNC]))
            if sync_idx < 0:
                self.buf.clear()
                return
            if sync_idx > 0:
                self.resyncs += 1
                del self.buf[:sync_idx]

            if len(self.buf) < 7:
                return  # warten auf Header

            type_ = self.buf[1]
            length = self.buf[2]

            expected_payload = PAYLOAD_LEN.get(type_)
            if expected_payload is None or length != expected_payload:
                # ungültiger Header → 1 Byte vorrücken, neu synchronisieren
                del self.buf[0]
                self.resyncs += 1
                continue

            total = 7 + length + 1  # hdr + payload + crc
            if len(self.buf) < total:
                return  # warten auf rest des Frames

            crc_expected = crc8_smbus(bytes(self.buf[1:7 + length]))
            crc_got = self.buf[7 + length]
            if crc_expected != crc_got:
                self.crc_errors += 1
                del self.buf[0]  # auf nächsten Sync vorrücken
                continue

            t_ms = struct.unpack_from("<I", self.buf, 3)[0]
            payload = bytes(self.buf[7:7 + length])
            del self.buf[:total]
            self.frames_ok += 1

            if type_ == TYPE_ICM:
                yield IcmSample.parse(t_ms, payload)
            elif type_ == TYPE_PAA:
                yield PaaSample.parse(t_ms, payload)


def autodetect_port() -> str | None:
    for p in list_ports.comports():
        # STMicroelectronics VID = 0x0483, CDC PID = 0x5740
        if (p.vid, p.pid) == (0x0483, 0x5740):
            return p.device
    # Fallback: irgendein ttyACM*
    for p in list_ports.comports():
        if "ACM" in p.device:
            return p.device
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="serielle Schnittstelle (auto-detect wenn weggelassen)")
    ap.add_argument("--baud", type=int, default=921600,
                    help="ignoriert von CDC, dient nur dem pyserial-API (Default 921600)")
    ap.add_argument("--csv", help="zusätzlich CSV-Log schreiben (kombiniert ICM+PAA)")
    ap.add_argument("--stats", action="store_true",
                    help="nur Statistik (Rate, Drops, CRC-Errors) anzeigen, keine Samples")
    ap.add_argument("--max-samples", type=int, default=0,
                    help="nach N erfolgreichen Frames beenden (0 = unendlich)")
    args = ap.parse_args()

    port = args.port or autodetect_port()
    if not port:
        print("Keine /dev/ttyACM* gefunden.  Board angesteckt?  Mit --port erzwingen.",
              file=sys.stderr)
        sys.exit(2)
    print(f"# port = {port}", file=sys.stderr)

    ser = serial.Serial(port, args.baud, timeout=0.1)
    decoder = FrameDecoder()

    csv_writer = None
    csv_fh = None
    if args.csv:
        csv_fh = Path(args.csv).open("w", newline="")
        csv_writer = csv.writer(csv_fh)
        csv_writer.writerow([
            "type", "t_ms",
            "ax_g", "ay_g", "az_g",
            "gx_dps", "gy_dps", "gz_dps",
            "temp_c",
            "paa_dx", "paa_dy", "paa_squal", "paa_shutter", "paa_motion",
        ])

    icm_count = 0
    paa_count = 0
    t_start = time.monotonic()
    t_last_stats = t_start

    try:
        while True:
            chunk = ser.read(4096)
            if chunk:
                for f in decoder.feed(chunk):
                    if isinstance(f, IcmSample):
                        icm_count += 1
                        if not args.stats:
                            print(f"ICM t={f.t_ms:>8} ms  "
                                  f"a=({f.ax_g:+.3f},{f.ay_g:+.3f},{f.az_g:+.3f}) g  "
                                  f"g=({f.gx_dps:+7.2f},{f.gy_dps:+7.2f},{f.gz_dps:+7.2f}) dps  "
                                  f"T={f.temp_c:.2f}°C")
                        if csv_writer:
                            csv_writer.writerow([
                                "ICM", f.t_ms,
                                f.ax_g, f.ay_g, f.az_g,
                                f.gx_dps, f.gy_dps, f.gz_dps,
                                f.temp_c,
                                "", "", "", "", "",
                            ])
                    else:  # PaaSample
                        paa_count += 1
                        if not args.stats:
                            print(f"PAA t={f.t_ms:>8} ms  "
                                  f"d=({f.dx:+5d},{f.dy:+5d})  "
                                  f"squal={f.squal:>3}  shutter={f.shutter:>5}  "
                                  f"motion=0x{f.motion:02x}")
                        if csv_writer:
                            csv_writer.writerow([
                                "PAA", f.t_ms,
                                "", "", "", "", "", "", "",
                                f.dx, f.dy, f.squal, f.shutter, f.motion,
                            ])

                    if args.max_samples and decoder.frames_ok >= args.max_samples:
                        raise KeyboardInterrupt

            now = time.monotonic()
            if now - t_last_stats >= 1.0:
                dt = now - t_start
                print(f"# stats: ICM={icm_count} ({icm_count/dt:6.1f} Hz)  "
                      f"PAA={paa_count} ({paa_count/dt:6.1f} Hz)  "
                      f"frames_ok={decoder.frames_ok}  "
                      f"crc_err={decoder.crc_errors}  "
                      f"resyncs={decoder.resyncs}  "
                      f"bytes={decoder.bytes_seen}",
                      file=sys.stderr)
                t_last_stats = now

    except KeyboardInterrupt:
        pass
    finally:
        dt = time.monotonic() - t_start
        print(f"\n# final: {decoder.frames_ok} frames in {dt:.1f}s  "
              f"(ICM {icm_count/dt:.1f} Hz, PAA {paa_count/dt:.1f} Hz)  "
              f"crc_err={decoder.crc_errors}  resyncs={decoder.resyncs}",
              file=sys.stderr)
        if csv_fh:
            csv_fh.close()
        ser.close()


if __name__ == "__main__":
    main()
