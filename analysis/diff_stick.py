"""Fields that follow the analog stick (probe v0.21 F6 captures).

usage: diff_stick.py DUMPDIR LOG

LOG lines "=== SCAN #n" + "LABEL: neutral|up|right|down|left" give each capture's direction.
Compared per field (f32, s16, s32), over every file present in all labelled scans: the
Battle:: object dumps (matched by class+address), the 24-class dumps (sNN_<Class>_<addr>.bin) and
sNN_inputglobals.bin (game+0x1059000). Reported:
  Y axis: neutral/left/right ~ 0, up and down non-zero with opposite signs (both passes);
  X axis: neutral/up/down ~ 0, left and right non-zero with opposite signs;
  discrete: integer field with one value per direction, neutral different from all.
Read-only.
"""
import os, re, struct, sys, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from diff_battle import load

DIRS = ("neutral", "up", "right", "down", "left")


def labels(log):
    text = open(log, encoding="latin-1").read()
    text = text[text.rfind("===== session"):] if "===== session" in text else text
    res, cur = {}, None
    for ln in text.splitlines():
        m = re.match(r"=== SCAN #(\d+)", ln)
        if m:
            cur = int(m.group(1))
        m = re.search(r"LABEL: (\w+)", ln)
        if m and cur is not None:
            res[cur] = m.group(1)
    return res


def files(d, s):
    out = {}
    try:
        for (n, a), b in load(d, s).items():
            out[f"{n}@{a}"] = b
    except FileNotFoundError:
        pass
    for f in os.listdir(d):
        m = re.match(rf"s{s:02d}_(.+)\.bin$", f)
        if m and "battle_objects" not in m.group(1) and "globals_" not in m.group(1):
            out[m.group(1)] = open(os.path.join(d, f), "rb").read()
    return out


def sign(v, t):
    return 1 if v > t else (-1 if v < -t else 0)


def main():
    d, log = sys.argv[1], sys.argv[2]
    lab = labels(log)
    scans = [s for s in sorted(lab) if lab[s] in DIRS]
    print("labels:", ", ".join(f"#{s}={lab[s]}" for s in scans))
    data = {s: files(d, s) for s in scans}
    keys = set.intersection(*(set(v) for v in data.values()))
    print(f"{len(keys)} objects/files present in all {len(scans)} labelled scans")
    hits = collections.defaultdict(list)
    for key in sorted(keys):
        blobs = [data[s][key] for s in scans]
        n = min(len(b) for b in blobs)
        for fmt, size, kind in (("<f", 4, "f32"), ("<h", 2, "s16"), ("<i", 4, "s32")):
            for off in range(0, n - size + 1, size):
                vals = [struct.unpack_from(fmt, b, off)[0] for b in blobs]
                if kind == "f32":
                    if any(v != v or abs(v) > 1e6 for v in vals):
                        continue
                    mx = max(abs(v) for v in vals)
                    if mx < 1e-3:
                        continue
                    t = 0.3 if mx <= 2.0 else 0.25 * mx
                else:
                    mx = max(abs(v) for v in vals)
                    if mx < 4:
                        continue
                    t = 0.25 * mx
                c = collections.defaultdict(set)
                for s, v in zip(scans, vals):
                    c[lab[s]].add(sign(v, t))
                if any(len(c[k]) != 1 for k in c):
                    continue
                g = {k: next(iter(c[k])) for k in c}
                if g.get("neutral") != 0:
                    continue
                if g.get("left") == 0 and g.get("right") == 0 and g.get("up", 0) != 0 and g.get("up") == -g.get("down", 0):
                    hits[key].append((off, kind, "Y axis", vals))
                elif g.get("up") == 0 and g.get("down") == 0 and g.get("left", 0) != 0 and g.get("left") == -g.get("right", 0):
                    hits[key].append((off, kind, "X axis", vals))
        for off in range(0, n - 3, 4):                       # discrete per-direction ints
            vals = [struct.unpack_from("<I", b, off)[0] for b in blobs]
            m, ok = {}, True
            for s, v in zip(scans, vals):
                if m.setdefault(lab[s], v) != v:
                    ok = False
                    break
            if ok and len(m) == 5 and len(set(m.values())) == 5 and max(m.values()) < 0x10000:
                hits[key].append((off, "u32", "discrete", vals))
    total = sum(len(v) for v in hits.values())
    print(f"{total} fields in {len(hits)} objects")
    for key, lst in sorted(hits.items(), key=lambda x: (len(x[1]), x[0])):
        print(f"\n== {key}")
        for off, kind, what, vals in lst[:10]:
            fmtv = (lambda v: f"{v:.2f}") if kind == "f32" else (lambda v: str(v))
            print(f"   +{off:#06x} {kind} {what:8s} " + " ".join(f"{lab[s][:1]}={fmtv(v)}" for s, v in zip(scans, vals)))
        if len(lst) > 10:
            print(f"   ... {len(lst) - 10} more")


if __name__ == "__main__":
    main()
