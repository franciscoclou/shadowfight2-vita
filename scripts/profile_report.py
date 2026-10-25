#!/usr/bin/env python3
"""Fold the harness's sampling profile into functions.

    scripts/profile_report.py [profile.txt] [functions.txt] [harness]

profile.txt comes from a harness run with SF_PROFILE=1
(tests/harness/out/profile.txt); functions.txt from
./scripts/ghidra_game.sh functions ghidra/functions.txt. Game samples are
attributed to Ghidra functions (link addresses); host samples inside the
harness binary are named with addr2line when the harness image is around,
the rest (libc, Mesa) are lumped together.
"""
import bisect
import collections
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
prof = Path(sys.argv[1] if len(sys.argv) > 1 else root / "tests/harness/out/profile.txt")
funcs = Path(sys.argv[2] if len(sys.argv) > 2 else root / "ghidra/functions.txt")
harness = Path(sys.argv[3] if len(sys.argv) > 3 else root / "tests/harness/harness")

starts, ends, names = [], [], []
for line in funcs.read_text().splitlines():
    a, b, n = line.split(None, 2)
    starts.append(int(a, 16)); ends.append(int(b, 16)); names.append(n)

game, host = collections.Counter(), collections.Counter()
maps = []   # (start, end, offset, path)
total = 0
for line in prof.read_text().splitlines():
    if line.startswith("#"):
        print(line)
        continue
    if line.startswith("map "):
        f = line[4:].split()
        a, b = (int(x, 16) for x in f[0].split("-"))
        maps.append((a, b, int(f[2], 16), f[5] if len(f) > 5 else "[anon]"))
        continue
    n, kind, addr = line.split()
    n, addr = int(n), int(addr, 16)
    total += n
    if kind == "game":
        i = bisect.bisect_right(starts, addr) - 1
        key = f"{names[i]} @ {starts[i]:#x}" if i >= 0 and addr < ends[i] else f"? {addr:#x}"
        game[key] += n
    else:
        host[addr] += n

def mapping(addr):
    for a, b, off, path in maps:
        if a <= addr < b:
            return a, off, path
    return None, 0, "?"

# Name host addresses inside the harness binary with addr2line (as file
# offsets, so a PIE build works); everything else by its library.
named = collections.Counter()
mine = {}
for addr, n in host.items():
    base, off, path = mapping(addr)
    if path.endswith("/harness"):
        mine[addr - base + off] = mine.get(addr - base + off, 0) + n
    else:
        named["lib " + path.rsplit("/", 1)[-1]] += n
addrs = sorted(mine)
try:
    out = subprocess.run(
        ["docker", "run", "--rm", "-i", "-v", f"{harness.parent}:/h", "shadowfight2-harness:latest",
         "arm-linux-gnueabihf-addr2line", "-f", "-e", f"/h/{harness.name}"],
        input="\n".join(f"{a:#x}" for a in addrs), capture_output=True, text=True, timeout=300).stdout.split("\n")
    for i, a in enumerate(addrs):
        fn = out[2 * i] if 2 * i < len(out) else "??"
        named["shim " + fn] += mine[a]
except Exception as e:  # no docker: keep the totals
    named["shim (unresolved)"] = sum(mine.values())

g = sum(game.values())
print(f"total {total} samples: game {g} ({100 * g / max(total, 1):.1f}%), host {total - g}")
print("\n-- top game functions --")
for k, n in game.most_common(40):
    print(f"{100 * n / total:6.2f}%  {n:7d}  {k}")
print("\n-- top host functions --")
for k, n in named.most_common(25):
    print(f"{100 * n / total:6.2f}%  {n:7d}  {k}")
