#!/usr/bin/env python3
"""A5 reference INI parser (independent of src/settings/Settings.cpp).

Proves the config/settings.ini contract round-trips with a second,
independent implementation: parse -> assert -> modify -> save -> reparse.

Usage: python3 tools/ini_roundtrip.py   (run from repo root)
Exit 0 = PASS.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "config" / "settings.ini"
TMP = Path("/tmp/py_rt_out.ini")

ALIASES = {"shift": "LeftShift", "ctrl": "LeftCtrl", "control": "LeftCtrl",
           "esc": "Escape", "del": "Delete"}


def canon_key(n: str) -> str:
    n = n.strip()
    l = n.lower()
    if l in ALIASES:
        return ALIASES[l]
    if l in ("up", "down", "left", "right"):
        return l.capitalize()
    if len(n) == 1 and n.isalpha():
        return n.upper()
    return n


def parse(text: str):
    ini, section = {}, ""
    for ln, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line[0] in ";#":
            continue
        if line.startswith("["):
            assert line.endswith("]"), f"line {ln}: bad section"
            section = line[1:-1].strip().lower()
            ini.setdefault(section, {})
            continue
        assert "=" in line and section, f"line {ln}: bad pair"
        k, v = line.split("=", 1)
        v = re.split(r"\s[;#]", v.strip(), maxsplit=1)[0].strip()
        ini[section][k.strip().lower()] = v
    return ini


def parse_axis(raw: str):
    halves = [h for h in raw.split("/")[:2]]
    out = []
    for h in halves:
        out.append([canon_key(k) for k in h.split(",") if k.strip()])
    while len(out) < 2:
        out.append([])
    return out


def save(ini, path: Path):
    with open(path, "w") as f:
        f.write("; written by tools/ini_roundtrip.py\n")
        for sec, keys in ini.items():
            f.write(f"\n[{sec}]\n")
            for k, v in keys.items():
                f.write(f"{k} = {v}\n")


def check(cond, msg, fails):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        fails.append(msg)


def main():
    fails = []
    ini = parse(SRC.read_text())
    print("[1] parse shipped config/settings.ini (python impl)")
    check(ini.get("version", {}).get("schema") == "1", "schema == 1", fails)
    check(ini["camera"]["fov"] == "110", "camera.fov == 110", fails)
    check(ini["camera"]["ballcam"] == "true", "ballcam == true", fails)
    th = parse_axis(ini["input"]["throttle"])
    check(th == [["W", "Up"], ["S", "Down"]], f"throttle axis {th}", fails)
    check([canon_key(k) for k in ini["input"]["boost"].split(",")] == ["LeftShift"],
          "boost canonicalises to LeftShift", fails)
    check(ini["physics"]["tick"] == "120", "tick == 120", fails)
    check(ini["boost"]["unlimited"] == "false", "unlimited == false", fails)
    check(ini["game"]["kickoff_index"] == "0", "kickoff_index == 0", fails)
    # all 5 kickoff bindings present
    check(all(f"kickoff_{i}" in ini["input"] for i in range(5)), "kickoff_0..4 present", fails)

    print("[2] modify, save, reparse")
    ini["camera"]["fov"] = "100"
    ini["boost"]["unlimited"] = "true"
    ini["input"]["throttle"] = "T/S,Down"
    save(ini, TMP)
    ini2 = parse(TMP.read_text())
    check(ini2["camera"]["fov"] == "100", "fov mutation survived", fails)
    check(ini2["boost"]["unlimited"] == "true", "unlimited mutation survived", fails)
    check(parse_axis(ini2["input"]["throttle"]) == [["T"], ["S", "Down"]],
          "throttle mutation survived", fails)
    check(ini2["camera"]["distance"] == "270", "untouched key intact", fails)

    print("PY ROUNDTRIP PASS" if not fails else f"PY ROUNDTRIP FAIL ({len(fails)})")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
