#!/usr/bin/env python3
"""Parser check. The bank bytes are synthetic, not a game file."""

import os
import struct
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

import rip_sounds  # noqa: E402
import wwise_bnk  # noqa: E402


def _obj(kind, body):
    return bytes([kind]) + struct.pack("<I", len(body)) + body


def _chunk(kind, payload):
    return kind + struct.pack("<I", len(payload)) + payload


def _bank():
    sound_id, media_id, action_id, event_id, box_id = 0x10, 0x99, 0x20, 0x30, 0x40
    sound = struct.pack("<IIBI", sound_id, 0x00040001, 0, media_id)
    box = struct.pack("<I", box_id) + b"\x00" * 8 + struct.pack("<II", 1, sound_id)
    action = struct.pack("<IHI", action_id, 0x0403, box_id) + b"\x00" * 8
    event = struct.pack("<IBI", event_id, 1, action_id)
    hirc = struct.pack("<I", 4) + _obj(2, sound) + _obj(5, box) + _obj(3, action) + _obj(4, event)
    didx = struct.pack("<III", media_id, 0, 4)
    data = b"RIFF"
    return _chunk(b"BKHD", b"\x00" * 8) + _chunk(b"DIDX", didx) + _chunk(b"DATA", data) + _chunk(b"HIRC", hirc), event_id, media_id


def main():
    blob, event_id, media_id = _bank()
    bank = wwise_bnk.Bank(blob)
    leaves = bank.leaves(event_id)
    if leaves != [media_id]:
        print("FAIL leaves", leaves)
        return 1
    if bank.media(media_id) != b"RIFF":
        print("FAIL media")
        return 1
    if wwise_bnk.fnv1("Play_Car_Movement_Jump") != 0x6A8535EF:
        print("FAIL fnv")
        return 1
    try:
        rip_sounds.guard_out_dir(os.path.join(rip_sounds._REPO, "src", "audio"))
        print("FAIL guard allowed a tracked directory")
        return 1
    except rip_sounds.RipError:
        pass
    outside = tempfile.mkdtemp(prefix="rl-sound-guard-")
    if rip_sounds.guard_out_dir(outside) != os.path.realpath(outside):
        print("FAIL guard rejected an outside directory")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
