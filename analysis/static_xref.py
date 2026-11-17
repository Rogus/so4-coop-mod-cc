"""Offline static analysis of the game's code section saved by probe v0.11.

usage:
  static_xref.py disp  CODE EXE 0x129a0 [0x129c0 ...]   instructions using these displacements
  static_xref.py calls CODE EXE 0x1740000 [...]          direct calls (E8 rel32) to these RVAs

CODE = so4coop_dumps/code_full_<rva>.bin, EXE = StarOceanTheLastHope.exe (only its headers,
.pdata and .xdata are read: function bounds). Read-only; prints disassembly via GNU objdump.
"""
import bisect, re, struct, subprocess, sys, os, tempfile

BASE = 0x140000000


def pe_sections(exe):
    with open(exe, "rb") as f:
        hdr = f.read(0x1000)
        pe = struct.unpack_from("<I", hdr, 0x3C)[0]
        nsec = struct.unpack_from("<H", hdr, pe + 6)[0]
        optsz = struct.unpack_from("<H", hdr, pe + 20)[0]
        exc_rva, exc_size = struct.unpack_from("<II", hdr, pe + 24 + 112 + 3 * 8)
        secs = []
        for i in range(nsec):
            o = pe + 24 + optsz + 40 * i
            name = hdr[o:o + 8].rstrip(b"\0").decode("latin-1")
            vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", hdr, o + 8)
            secs.append((name, rva, vsize, rawptr, rawsize))
    return secs, exc_rva, exc_size


class Image:
    """reads RVAs from the exe on disk (for .pdata/.xdata, which are not encrypted)"""
    def __init__(self, exe):
        self.f = open(exe, "rb")
        self.secs, self.exc_rva, self.exc_size = pe_sections(exe)

    def read(self, rva, n):
        for name, srva, vsize, rawptr, rawsize in self.secs:
            if srva <= rva < srva + max(vsize, rawsize) and rva - srva + n <= rawsize:
                self.f.seek(rawptr + rva - srva)
                return self.f.read(n)
        return None


class Functions:
    def __init__(self, img):
        raw = img.read(img.exc_rva, img.exc_size)
        self.img = img
        self.tab = [struct.unpack_from("<III", raw, i) for i in range(0, len(raw) - 11, 12)]
        self.starts = [t[0] for t in self.tab]

    def find(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i < 0 or not (self.tab[i][0] <= rva < self.tab[i][1]):
            return None
        e = self.tab[i]
        end = e[1]
        for _ in range(8):                        # chained unwind info -> primary entry
            ui = self.img.read(e[2] & ~1, 4)
            if not ui or not ((ui[0] >> 3) & 4):
                break
            off = (e[2] & ~1) + 4 + ((ui[2] + 1) & ~1) * 2
            ch = self.img.read(off, 12)
            if not ch:
                break
            e = struct.unpack("<III", ch)
        return e[0], end


def disasm(code, code_rva, lo, hi):
    """objdump of [lo, hi) RVAs -> list of (rva, text)"""
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as t:
        t.write(code[lo - code_rva:hi - code_rva])
        path = t.name
    try:
        out = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel",
                              f"--adjust-vma={BASE + lo:#x}", path], capture_output=True, text=True).stdout
    finally:
        os.unlink(path)
    res = []
    for ln in out.splitlines():
        m = re.match(r"\s+([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)", ln)
        if m:
            res.append((int(m.group(1), 16) - BASE, len(m.group(2).split()), m.group(3)))
    return res


def main():
    mode, code_path, exe = sys.argv[1:4]
    vals = [int(v, 16) for v in sys.argv[4:]]
    code = open(code_path, "rb").read()
    code_rva = int(re.search(r"code_full_([0-9a-f]{8})", code_path).group(1), 16)
    fns = Functions(Image(exe))
    print(f"code rva {code_rva:#x} size {len(code):#x}, {len(fns.tab)} function entries")

    if mode == "disp":
        hits = {}
        for v in vals:
            pat = struct.pack("<I", v)
            i = code.find(pat)
            while i >= 0:
                hits.setdefault(code_rva + i, v)
                i = code.find(pat, i + 1)
        byfn = {}
        for at, v in sorted(hits.items()):
            fb = fns.find(at)
            byfn.setdefault(fb, []).append((at, v))
        print(f"{len(hits)} raw byte matches in {len(byfn)} functions")
        for fb, lst in sorted(byfn.items(), key=lambda x: (x[0] is None, x[0])):
            if fb is None:
                print(f"\n-- no function entry: {', '.join(f'{a:#x}' for a, _ in lst)}")
                continue
            ins = disasm(code, code_rva, fb[0], fb[1])
            print(f"\n== fn game+{fb[0]:#010x}-{fb[1]:#010x} ({fb[1] - fb[0]} bytes)")
            for at, v in lst:
                for rva, n, text in ins:
                    if rva <= at < rva + n:
                        kind = "WRITE" if re.match(rf"\w+\s+\w*\s*(?:PTR )?\[[^\]]*{v:#x}\],", text) else "read "
                        print(f"   {kind} game+{rva:#010x}: {text}")
                        break
                else:
                    print(f"   (not at an instruction boundary) {at:#x}")

    elif mode == "calls":
        targets = set(vals)
        i = code.find(b"\xe8")
        found = []
        while i >= 0:
            if i + 5 <= len(code):
                t = code_rva + i + 5 + struct.unpack_from("<i", code, i + 1)[0]
                if t in targets:
                    found.append((code_rva + i, t))
            i = code.find(b"\xe8", i + 1)
        print(f"{len(found)} direct calls")
        for at, t in found:
            fb = fns.find(at)
            print(f"   call game+{t:#010x} from game+{at:#010x} in fn " + (f"game+{fb[0]:#010x}" if fb else "?"))


if __name__ == "__main__":
    main()
