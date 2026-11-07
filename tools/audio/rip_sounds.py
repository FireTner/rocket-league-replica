#!/usr/bin/env python3
"""Build a local sound pack from the user's own cooked banks.

Writes gitignored sound_assets/ (or a directory outside the repo). The wavs
are decoded from that install; this tool never copies them into a tracked path.
Playback does not need this tool again. Decoding needs vgmstream-cli.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(os.path.dirname(_HERE))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import wwise_bnk  # noqa: E402

PACK_DIR_NAME = "sound_assets"
MANIFEST = "manifest.txt"
FORMAT = "rl-sound-pack/1"

# Free-play cues. Each name is a Wwise event in one bank. Cosmetic boosts,
# music, and the countdown (no match clock here) are not in this list.
# The stock engine is a REV synth in the game, not a sample, so it is absent.
CUES = (
    ("jump", "pick", ("Play_Car_Movement_Jump", "SFX_Car_Movements.bnk")),
    ("dodge", "pick", ("Play_Car_Movement_Dodge", "SFX_Car_Movements.bnk")),
    ("double_jump", "pick", ("Play_Car_Movement_DoubleJump", "SFX_Car_Movements.bnk")),
    ("pickup", "pick", ("Play_Boost_Pickup_Pad_Local", "SFX_UI_Boost.bnk")),
    ("pickup_big", "pick", ("Play_Boost_Pickup_Pill_Local", "SFX_UI_Boost.bnk")),
    ("land", "pick", ("Play_Impact_Wheel", "SFX_Impacts.bnk")),
    ("ball", "pick", ("Play_Impact_Ball", "SFX_Impacts.bnk")),
    ("slide", "pick", ("Play_Body_Slide", "SFX_Impacts.bnk")),
    ("super", "pick", ("Play_Vehicle_SuperSonic", "SFX_VehicleElements.bnk")),
    ("goal_horn", "pick", ("Play_GoalEvent_Horn", "SFX_GoalEvent.bnk")),
    ("goal_boom", "pick", ("Play_GoalExplosion_NewDefault", "SFX_GoalExplosion_NewDefault.bnk")),
    ("goal_crowd", "pick", ("Play_BG_Crowd_OneShots_Goal", "SFX_BG_Crowd.bnk")),
    ("demo_hit", "pick", ("Play_Demolition_Opponent", "SFX_Impacts.bnk")),
    ("demo_boom", "pick", ("Play_Impact_Car_Explode", "SFX_Impacts.bnk")),
    ("boost", "loop", ("Play_Boost_Standard_Loop", "SFX_Boost_Standard.bnk")),
    ("tires", "loop", ("Play_Tires_Rolling_Loop", "SFX_Car_Tires.bnk")),
    ("whoosh", "loop", ("Play_Car_Movement_InAirWhoosh_Loop", "SFX_VehicleElements.bnk")),
    ("super_loop", "loop", ("Play_Vehicle_SuperSonic_Loop", "SFX_UI_Boost.bnk")),
    ("ball_roll", "loop", ("Play_Ball_Rolling_Loop", "SFX_Ball.bnk")),
    ("ball_fly", "loop", ("Play_Ball_Flying_Loop", "SFX_Ball.bnk")),
    ("crowd", "loop", ("Play_BG_Crowd_01", "SFX_BG_Crowd.bnk")),
)

class RipError(RuntimeError):
    pass


def guard_out_dir(path):
    """Allow sound_assets/ inside the repo, or any directory outside it."""
    real = os.path.realpath(os.path.abspath(os.path.expanduser(path)))
    repo = os.path.realpath(_REPO)
    if real == repo or not real.startswith(repo + os.sep):
        return real
    allowed = os.path.join(repo, PACK_DIR_NAME)
    if real == allowed or real.startswith(allowed + os.sep):
        return real
    raise RipError(
        "refusing to write a sound pack inside the repository at %s "
        "(only <repo>/%s/ is allowed)" % (real, PACK_DIR_NAME))


def _to_mono(path):
    """vgmstream writes multi-channel wavs SDL will not load. Average to mono."""
    with open(path, "rb") as fh:
        blob = fh.read()
    if blob[0:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise RipError("%s is not a wav" % path)
    pos = 12
    fmt = data = None
    data_off = 0
    while pos + 8 <= len(blob):
        kind = blob[pos:pos + 4]
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        chunk = blob[pos + 8:pos + 8 + size]
        if kind == b"fmt ":
            fmt = chunk
        elif kind == b"data":
            data = chunk
        pos += 8 + size + (size & 1)
    if fmt is None or data is None or len(fmt) < 16:
        raise RipError("%s has no fmt/data" % path)
    tag, channels, rate, _br, _align, bits = struct.unpack_from("<HHIIHH", fmt, 0)
    if tag != 1 or bits != 16 or channels < 1:
        raise RipError("%s is not PCM s16 (tag %s channels %s)" % (path, tag, channels))
    if channels == 1:
        return
    import array
    samples = array.array("h")
    samples.frombytes(data[:len(data) - (len(data) % (2 * channels))])
    frames = len(samples) // channels
    mono = array.array("h", [0] * frames)
    for i in range(frames):
        acc = 0
        base = i * channels
        for c in range(channels):
            acc += samples[base + c]
        mono[i] = int(acc / channels)
    out = bytearray()
    out += b"RIFF"
    body = bytearray()
    body += b"WAVE"
    fmt_out = struct.pack("<HHIIHH", 1, 1, rate, rate * 2, 2, 16)
    body += b"fmt " + struct.pack("<I", 16) + fmt_out
    raw = mono.tobytes()
    body += b"data" + struct.pack("<I", len(raw)) + raw
    out += struct.pack("<I", len(body)) + body
    with open(path, "wb") as fh:
        fh.write(out)


def _decode(vgmstream, wem_path, wav_path):
    proc = subprocess.run(
        [vgmstream, "-o", wav_path, wem_path],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.returncode != 0 or not os.path.isfile(wav_path):
        tail = (proc.stdout or "").strip().splitlines()
        detail = tail[-1] if tail else "no output"
        raise RipError("vgmstream failed on %s (%s)" % (os.path.basename(wem_path), detail))


def _banks_used():
    names = []
    for _cue, _mode, spec in CUES:
        if spec[1] not in names:
            names.append(spec[1])
    return names


def rip(cooked, out_dir, vgmstream, leaf_cap=4):
    cooked = os.path.abspath(os.path.expanduser(cooked))
    out_dir = guard_out_dir(out_dir)
    if not os.path.isdir(cooked):
        raise RipError("cooked directory not found: %s" % cooked)
    if not os.path.isfile(vgmstream) and not shutil.which(vgmstream):
        raise RipError(
            "vgmstream-cli not found (%s). Build it, then pass --vgmstream. "
            "The game plays the wav pack without vgmstream." % vgmstream)
    os.makedirs(out_dir, exist_ok=True)
    loaded = {}
    for bank_name in _banks_used():
        path = os.path.join(cooked, bank_name)
        if not os.path.isfile(path):
            raise RipError("missing bank %s" % path)
        loaded[bank_name] = wwise_bnk.Bank(open(path, "rb").read())
    lines = ["# %s" % FORMAT]
    written = 0
    for cue, mode, (event, bank_name) in CUES:
        bank = loaded[bank_name]
        eid = bank.event_id(event)
        if eid is None:
            print("skip %s (%s not in %s)" % (cue, event, bank_name), file=sys.stderr)
            continue
        leaves = bank.leaves(eid, leaf_cap)
        if not leaves:
            print("skip %s (no media)" % cue, file=sys.stderr)
            continue
        paths = []
        for index, mid in enumerate(leaves):
            wav_name = "%s_%d.wav" % (cue, index)
            wav_path = os.path.join(out_dir, wav_name)
            fd, wem_path = tempfile.mkstemp(suffix=".wem")
            os.close(fd)
            try:
                with open(wem_path, "wb") as fh:
                    fh.write(bank.media(mid))
                _decode(vgmstream, wem_path, wav_path)
                _to_mono(wav_path)
            finally:
                if os.path.isfile(wem_path):
                    os.remove(wem_path)
            paths.append(wav_path)
            written += 1
        if mode == "loop" and len(paths) > 1:
            paths.sort(key=lambda p: os.path.getsize(p), reverse=True)
            for extra in paths[1:]:
                os.remove(extra)
                written -= 1
            paths = paths[:1]
        names = []
        for index, src in enumerate(paths):
            final = "%s_%d.wav" % (cue, index)
            final_path = os.path.join(out_dir, final)
            if os.path.abspath(src) != os.path.abspath(final_path):
                os.replace(src, final_path)
            names.append(final)
        lines.append("%s %s %s" % (cue, mode, " ".join(names)))
        print("%s %s %d" % (cue, mode, len(names)), file=sys.stderr)
    if len(lines) == 1:
        raise RipError("no cues were decoded")
    manifest = os.path.join(out_dir, MANIFEST)
    with open(manifest, "w", encoding="ascii") as fh:
        fh.write("\n".join(lines) + "\n")
    return manifest, written


def main(argv):
    default_cooked = os.path.expanduser(
        "~/Games/Heroic/rocketleague/TAGame/CookedPCConsole")
    parser = argparse.ArgumentParser(description="Rip a local free-play sound pack")
    parser.add_argument("--cooked", default=default_cooked,
                        help="CookedPCConsole directory from your own install")
    parser.add_argument("--out", default=os.path.join(_REPO, PACK_DIR_NAME))
    parser.add_argument("--vgmstream", default="vgmstream-cli")
    args = parser.parse_args(argv)
    try:
        manifest, count = rip(args.cooked, args.out, args.vgmstream)
    except RipError as exc:
        print("rip_sounds: %s" % exc, file=sys.stderr)
        return 2
    print("wrote %s (%d wavs)" % (manifest, count))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
