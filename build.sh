#!/bin/sh
# Builds the Windows x64 DLLs with clang + lld-link only (no mingw, no MSVC).
set -e
cd "$(dirname "$0")"
mkdir -p build
for l in kernel32 user32; do llvm-dlltool -m i386:x86-64 -d src/lib/$l.def -l build/$l.lib; done
CFLAGS="--target=x86_64-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe -Wall -Wextra -Wno-unused-parameter"
clang $CFLAGS -c src/probe.c -o build/probe.obj
lld-link /nologo /dll /nodefaultlib /entry:DllMain /subsystem:windows /machine:x64 \
    /def:src/xinput1_3.def /out:build/xinput1_3.dll build/probe.obj build/kernel32.lib build/user32.lib
echo "built build/xinput1_3.dll"
