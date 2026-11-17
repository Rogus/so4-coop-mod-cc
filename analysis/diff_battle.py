"""Find fields of any Battle:: object that follow the controlled character (probe v0.12 dumps).

usage: diff_battle.py DUMPDIR LOG [min_distinct_characters=3]

LOG is so4coop_probe.log; the last session's "=== SCAN #n" / "CONTROLLED: CPlayerX" lines give the
controlled character of each scan. An object is matched across scans by class + address. A field
(u8 or aligned u32) is reported when, over every scan with a controlled character:
  same character -> same value, different characters -> different values,
and at least min_distinct characters occur. Read-only.
"""
import re, struct, sys, os, collections


def controlled_by_scan(log):
    text = open(log, encoding="latin-1").read()
    text = text[text.rfind("===== session"):] if "===== session" in text else text
    res, cur = {}, None
    for ln in text.splitlines():
        m = re.match(r"=== SCAN #(\d+)", ln)
        if m:
            cur = int(m.group(1))
            continue
        m = re.search(r"POSITION: (\d+)", ln)                 # v0.15 auto-capture label (status board)
        if m and cur is not None:
            res[cur] = f"pos{m.group(1)}"
            continue
        m = re.search(r"CONTROLLED: CPlayer(\w+)@Battle", ln)  # camera focus (unreliable, see run 14)
        if m and cur is not None and cur not in res:
            res[cur] = m.group(1)
    return res


def load(dumpdir, s):
    idx = os.path.join(dumpdir, f"s{s:02d}_battle_index.txt")
    blob = open(os.path.join(dumpdir, f"s{s:02d}_battle_objects.bin"), "rb").read()
    objs = {}
    for ln in open(idx, encoding="latin-1"):
        p = ln.split()
        if len(p) != 4:
            continue
        name, addr, off, size = p[0], p[1], int(p[2]), int(p[3])
        objs[(name, addr)] = blob[off:off + size]
    return objs


def main():
    dumpdir, log = sys.argv[1], sys.argv[2]
    need = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    ctl = controlled_by_scan(log)
    scans = [s for s in sorted(ctl) if os.path.exists(os.path.join(dumpdir, f"s{s:02d}_battle_index.txt"))]
    print("controlled per scan:", ", ".join(f"#{s}={ctl[s]}" for s in scans))
    if len(set(ctl[s] for s in scans)) < need:
        print(f"only {len(set(ctl[s] for s in scans))} different characters; need {need}")
    data = [load(dumpdir, s) for s in scans]
    common = set(data[0])
    for d in data[1:]:
        common &= set(d)
    print(f"{len(common)} objects present in all {len(scans)} scans")
    who = [ctl[s] for s in scans]
    found = collections.defaultdict(list)
    for key in sorted(common):
        blobs = [d[key] for d in data]
        n = min(len(b) for b in blobs)
        for width, fmt in ((1, "B"), (4, "<I")):
            for off in range(0, n - width + 1, width):
                vals = [struct.unpack_from(fmt, b, off)[0] if width == 4 else b[off] for b in blobs]
                if len(set(vals)) < 2:
                    continue
                m, ok = {}, True
                for w, v in zip(who, vals):
                    if m.setdefault(w, v) != v:
                        ok = False
                        break
                if not ok or len(set(m.values())) != len(m) or len(m) < need:
                    continue
                found[key].append((off, width, m))
    print(f"{sum(len(v) for v in found.values())} candidate fields in {len(found)} objects")
    for (name, addr), lst in sorted(found.items(), key=lambda x: len(x[1])):
        print(f"\n== {name} @{addr}")
        for off, width, m in lst[:12]:
            print(f"   +{off:#06x} {'u8 ' if width == 1 else 'u32'} " + "  ".join(f"{k}={v:#x}" for k, v in m.items()))
        if len(lst) > 12:
            print(f"   ... {len(lst) - 12} more")


if __name__ == "__main__":
    main()
