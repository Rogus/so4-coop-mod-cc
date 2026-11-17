"""Compare two v0.6 refs files and report pointer slots whose target character changed.
usage: diff_refs.py sNN_refs.txt sMM_refs.txt [FROM TO]   e.g. ... Edge Sarah
Line format: "<slot> in <class>@<obj>+<off> -> CPlayer<Name>@Battle+<toff>" or "<slot> (<region>) -> ..."."""
import sys, re, collections

LINE = re.compile(r"^([0-9A-F]{16}) (?:in (\S+)@([0-9A-F]{16})\+(0x[0-9a-f]+)|\((\S+)\)) -> CPlayer(\w+)@Battle\+(0x[0-9a-f]+)\s*$")

def load(path):
    out, bad = {}, 0
    for ln in open(path, encoding="latin-1"):
        m = LINE.match(ln.rstrip("\n"))
        if not m:
            bad += 1
            continue
        slot, cls, obj, off, region, chr_, toff = m.groups()
        where = f"{cls}+{off}" if cls else f"({region})"
        out[slot] = (where, chr_, toff, obj)
    return out, bad

a, bad_a = load(sys.argv[1])
b, bad_b = load(sys.argv[2])
want = sys.argv[3:5] if len(sys.argv) >= 5 else None
print(f"A: {len(a)} refs ({bad_a} unparsed)   B: {len(b)} refs ({bad_b} unparsed)")

changed = [(s, a[s], b[s]) for s in sorted(set(a) & set(b)) if a[s][1] != b[s][1] or a[s][2] != b[s][2]]
print(f"\n== same slot, different target: {len(changed)}")
for s, x, y in changed:
    hit = want and x[1] == want[0] and y[1] == want[1]
    print(f"{'**' if hit else '  '} {s} {x[0]:60s} {x[1]}+{x[2]} -> {y[1]}+{y[2]}")

def summary(title, d, keys):
    c = collections.Counter((d[k][0], d[k][1]) for k in keys)
    print(f"\n== {title}: {len(keys)} (grouped by container, target)")
    for (where, chr_), n in c.most_common(40):
        print(f"   {n:4d}  {where:60s} -> {chr_}")

summary("only in A", a, sorted(set(a) - set(b)))
summary("only in B", b, sorted(set(b) - set(a)))
