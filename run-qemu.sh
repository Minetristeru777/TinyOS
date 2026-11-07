#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec qemu-system-i386 \
    -boot order=a \
    -rtc base=utc \
    -drive format=raw,file=os.img,if=floppy \
    -drive format=raw,file=disk.bin,if=ide,index=0 \
    -drive format=raw,file=disk2.bin,if=ide,index=1 \
    -netdev user,id=net0 \
    -device ne2k_pci,netdev=net0
