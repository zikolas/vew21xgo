#!/bin/sh
# VEW21XGO 2.0 build — NASM flat binary (byte-identical on-box with NASM at C:\NASM)
# (The 1.x C point enabler still builds on-box with BUILD.BAT / Open Watcom.)
set -e
nasm -f bin -o VEW21XGO.COM VEW21XGO.ASM
ls -la VEW21XGO.COM
