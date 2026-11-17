"""What do the callers of a function do right after calling it? (offline)

usage: users.py EXE CODE_DUMP TARGET_RVA [after=40]

For every direct call site of TARGET, disassembles `after` instructions following the call
(linear; stops at ret / int3), and lists the calls and RIP-relative globals found there, flagging
the ones that look like input: calls into the controller layer (game+0x740000..0x780000) and
globals in the controller tables (game+0x1058000..0x105b000). Read-only.
"""
import os, re, sys, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vtable import build_image, disasm_from
from callgraph import call_index
from static_xref import Image, Functions

INPUT_CODE = (0x740000, 0x780000)
INPUT_DATA = (0x1058000, 0x105b000)


def main():
    exe, code, target = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    after = int(sys.argv[4]) if len(sys.argv) > 4 else 40
    img, _ = build_image(exe, code)
    fns = Functions(Image(exe))
    calls = call_index(img)
    sites = calls.get(target, [])
    print(f"{len(sites)} call sites of game+{target:#x}")
    summary = collections.Counter()
    for s in sites:
        fb = fns.find(s)
        ins = disasm_from(img, s + 5, after)
        cl, gl, flags = [], [], []
        for a, t in ins:
            m = re.match(r"call\s+0x([0-9a-f]+)", t)
            if m:
                r = int(m.group(1), 16) - 0x140000000
                cl.append(r)
                if INPUT_CODE[0] <= r < INPUT_CODE[1]: flags.append(f"call game+{r:#x}")
            for g in re.findall(r"# 0x([0-9a-f]+)", t):
                r = int(g, 16) - 0x140000000
                gl.append(r)
                if INPUT_DATA[0] <= r < INPUT_DATA[1]: flags.append(f"global game+{r:#x}")
        for r in cl: summary[r] += 1
        tag = "  <== INPUT? " + ", ".join(flags) if flags else ""
        print(f"\nsite game+{s:#x} in fn {('game+%#x' % fb[0]) if fb else '?'}: calls {[hex(c) for c in cl[:8]]} globals {[hex(g) for g in gl[:6]]}{tag}")
    print("\nmost called right after (all sites):")
    for r, n in summary.most_common(15):
        print(f"   game+{r:#x}: {n}")


if __name__ == "__main__":
    main()
