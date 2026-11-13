"""Same RTTI walk as src/scan.c, on the exe file (sections mapped by RVA)."""
import sys, struct, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
img = bytearray(pe.OPTIONAL_HEADER.SizeOfImage)
for s in pe.sections:
    d = s.get_data()[:s.Misc_VirtualSize]
    img[s.VirtualAddress:s.VirtualAddress + len(d)] = d
base = pe.OPTIONAL_HEADER.ImageBase
def secname(rva): return next((s.Name.rstrip(b'\0').decode() for s in pe.sections if s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize), '?')
classes = sys.argv[2:]
td = {}
for c in classes:
    i = img.find(b".?AV" + c.encode() + b"@@\0")
    if i > 0: td[c] = i - 0x10
print("type descriptors:", {c: f"{r:#x}({secname(r)})" for c, r in td.items()})
rev = {r: c for c, r in td.items()}
cols = {}
mv = memoryview(img)
arr = struct.unpack_from(f"<{len(img)//4}I", img)
for k in range(0, len(arr) - 6):
    if arr[k] == 1 and arr[k + 5] == k * 4 and arr[k + 3] in rev:
        cols.setdefault(rev[arr[k + 3]], []).append((k * 4, arr[k + 1], arr[k + 4]))
print("COLs:", {c: [(f"{r:#x}", off, f"chd={chd:#x}") for r, off, chd in v] for c, v in cols.items()})
colset = {r + base: c for c, v in cols.items() for r, off, chd in v if off == 0}
q = struct.unpack_from(f"<{len(img)//8}Q", img)
vt = {}
for k in range(len(q) - 1):
    if q[k] in colset and base <= q[k + 1] < base + len(img):
        vt.setdefault(colset[q[k]], []).append((k + 1) * 8)
for c in classes:
    print(f"  {c:24s} vtables: {[f'{r:#x}({secname(r)})' for r in vt.get(c, [])]}")
# class hierarchy of CPlayerEdge: CHD -> BaseClassArray -> BCDs -> TDs
def bases(c):
    r, off, chd = cols[c][0]
    n, arr_rva = struct.unpack_from("<4xII", img, chd + 4)
    out = []
    for i in range(n):
        bcd = struct.unpack_from("<I", img, arr_rva + 4 * i)[0]
        tdr = struct.unpack_from("<I", img, bcd)[0]
        out.append(img[tdr + 0x10: img.index(b"\0", tdr + 0x10)].decode())
    return out
for c in ("CPlayerEdge@Battle", "CPlayerThinkChr@Battle", "CBtlPad@Battle", "PadXe@Aska"):
    if c in cols: print(f"  hierarchy {c}: {bases(c)}")
