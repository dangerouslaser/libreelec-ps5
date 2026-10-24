#!/usr/bin/env python3
"""Send command batches to the PS5 HDMI converter through debugfs ps5_hdmi/icc.

Encodes batches the same way as the i2c_* helpers in drivers/ps5/hdmi.c and
prints the raw ICC reply. Run as root.

  ps5hdmi.py [--code N] CMD...
    r REG LEN          read LEN bytes starting at REG       (1.1)
    w REG BYTE...      write bytes starting at REG          (2.2)
    m REG VALUE MASK   read-modify-write                    (2.3)
    d MS               delay                                (3.1)
    ws REG BITS        wait until BITS set                  (3.2)
    wc REG BITS        wait until BITS clear                (3.3)
    raw MAJ MIN BYTE...  any command with a raw body (count = 1)
  ps5hdmi.py --icc SERVICE TYPE BYTE...   send an arbitrary ICC message

Numbers are hex. Commands are separated by ";", e.g.
  ps5hdmi.py --code 1 "m 7228 20 30; w 7204 40; wc 7204 40"
"""
import argparse
import struct
import sys

ICC = "/sys/kernel/debug/ps5_hdmi/icc"
SERVICE_HDMI = 0x10  # ICC_SERVICE_ID_HDMI (include/linux/ps5.h)


def h(x):
    return int(x, 16)


def cmd(major, minor, body):
    # struct i2c_cmd_hdr: major, length (hdr + body), minor, count
    return bytes([major, 4 + len(body), minor, 1]) + body


def encode(text):
    out, count = b"", 0
    for part in filter(None, (p.strip() for p in text.split(";"))):
        op, *a = part.split()
        if op == "r":
            reg = h(a[0])
            out += cmd(1, 1, bytes([h(a[1]), reg >> 8, reg & 0xff]))
        elif op == "w":
            reg, data = h(a[0]), bytes(h(x) for x in a[1:])
            out += cmd(2, 2, bytes([len(data), reg >> 8, reg & 0xff]) + data)
        elif op == "m":
            reg = h(a[0])
            out += cmd(2, 3, bytes([1, reg >> 8, reg & 0xff, h(a[1]), h(a[2])]))
        elif op == "d":
            t = h(a[0])
            out += cmd(3, 1, bytes([0, t & 0xff, t >> 8, 0]))
        elif op in ("ws", "wc"):
            reg = h(a[0])
            out += cmd(3, 2 if op == "ws" else 3, bytes([0, reg >> 8, reg & 0xff, h(a[1])]))
        elif op == "raw":
            out += cmd(h(a[0]), h(a[1]), bytes(h(x) for x in a[2:]))
        else:
            sys.exit(f"unknown command {op!r}")
        count += 1
    return out, count


def send(service, msg_type, payload):
    with open(ICC, "w") as f:
        f.write(f"{service:x} {msg_type:x} " + " ".join(f"{b:x}" for b in payload))
    print(open(ICC).read(), end="")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--code", type=h, default=1, help="batch code, as in i2c_init() (default 1)")
    ap.add_argument("--icc", nargs="+", help="SERVICE TYPE BYTE...")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("cmds", nargs="*")
    args = ap.parse_args()

    if args.icc:
        service, msg_type, *data = (h(x) for x in args.icc)
        payload = bytes(data)
    else:
        body, count = encode(" ".join(args.cmds))
        # struct icc_i2c_msg: code, length (le16, whole message), count
        payload = struct.pack("<BHB", args.code, 4 + len(body), count) + body
        service, msg_type = SERVICE_HDMI, 0
    if args.dry_run:
        print(f"service={service:02x} type={msg_type:04x} " + payload.hex(" "))
        return
    send(service, msg_type, payload)


if __name__ == "__main__":
    main()
