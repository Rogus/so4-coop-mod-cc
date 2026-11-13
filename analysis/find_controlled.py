"""Find fields that single out the controlled character across scans.
usage: find_controlled.py DUMPDIR [expected sequence e.g. Edge,Sarah,Sarah,Arumat,Muria,Edge,?]"""
import sys, os, re, struct, collections
D = sys.argv[1]
expect = sys.argv[2].split(",") if len(sys.argv) > 2 else None
by = collections.defaultdict(dict)
for f in os.listdir(D):
    m = re.match(r"s(\d+)_CPlayer(\w+)-Battle_([0-9A-F]+)\.bin$", f)
    if m and os.path.getsize(os.path.join(D, f)) > 0x10000:
        by[int(m.group(1))][m.group(2)] = open(os.path.join(D, f), "rb").read()
scans = sorted(by)
names = sorted(set.intersection(*(set(by[s]) for s in scans)))
size = min(len(by[s][n]) for s in scans for n in names)
print("scans", scans, "characters", names, f"size {size:#x}")
def show(seq): return " ".join(f"s{s}={x}" for s, x in zip(scans, seq))
def match(seq):
    return expect is None or all(e == "?" or e == x.lstrip("!") for e, x in zip(expect, seq))
res = []
# dword level
for off in range(0, size - 4, 4):
    seq = []
    for s in scans:
        vals = [struct.unpack_from("<I", by[s][n], off)[0] for n in names]
        c = collections.Counter(vals)
        uniq = [n for n, v in zip(names, vals) if c[v] == 1]
        if len(uniq) == 1 and len(c) <= 2: seq.append(uniq[0])
        else: seq = None; break
    if seq and len(set(seq)) >= 2 and match(seq): res.append((off, "dword", seq))
# bit level
for off in range(0, size):
    for bit in range(8):
        seq = []
        for s in scans:
            on = [(by[s][n][off] >> bit) & 1 for n in names]
            if sum(on) == 1: seq.append(names[on.index(1)])
            elif sum(on) == len(names) - 1: seq.append("!" + names[on.index(0)])
            else: seq = None; break
        if seq and len(set(x.lstrip("!") for x in seq)) >= 2 and match(seq): res.append((off, f"bit{bit}", seq))
print(len(res), "candidates" + (f" matching {expect}" if expect else ""))
for off, kind, seq in res[:60]:
    print(f"  +0x{off:05x} {kind:5s} {show(seq)}")
