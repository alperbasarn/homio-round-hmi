#!/usr/bin/env python3
"""Capture the USB-Serial/JTAG console with the port held open across a reset.

usage: bench_serial.py PORT SECONDS OUTFILE [--reset] [--send TEXT@SECONDS ...]

--reset pulses EN through the USB-Serial/JTAG line-state logic (RTS=1 with DTR=0
holds EN low; equal levels are ignored) without closing the port, so the ROM and
bootloader lines are captured too.  --send writes TEXT + CRLF at t=SECONDS after
the start of the capture (repeatable), for console commands such as help/info.
"""
import argparse
import re
import sys
import time

import serial

ANSI = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("seconds", type=float)
    ap.add_argument("outfile")
    ap.add_argument("--reset", action="store_true")
    ap.add_argument("--send", action="append", default=[])
    args = ap.parse_args()

    sends = []
    for item in args.send:
        text, _, at = item.rpartition("@")
        sends.append((float(at), text))
    sends.sort()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.timeout = 0.05
    # The kernel asserts DTR and RTS together on open; keep them so the open
    # itself never resets the chip.
    ser.dtr = True
    ser.rts = True
    try:
        ser.open()
    except serial.SerialException as exc:
        print(f"open failed: {exc}")
        return 2

    raw = bytearray()
    start = time.monotonic()
    if args.reset:
        ser.dtr = False   # (DTR=0, RTS=1): EN low
        ser.rts = True
        time.sleep(0.15)
        ser.rts = False   # (0, 0): chip runs
        ser.dtr = False
    deadline = start + args.seconds
    while time.monotonic() < deadline:
        while sends and (time.monotonic() - start) >= sends[0][0]:
            _, text = sends.pop(0)
            ser.write((text + "\r\n").encode())
            ser.flush()
        try:
            chunk = ser.read(4096)
        except serial.SerialException as exc:
            print(f"read failed after {time.monotonic() - start:.1f}s: {exc}")
            break
        if chunk:
            raw.extend(chunk)
    try:
        ser.close()
    except serial.SerialException:
        pass

    text = ANSI.sub(b"", bytes(raw)).replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    with open(args.outfile, "wb") as f:
        f.write(text)
    lines = text.split(b"\n")
    lines = [l for l in lines if l.strip()]
    print(f"captured {len(lines)} lines ({len(raw)} bytes) into {args.outfile}")
    for l in lines[:6]:
        print("  first: " + l.decode("utf-8", "replace"))
    for l in lines[-6:]:
        print("  last:  " + l.decode("utf-8", "replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
