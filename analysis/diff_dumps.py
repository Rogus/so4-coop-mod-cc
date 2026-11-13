"""Compare probe dumps across scans: find the 'controlled character' field."""
import sys, os, re, struct, collections
D = sys.argv[1]
files = os.listdir(D)
def load(scan, cls, addr=None):
    for f in files:
        m = re.match(r"s(\d+)_(.+?)_([0-9A-F]+)\.bin$", f)
        if m and int(m.group(1)) == scan and m.group(2) == cls and (addr is None or m.group(3) == addr):
            return open(os.path.join(D, f), "rb").read(), int(m.group(3), 16)
    return None, None
CH = [("Edge", "CPlayerEdge-Battle", "00000001CD1D01B0"), ("Sarah", "CPlayerSarah-Battle", "00000001CD3301B0"),
      ("Arumat", "CPlayerArumat-Battle", "00000001CD5C01B0"), ("Myuria", "CPlayerMuria-Battle", "00000001CD5F01B0")]
scans = sorted({int(f[1:3]) for f in files})
print("scans:", scans)
chars = {s: [load(s, c, a)[0] for _, c, a in CH] for s in scans}
bases = [int(a, 16) for _, _, a in CH]
size = min(len(b) for s in scans for b in chars[s])
# 1) per-offset: exactly one character differs from the other three, in every battle scan
battle = [s for s in scans if s >= 3]
cands = []
for off in range(0x18, size - 4, 4):
    seq = []
    ok = True
    for s in battle:
        vals = [struct.unpack_from("<I", chars[s][i], off)[0] for i in range(4)]
        c = collections.Counter(vals)
        if len(c) != 2 or 3 not in c.values():
            ok = False; break
        odd = next(i for i, v in enumerate(vals) if c[v] == 1)
        seq.append((CH[odd][0], vals[odd], [v for v in vals if c[v] == 3][0]))
    if ok and len({x[0] for x in seq}) >= 2:
        cands.append((off, seq))
print(f"\n'unique character' fields (battle scans {battle}): {len(cands)}")
for off, seq in cands[:40]:
    print(f"  +0x{off:04x}: " + "  ".join(f"s{s}:{n}({v:#x} vs {o:#x})" for s, (n, v, o) in zip(battle, seq)))
# 2) pointers from the battle manager / pad into characters (also interior pointers)
for cls, addr in (("CBattleMainTask-Battle", "00000001459FA6F0"), ("CBtlPad-Battle", None), ("CTacticsMode-Battle", "00000001D9990B90"), ("CSelectTarget-Battle", "00000001DAE4D510")):
    print(f"\n{cls}: fields pointing into a character")
    rows = collections.defaultdict(dict)
    for s in scans:
        b, a = load(s, cls, addr)
        if b is None: continue
        for off in range(0, len(b) - 8, 8):
            v = struct.unpack_from("<Q", b, off)[0]
            for (n, _, _), base in zip(CH, bases):
                if base <= v < base + 0x4000:
                    rows[off][s] = f"{n}+{v - base:#x}"
    for off in sorted(rows):
        print(f"  +0x{off:04x}: " + "  ".join(f"s{s}={rows[off].get(s, '-')}" for s in scans))

# 3) bit level: a bit set (or clear) in exactly one character, in every battle scan
print("\nbit flags held by exactly one character in every scan 3..7:")
found = 0
for off in range(0x18, size):
    for bit in range(8):
        seq = []
        for s in battle:
            on = [(chars[s][i][off] >> bit) & 1 for i in range(4)]
            if sum(on) == 1: seq.append(CH[on.index(1)][0])
            elif sum(on) == 3: seq.append("!" + CH[on.index(0)][0])
            else: seq = None; break
        if seq and len(set(seq)) >= 2:
            print(f"  +0x{off:04x} bit{bit}: " + " ".join(f"s{s}={n}" for s, n in zip(battle, seq)))
            found += 1
print(found, "bit candidates")
# 4) what changed between consecutive battle scans, per character (count of changed dwords)
print("\nchanged dwords between consecutive scans (per character):")
for a, b in zip(battle, battle[1:]):
    row = []
    for i in range(4):
        n = sum(1 for off in range(0, size, 4) if chars[a][i][off:off+4] != chars[b][i][off:off+4])
        row.append(f"{CH[i][0]}={n}")
    print(f"  s{a}->s{b}: " + " ".join(row))
