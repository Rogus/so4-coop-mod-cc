"""List a vtable's methods and disassemble the start of each (offline).

usage: vtable.py EXE CODE_DUMP VTABLE_RVA [max_entries=64] [max_insns=60]

The image is the exe on disk (sections mapped by RVA) with the executable section saved by the
probe (code_full_<rva>.bin, decrypted at runtime) laid over it. Each method is disassembled
linearly from its start until ret / unconditional jmp / int3 (anti-disassembly junk follows
those), printing call targets and the fields of `this` (rcx) it touches. Read-only.
"""
import re, struct, subprocess, sys, tempfile, os

BASE = 0x140000000
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from static_xref import pe_sections, Image, Functions


def build_image(exe, code_path):
    secs, _, _ = pe_sections(exe)
    data = open(exe, "rb").read()
    size = max(rva + max(vsize, rawsize) for _, rva, vsize, _, rawsize in secs)
    img = bytearray(size)
    for _, rva, vsize, rawptr, rawsize in secs:
        k = min(rawsize, vsize) if vsize else rawsize
        img[rva:rva + k] = data[rawptr:rawptr + k]
    code_rva = int(re.search(r"code_full_([0-9a-f]{8})", code_path).group(1), 16)
    code = open(code_path, "rb").read()
    img[code_rva:code_rva + len(code)] = code
    execs = [(rva, rva + vsize) for name, rva, vsize, _, _ in secs if vsize]
    return bytes(img), secs


def disasm_from(img, rva, max_insns):
    blob = img[rva:rva + max_insns * 15]
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as t:
        t.write(blob)
        path = t.name
    try:
        out = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel",
                              f"--adjust-vma={BASE + rva:#x}", path], capture_output=True, text=True).stdout
    finally:
        os.unlink(path)
    res = []
    for ln in out.splitlines():
        m = re.match(r"\s+([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)", ln)
        if not m:
            continue
        res.append((int(m.group(1), 16) - BASE, m.group(3)))
        t = m.group(3)
        if len(res) >= max_insns or re.match(r"(ret|int3|jmp\s)", t):
            break
    return res


def main():
    exe, code_path, vt = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    max_entries = int(sys.argv[4]) if len(sys.argv) > 4 else 64
    max_insns = int(sys.argv[5]) if len(sys.argv) > 5 else 60
    img, secs = build_image(exe, code_path)
    execs = [(rva, rva + vsize) for name, rva, vsize, _, _ in secs if vsize and rva in (0x1000, 0x13a2000)]
    in_code = lambda r: any(a <= r < b for a, b in execs)
    for i in range(max_entries):
        p = struct.unpack_from("<Q", img, vt + 8 * i)[0]
        r = p - BASE
        if not in_code(r):
            print(f"[{i}] {p:#x}: not code, end of vtable")
            break
        ins = disasm_from(img, r, max_insns)
        fields = sorted(set(re.findall(r"\[rcx\+(0x[0-9a-f]+)\]", " ".join(t for _, t in ins[:8]))), key=lambda x: int(x, 16))
        calls = [t.split()[-1] for _, t in ins if t.startswith("call")]
        print(f"\n[{i}] +{8 * i:#x} -> game+{r:#x}  ({len(ins)} insns shown; this-fields {fields}; calls {calls[:6]})")
        for a, t in ins:
            print(f"     game+{a:#x}: {t}")


if __name__ == "__main__":
    main()
