#!/usr/bin/env python3
"""Parse binary frames from Flydigi BLE Receiver serial output."""

import argparse
import struct
import sys
import time

FRAME_LEN = 16
HEAD1 = 0xAA
HEAD2 = 0x55
PAYLOAD_LEN = 13

try:
    import serial
except ImportError:
    print("Error: pyserial not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def calc_checksum(frame):
    return sum(frame[2:15]) & 0xFF


def sync_frame(ser):
    """Read bytes until we find 0xAA 0x55 header."""
    while True:
        b = ser.read(1)
        if len(b) == 0:
            return None
        if b[0] == HEAD1:
            b2 = ser.read(1)
            if len(b2) == 1 and b2[0] == HEAD2:
                return True
    return None


def parse_frame(frame):
    """Parse a 16-byte frame. Returns dict or None if invalid."""
    if len(frame) != FRAME_LEN:
        return None
    if frame[0] != HEAD1 or frame[1] != HEAD2:
        return None

    length = frame[2]
    buttons, lx, ly, rx, ry = struct.unpack_from('<HHHHH', frame, 3)
    lt = frame[13]
    rt = frame[14]
    checksum = frame[15]

    expected = calc_checksum(frame)
    valid = (checksum == expected) and (length == PAYLOAD_LEN)

    return {
        'length': length,
        'buttons': buttons,
        'lx': struct.unpack_from('<h', frame, 5)[0],
        'ly': struct.unpack_from('<h', frame, 7)[0],
        'rx': struct.unpack_from('<h', frame, 9)[0],
        'ry': struct.unpack_from('<h', frame, 11)[0],
        'lt': lt,
        'rt': rt,
        'checksum': checksum,
        'valid': valid,
    }


def main():
    parser = argparse.ArgumentParser(description='Parse binary frames from serial')
    parser.add_argument('--port', default='/dev/ttyACM0', help='Serial port')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate')
    parser.add_argument('--count', type=int, default=0, help='Number of frames (0=forever)')
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=2.0)
    print(f"Listening on {args.port} @ {args.baud} baud...", file=sys.stderr)

    count = 0
    try:
        while args.count == 0 or count < args.count:
            if not sync_frame(ser):
                print("Timeout waiting for frame header", file=sys.stderr)
                continue

            rest = ser.read(FRAME_LEN - 2)
            if len(rest) < FRAME_LEN - 2:
                print("Incomplete frame", file=sys.stderr)
                continue

            frame = bytes([HEAD1, HEAD2]) + rest
            data = parse_frame(frame)

            if data is None:
                print(f"Invalid frame: {frame.hex()}", file=sys.stderr)
                continue

            status = "OK" if data['valid'] else "BAD"
            print(f"[{count:4d}] {status} | "
                  f"BTN:{data['buttons']:04x} "
                  f"LX:{data['lx']:+6d} LY:{data['ly']:+6d} "
                  f"RX:{data['rx']:+6d} RY:{data['ry']:+6d} "
                  f"LT:{data['lt']:3d} RT:{data['rt']:3d} "
                  f"CHK:{data['checksum']:02x}")
            count += 1

    except KeyboardInterrupt:
        print(f"\nStopped. {count} frames received.", file=sys.stderr)
    finally:
        ser.close()


if __name__ == '__main__':
    main()
