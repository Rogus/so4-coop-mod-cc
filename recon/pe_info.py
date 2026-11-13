"""Static recon of StarOceanTheLastHope.exe (read-only)."""
import sys, re, collections, pefile
exe = sys.argv[1]
pe = pefile.PE(exe, fast_load=False)
print("ImageBase", hex(pe.OPTIONAL_HEADER.ImageBase), "Entry", hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint))
for s in pe.sections:
    print(f"  {s.Name.rstrip(b'\0').decode():8s} va={s.VirtualAddress:#010x} vsize={s.Misc_VirtualSize:#010x} raw={s.SizeOfRawData:#010x}")
print("Imports:")
for imp in pe.DIRECTORY_ENTRY_IMPORT:
    names = [i.name.decode() if i.name else f"#{i.ordinal}" for i in imp.imports]
    tag = imp.dll.decode()
    if re.search(r"xinput|steam|user32|hid|setupapi|dinput|winmm", tag, re.I):
        print(f"  {tag}: " + ", ".join(f"{n}@{i.address:#x}" for n, i in zip(names, imp.imports)))
    else:
        print(f"  {tag}: {len(names)} functions")
