#!/bin/sh
set -eu
cd "$(dirname "$0")"

nasm -f bin boot.asm -o boot.bin
nasm -f bin setup.asm -o setup.bin
gcc -m32 -march=i386 -mno-sse -mno-mmx -msoft-float -Os \
    -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
    -ffunction-sections -fdata-sections \
    -c kernel.c -o kernel.o
gcc -m32 -march=i386 -mno-sse -mno-mmx -msoft-float -Os \
    -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
    -ffunction-sections -fdata-sections \
    -c network.c -o network.o
ld -m elf_i386 --oformat binary -T kernel.ld kernel.o network.o -o kernel.bin

kernel_size=$(wc -c < kernel.bin)
if [ "$kernel_size" -gt 137216 ]; then
    echo "kernel.bin too large for boot loader (max 137216 bytes)" >&2
    exit 1
fi

dd if=/dev/zero of=os.img bs=512 count=2880 status=none
dd if=boot.bin of=os.img conv=notrunc status=none
dd if=setup.bin of=os.img bs=512 seek=1 conv=notrunc status=none
dd if=kernel.bin of=os.img bs=512 seek=2 conv=notrunc status=none
if [ ! -f disk.bin ]; then
    dd if=/dev/zero of=disk.bin bs=512 count=128 status=none
else
    disk_size=$(wc -c < disk.bin)
    if [ "$disk_size" -gt 65536 ]; then
        echo "disk.bin is larger than the supported 64 KiB" >&2
        exit 1
    fi
    if [ "$disk_size" -lt 65536 ]; then
        truncate -s 65536 disk.bin
    fi
fi
if [ ! -f disk2.bin ]; then
    dd if=/dev/zero of=disk2.bin bs=512 count=128 status=none
else
    disk2_size=$(wc -c < disk2.bin)
    if [ "$disk2_size" -gt 65536 ]; then
        echo "disk2.bin is larger than the supported 64 KiB" >&2
        exit 1
    fi
    if [ "$disk2_size" -lt 65536 ]; then
        truncate -s 65536 disk2.bin
    fi
fi
echo "Built os.img (NASM boot + protected-mode setup + ${kernel_size}-byte C kernel)"
