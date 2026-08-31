# The OPL4 wavetable synth (CF-VEW212)

Two DOS programs that drive the OPL4 (Yamaha YMF278B) and its onboard
YRW801 sample ROM directly, written for the Panasonic CF-VEW212
"Sound Card PRO" PC Card:

- **OPL4MID** — a standalone General MIDI .MID file player.
- **OPL4SYN** — the same engine as a resident synth TSR: MIDI bytes go
  in over an INT 2Fh multiplex, wavetable GM comes out. This is what
  lets DOS **games** play the wavetable, through
  [MPUSHIM](https://github.com/zikolas/mpushim).

The CF-VEW211 has no OPL4; on other YMF278B+YRW801 cards exposing the
OPL4 at the standard FM base this should also work, but only the 212 has
been tested.

## Why

The CF-VEW212 has two hardware personalities, selected by the PCMCIA
configuration index, and they are complementary:

| COR index | provides | lacks |
|---|---|---|
| 23h | CS4231A codec + OPL4 FM + wavetable | MPU-401 |
| 26h | MPU-401 at 330h + OPL4 FM + wavetable | codec |

The vendor's DOS MIDI stack needs the card's MPU-401, so it cannot
coexist with digital audio. These programs need no MPU: they program the
OPL4's 24 wave voices over the FM register window, so digital audio, FM
and wavetable MIDI all work together on index 23h.

## OPL4SYN - wavetable MIDI for games

    OPL4SYN [/BASE=388] [/MIX=n] [/TL=n] [/ID=xx]    install, stay resident
    OPL4SYN /TEST [/ID=xx]                           play a test through it

The resident interface (`AH` = multiplex id, default BDh):

    INT 2Fh  AL=00  install check -> AL=FFh
             AL=01  MIDI byte in DL

Games don't call that themselves - they write to an MPU-401 at 330h.
MPUSHIM supplies that MPU-401 as a trap-based facade in every world a
DOS game lives in (V86, 16-bit and 32-bit protected mode) and hands each
MIDI byte to OPL4SYN:

    VEW21XGO /PCIC          the enabler, COR index 23h
    OPL4SYN                 this synth, resident
    ...trap hosts...        JEMM+QPIEMU, HDPMI16i, HDPMI32i
    MPUSHM16 /SYNTH         the 16-bit protected-mode shim
    MPUSHIM /SYNTH          the 32-bit + V86 shim

Ready-made launchers for that stack (and the flavours that add Sound
Blaster digital via VSBPCM) are in mpushim's `go/` directory - GOWMIDI,
GOW32, GOW16. Bench-proven catalogue: DOSMID and Monkey Island (V86),
DOOM (32-bit), Tyrian (16-bit), all from one boot.

Over OPL4MID's engine, OPL4SYN adds a live byte-stream state machine
(running status, SysEx swallowing), live pitch bend, CC10 pan and
CC121; CC64 sustain is not implemented yet. It keeps 17,712 bytes
resident - it links no C library, printing and parsing through DOSIO.H
instead, because everything stdio drags in would stay resident too.

## OPL4MID - the file player

    OPL4MID <file.mid> [/TL=n] [/MIX=n] [/PAN=n] [/LOOP] [/LIST] [/V]
                       [/BASE=388]

    /TL=n    master attenuation, 0-127, added to every voice (default 0)
    /MIX=n   PCM mix level into DO2, 0-7, 3 dB a step, 0 = loudest
    /PAN=n   fixed panpot -7..7, overriding the per-region value
    /LOOP    repeat until a key is pressed
    /LIST    parse and report, play nothing
    /V       trace note events

Format 0 and 1 files up to 64000 bytes and 32 tracks are handled. Pitch
bend, expression (CC11), pan (CC10) and sustain (CC64) are not
implemented in the player; volume (CC7), program change and
all-notes-off are.

## Requirements

- The card enabled on COR index 23h with the OPL4 window at 388-38Dh:
  run VEW21XGO (this repo) first.
- Something audible on the card's output. On the 212 the wave/FM mix
  (DO2) is routed into the codec's line input; VEW21XGO unmutes it.

## Build

Open Watcom 1.9, 16-bit small model, C89 (an on-box BLD.BAT with
`wcc -ms` works the same):

    wcc -ms OPL4MID.C
    wlink system dos file OPL4MID.obj

    wcc -ms OPL4SYN.C
    wlink system dos file OPL4SYN.obj

## Instrument data

The YRW801 region tables (key splits, pitch offsets, envelope and level
parameters, drum map) and the volume table are ported from the Linux
ALSA opl4 driver, sound/drivers/opl4/yrw801.c and opl4_synth.c,
copyright 2003 Clemens Ladisch, dual-licensed BSD-2-clause / GPL v2.
Reference copies of those files are in ref-alsa/, and genalsa.py
regenerates OPL4TAB.H from them. The melodic map was independently
cross-checked against register captures taken from the vendor renderer
on real hardware (doc/ in this repo).

They are packed for a program that stays resident, and the packing is
generated, never hand-edited. A region quotes its level set (ksc, pan,
att, vf) and its envelope set by one-byte index, since 610 regions draw
on only 126 and 220 distinct sets; interning whole regions would be
pointless, as 601 of the 610 differ once tone and pitch offset are
included. ALSA's 1536-entry F-number table is exactly
round(1024*2^(p/1536))-1024, so pitch_fnum() interpolates it from 13
semitone knots instead - 3072 bytes become 26, worst case 1 LSB, finer
than the OPL4's own F-number quantisation. Read regions through the RG_*
accessors and pitch through pitch_fnum(); nothing outside OPL4TAB.H
should know how any of it is stored.

verifytab.py is the check on all of that: it digests every value that
reaches the chip, for all 129 programs and all 128 notes, from two
layouts and compares. Run it against the previous OPL4TAB.H after any
change to the packing.

## License

GPL v2, see the repo LICENSE. The ALSA-derived tables are used under the
GPL option of their dual license.
