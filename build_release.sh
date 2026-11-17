#!/bin/sh
# Release build of the tag-team mod: same sources, SO4COOP_RELEASE (only F3/F4 + controller R3,
# log in so4coop.log). Output: build/release/xinput1_3.dll
set -e
cd "$(dirname "$0")"
mkdir -p build/release
for l in kernel32 user32; do llvm-dlltool -m i386:x86-64 -d src/lib/$l.def -l build/$l.lib; done
CFLAGS="--target=x86_64-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe -Wall -Wextra -Wno-unused-parameter -DSO4COOP_RELEASE"
clang $CFLAGS -c src/probe.c -o build/release/probe.obj
lld-link /nologo /dll /nodefaultlib /entry:DllMain /subsystem:windows /machine:x64 \
    /def:src/xinput1_3.def /out:build/release/xinput1_3.dll build/release/probe.obj build/kernel32.lib build/user32.lib
echo "built build/release/xinput1_3.dll"
