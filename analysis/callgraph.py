"""Who calls a function, labelled by class (offline).

usage: callgraph.py EXE CODE_DUMP TARGET_RVA [depth=3]

1. RTTI of every class in the image (type descriptors ".?AV/.?AU...@@" -> primary complete-object
   locators -> vtables) -> map: function RVA -> "Class::vfN".
2. One pass over the executable sections: every direct call (E8 rel32) -> map target -> call sites.
3. For each direct caller of TARGET: its label if it is a virtual method, else walk up its own
   direct callers (breadth first, up to `depth` levels) and print the first labelled ancestors.
Also prints the immediate argument in ecx when the call is preceded by mov ecx,imm / xor ecx,ecx.
Read-only.
"""
import os, re, struct, sys, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vtable import build_image
from static_xref import Image, Functions

BASE = 0x140000000
EXEC = ((0x1000, 0x1000 + 0x90b000), (0x13a2000, 0x13a2000 + 0x4487932))


def in_exec(r):
    return any(a <= r < b for a, b in EXEC)


def rtti_methods(img):
    tds = {}
    for m in re.finditer(rb"\.\?A[VU]([^\0]{1,200}?@@)\0", img):
        p = m.start()
        if p >= 0x10 and BASE <= struct.unpack_from("<Q", img, p - 0x10)[0] < BASE + len(img):
            tds[p - 0x10] = m.group(1).decode("latin-1")
    cols = {}
    pat = b"\x01\x00\x00\x00\x00\x00\x00\x00"
    i = img.find(pat)
    while i >= 0:
        if i % 4 == 0 and i + 24 <= len(img):
            c = struct.unpack_from("<6I", img, i)
            if c[5] == i and c[3] in tds:
                cols[i] = tds[c[3]]
        i = img.find(pat, i + 1)
    methods = collections.defaultdict(list)
    nvt = 0
    for crva, name in cols.items():
        q = struct.pack("<Q", BASE + crva)
        j = img.find(q)
        while j >= 0:
            if j % 8 == 0:
                vt = j + 8
                k = 0
                while vt + 8 * k + 8 <= len(img):
                    r = struct.unpack_from("<Q", img, vt + 8 * k)[0] - BASE
                    if not in_exec(r):
                        break
                    methods[r].append(f"{name}::vf{k}")
                    k += 1
                nvt += 1
            j = img.find(q, j + 1)
    return methods, len(tds), nvt


def call_index(img):
    calls = collections.defaultdict(list)
    for lo, hi in EXEC:
        i = img.find(b"\xe8", lo)
        while 0 <= i < hi - 5:
            t = i + 5 + struct.unpack_from("<i", img, i + 1)[0]
            if in_exec(t):
                calls[t].append(i)
            i = img.find(b"\xe8", i + 1)
    return calls


def arg_ecx(img, at):
    back = img[at - 12:at]
    m = back.rfind(b"\xb9")
    if m >= 0 and m + 5 <= 12:
        return f"ecx={struct.unpack_from('<I', back, m + 1)[0]}"
    if back.endswith(b"\x33\xc9") or back.endswith(b"\x31\xc9"):
        return "ecx=0"
    return ""


def main():
    exe, code, target = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    depth = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    img, _ = build_image(exe, code)
    fns = Functions(Image(exe))
    methods, ntd, nvt = rtti_methods(img)
    calls = call_index(img)
    print(f"RTTI: {ntd} classes, {nvt} vtables, {len(methods)} labelled functions; {sum(len(v) for v in calls.values())} direct calls")
    fstart = lambda r: (fns.find(r) or (None,))[0]
    sites = calls.get(target, [])
    byfn = collections.OrderedDict()
    for s in sites:
        byfn.setdefault(fstart(s), []).append(s)
    for f, ss in byfn.items():
        args = ", ".join(sorted(set(arg_ecx(img, s) or "?" for s in ss)))
        label = "; ".join(methods.get(f, [])) if f else ""
        print(f"\n== caller fn game+{f:#x} ({args})  {label}" if f else f"\n== caller (no function entry) {[hex(s) for s in ss]}")
        if not f or label:
            continue
        seen, frontier = {f}, [f]
        for lvl in range(1, depth + 1):
            nxt = []
            for g in frontier:
                for s in calls.get(g, []):
                    h = fstart(s)
                    if h is None or h in seen:
                        continue
                    seen.add(h)
                    lab = "; ".join(methods.get(h, []))
                    if lab:
                        print(f"   {'  ' * lvl}^ level {lvl}: game+{h:#x}  {lab}")
                    else:
                        nxt.append(h)
            frontier = nxt[:40]
            if not frontier:
                break
        if len(seen) == 1:
            print("   (no direct callers: probably called through a pointer)")


if __name__ == "__main__":
    main()
