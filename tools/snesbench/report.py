"""Compares two benchmark runs (bench.lua output): clocks per tick by game state.
   report.py <a.txt> <b.txt> [first N ticks only]"""
import sys

FRAME = 357368  # master clocks per NTSC frame (262 lines x 1364)
NAMES = ["title", "story", "options", "transition", "round intro", "play", "round skip",
         "pause", "continue", "game over", "ending", "name entry", "hiscores"]
NAMES = {i: n for i, n in enumerate(NAMES)} | {32 + i: n + " +cpu" for i, n in enumerate(NAMES)}  # bench.c CPU_STATE
NAMES[98] = "micro loop"
NAMES[99] = "micro call"


def load(path):
    rows = []
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 4 and p[0].isdigit():
                rows.append((int(p[0]), int(p[1]), int(p[2]), p[3]))
    return rows


a, b = load(sys.argv[1]), load(sys.argv[2])
n = min(len(a), len(b))
if len(sys.argv) > 3:   # compare only the first N ticks (where both builds still agree)
    n = min(n, int(sys.argv[3]))
micro = {}
for i in range(min(len(a), len(b))):
    if a[i][1] in (98, 99):
        m = micro.setdefault(a[i][1], [0, 0, 0])
        m[0] += 1
        m[1] += a[i][2]
        m[2] += b[i][2]
diff = [i for i in range(n) if a[i][3] != b[i][3] or a[i][1] != b[i][1]]
print(f"ticks compared: {n}; checksum differs in {len(diff)}" + (f" (first at tick {diff[0]})" if diff else ""))
for i in diff[:3]:
    print(f"  tick {i}: oam/bg0/bg1 {a[i][3]} vs {b[i][3]}")
print(f"{'state':16} {'ticks':>5}  {'816-tcc':>14}  {'Calypsi':>14}  {'factor':>6}   (average clocks per tick / frames per tick)")
by = {}
for i in range(n):
    s = by.setdefault(a[i][1], [0, 0, 0, 0, 0])
    s[0] += 1
    s[1] += a[i][2]
    s[2] += b[i][2]
    s[3] = max(s[3], a[i][2])
    s[4] = max(s[4], b[i][2])
for st in sorted(by):
    c, ta, tb, ma, mb = by[st]
    name = NAMES.get(st, str(st))
    print(f"{name:16} {c:5}  {ta / c:8.0f} {ta / c / FRAME:5.2f}f  {tb / c:8.0f} {tb / c / FRAME:5.2f}f  {ta / tb:6.2f}"
          f"   worst {ma / FRAME:.2f}f / {mb / FRAME:.2f}f")
ta = sum(r[2] for r in a[:n])
tb = sum(r[2] for r in b[:n])
if 98 in micro and 99 in micro:
    la, lb = micro[98][1] / micro[98][0], micro[98][2] / micro[98][0]
    ca, cb = micro[99][1] / micro[99][0], micro[99][2] / micro[99][0]
    print(f"micro: level_solid_px per call (loop subtracted): {(ca - la) / 1000:.0f} vs {(cb - lb) / 1000:.0f} clocks,"
          f" factor {(ca - la) / (cb - lb):.2f}; loop alone {la / 1000:.0f} vs {lb / 1000:.0f} clocks per pass")
print(f"{'all':16} {n:5}  {ta / n:8.0f} {ta / n / FRAME:5.2f}f  {tb / n:8.0f} {tb / n / FRAME:5.2f}f  {ta / tb:6.2f}")
