"""Compare two animation memory sweeps (launcher modes "60 FPS + animation
memory sweep" and "30 FPS reference + animation memory sweep").

Each sweep file (out/game/logs/anim_sweep_<rate>_<n>.txt) has one line per
changed float word: "ADDRESS v0 d1 d2" (value at snapshot 1, change to
snapshot 2, change to snapshot 3). Take the three snapshots at the same
real moments in both runs (automatic in the Niki Marina intro, or F9).

Reports words that start and end at the same values in both runs but whose
change happens earlier (or faster) at 60, i.e. values driven by an unpaced
per-update counter or smoothing step.

usage: python scripts/anim_sweep_compare.py anim_sweep_60_1.txt anim_sweep_30_1.txt
"""
import collections
import os
import sys

LOGS = os.path.join(os.path.dirname(__file__), '..', 'out', 'game', 'logs')


def load(name):
    path = name if os.path.exists(name) else os.path.join(LOGS, name)
    data = {}
    with open(path) as f:
        for line in f:
            a, v0, d1, d2 = line.split()
            data[int(a, 16)] = (float(v0), float(d1), float(d2))
    return data


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    rows = []
    for k in set(a) & set(b):
        v0a, d1a, d2a = a[k]
        v0b, d1b, d2b = b[k]
        ta, tb = d1a + d2a, d1b + d2b
        if not (0.05 < abs(ta) < 1e4 and 0.05 < abs(tb) < 1e4):
            continue
        if abs(v0a - v0b) > 0.08 * max(1, abs(v0b)):
            continue  # different start
        if abs(ta - tb) > 0.25 * max(abs(ta), abs(tb)):
            continue  # different total change
        if abs(d1a) + abs(d2a) > 1.3 * abs(ta) or abs(d1b) + abs(d2b) > 1.3 * abs(tb):
            continue  # not monotonic
        early = d1a / ta - d1b / tb  # fraction done by snapshot 2, 60 minus 30
        if early > 0.35:
            rows.append((early, k, a[k], b[k]))
    rows.sort(key=lambda r: r[1])
    pages = collections.Counter(r[1] >> 16 for r in rows)
    print(f'{len(rows)} early words; by 64 KB page:')
    print(' '.join(f'{p << 16:08X}:{n}' for p, n in sorted(pages.items())))
    for early, k, ra, rb in rows[:400]:
        print(f'{k:08X} early {early:.2f}  60: {ra[0]:.4g} {ra[1]:+.4g} {ra[2]:+.4g}'
              f'  30: {rb[0]:.4g} {rb[1]:+.4g} {rb[2]:+.4g}')


if __name__ == '__main__':
    main()
