#!/usr/bin/env python3
"""Run deterministic local tool/state/save checks over the 115200 baud MSH console."""
import argparse
import json
import sys
import time


def command(serial, text, markers, timeout):
    serial.reset_input_buffer()
    serial.write((text + "\n").encode("ascii"))
    serial.flush()
    deadline = time.monotonic() + timeout
    found = {}
    transcript = []
    while time.monotonic() < deadline and len(found) < len(markers):
        line = serial.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        transcript.append(line)
        for marker in markers:
            if line.startswith(marker):
                if " failed " in line:
                    raise RuntimeError("board command failed: " + line)
                found[marker] = line[len(marker):].strip()
    missing = set(markers) - set(found)
    if missing:
        raise RuntimeError("timeout waiting for %s; last output: %s" %
                           (", ".join(sorted(missing)), " | ".join(transcript[-8:])))
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="MSH serial port, for example COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=12.0)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    try:
        import serial
    except ImportError:
        print("pyserial is required: python -m pip install pyserial", file=sys.stderr)
        return 2
    try:
        with serial.Serial(args.port, args.baud, timeout=0.25) as console:
            for round_number in range(1, args.rounds + 1):
                command(console, "sca_demo_reset", ["DEMO_RESET "], args.timeout)
                result = command(console, "sca_demo_check",
                                 ["DEMO_TOOL ", "DEMO_CHECK "], args.timeout)
                tool = json.loads(result["DEMO_TOOL "])
                if (tool.get("ok") is not True or tool.get("source") != "demo" or
                        tool.get("room") != "living_room" or tool.get("on") is not True or
                        tool.get("brightness") != 72):
                    raise RuntimeError("unexpected tool result: %r" % tool)
                print("round %d: reset/tool/state/save passed" % round_number)
    except (OSError, ValueError, RuntimeError, serial.SerialException) as error:
        print("demo check failed: %s" % error, file=sys.stderr)
        return 1
    print("%d/%d demo rounds passed" % (args.rounds, args.rounds))
    return 0


if __name__ == "__main__":
    sys.exit(main())
