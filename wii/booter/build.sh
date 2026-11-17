#!/bin/bash
# Compile le booter en binaire brut et le dépose dans data/ (intégré à Wiil).
set -e
cd "$(dirname "$0")"
PPC=$DEVKITPPC/bin/powerpc-eabi
$PPC-gcc -Os -mcpu=750 -meabi -mhard-float -ffreestanding -fno-builtin -nostdlib -fno-pic \
	-Wall -c booter.c -o booter.o
$PPC-ld -T booter.ld booter.o -o booter.elf
$PPC-objcopy -O binary booter.elf ../data/booter.bin
$PPC-nm booter.elf | grep -i ' booter$'
ls -l ../data/booter.bin
