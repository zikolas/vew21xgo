# vew212-opl4

A General MIDI file player for DOS that drives the OPL4 (Yamaha YMF278B)
wavetable directly, written for the Panasonic CF-VEW211/212 "Sound Card PRO"
PC Card and its onboard YRW801 sample ROM.

## Why

The CF-VEW212 has two hardware personalities, selected by the PCMCIA
configuration index, and they are complementary:

| COR index | provides | lacks |
|---|---|---|
| 23h | CS4231A codec + OPL4 FM + wavetable | MPU-401 |
| 26h | MPU-401 at 330h + OPL4 FM + wavetable | codec |

The vendor's DOS MIDI stack needs the MPU-401, so it cannot coexist with
digital audio. This player needs no MPU: it parses the MIDI file itself and
programs the OPL4's 24 wave voices over the FM register window, so digital
audio, FM and wavetable MIDI all work together on index 23h.

It should also work on other YMF278B+YRW801 cards that expose the OPL4 at
the standard FM base, but has only been tested on the CF-VEW212.

## Requirements

- The card enabled on COR index 23h with the OPL4 window at 388-38Dh.
  On the CF-VEW211/212, run VEW21XGO (github.com/zikolas/vew21xgo) first.
- Something audible on the card's output. On the 212 the wave/FM mix (DO2)
  is routed into the codec's line input; VEW21XGO unmutes it.

## Usage

    OPL4MID <file.mid> [/TL=n] [/MIX=n] [/PAN=n] [/LOOP] [/LIST] [/V]
                       [/BASE=388]

    /TL=n    master attenuation, 0-127, added to every voice (default 0)
    /MIX=n   PCM mix level into DO2, 0-7, 3 dB a step, 0 = loudest
    /PAN=n   fixed panpot -7..7, overriding the per-region value
    /LOOP    repeat until a key is pressed
    /LIST    parse and report, play nothing
    /V       trace note events

Format 0 and 1 files up to 64000 bytes and 32 tracks are handled. Pitch
bend, expression (CC11), pan (CC10) and sustain (CC64) are not implemented
yet; volume (CC7), program change and all-notes-off are.

## Build

Open Watcom 1.9, 16-bit small model, C89:

    wcc -ms OPL4MID.C
    wlink system dos file OPL4MID.obj

## Instrument data

The YRW801 region tables (key splits, pitch offsets, envelope and level
parameters, drum map) and the F-number pitch map are ported from the Linux
ALSA opl4 driver, sound/drivers/opl4/yrw801.c and opl4_synth.c, copyright
2003 Clemens Ladisch, dual-licensed BSD-2-clause / GPL v2. Reference copies
of those files are in ref-alsa/, and genalsa.py regenerates OPL4TAB.H from
them. The melodic map was independently cross-checked against register
captures taken from the vendor renderer on real hardware (doc/ in the
vew21xgo repo).

## License

GPL v2, see LICENSE. The ALSA-derived tables are used under the GPL option
of their dual license.
