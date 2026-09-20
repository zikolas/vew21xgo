# VEW21XGO — a DOS enabler for the Panasonic CF-VEW211 family

One `.COM` brings up the Matsushita/Panasonic CF-VEW211 PCMCIA sound card
and its relatives with no vendor software: a CS4231A WSS codec plus FM,
three host backends, auto-detected.

| Card | MANFID | Configured as |
|---|:---:|---|
| CF-VEW211 | `0032/0001` | codec at `530` (or `E80`/`F40`/`604` via `/IO`), OPL3 at `388` |
| CF-VEW212 "Sound Card PRO" | `0032/0501` | codec at `530` + OPL4 FM/wavetable at `388–38D`, on its undeclared working index `23h`; `/MIDI` switches to the vendor MPU-401 personality |
| CF-JSC101 "Sound SCSI Card" | `0032/0701` | codec at `530` + OPL3 at `388`; the SCSI half is left unconfigured |
| NEC PC-9801N-J04 | none (VERS_1 string) | codec at `F40`, no FM fitted |

All four share the MEI ASIC and codec. On the 212 and JSC101 the FM
enters the codec's line input, which the enabler un-mutes; the JSC101's
ASIC has four configuration register sets 20h apart (codec, OPL3 plus
the codec's clock, SCSI, unknown), so its COR index is written twice.
The 211's FM is summed after the codec and has no volume control in
hardware (see FMVOL). A card whose CIS EEPROM has failed reads as a
uniform fill; it is reported, and configured from built-in knowledge only
under `/FORCE /S=n`, never written (see VEWCIS).

Backends, in auto-detect order:

- `/CS` — any PCMCIA Card Services 2.1 stack. Registers as a client and
  stays resident: configures on hot-plug, a later run reconfigures through
  the resident copy, `/OFF` releases it.
- `/OB` — HP OmniBook 300/425/430 ROM Socket Services, no Card Services
  needed. Verified on a 425; user sockets 1–2 only.
- `/PCIC` — Intel 82365-class controllers programmed directly (IBM PC110,
  ThinkPad 235…).

## Usage

```
VEW21XGO /T
```

Run it once; the configuration sticks. Point games at AdLib at `388`
(not on the J04), or run [VSBPCMCIA](https://github.com/zikolas/vsbhda-pcmcia)
against the codec for Sound Blaster emulation with the real FM chip.

```
VEW21XGO [/PCIC|/CS|/OB] [/IO=530] [/I=0] [/VOL=24] [/T] [/SPKR] [/NOFM]
         [/MIDI] [/S=n] [/W=D000] [/FORCE] [/OFF] [/V] [/?]

  /IO=hex   codec base 530 (default) / E80 / F40 / 604 — 211 only; the
            212 and JSC101 are fixed at 530, the J04 at F40
  /I=dec    IRQ 7, 9, 10 or 11 (level mode; default none — FM needs none)
  /VOL=dec  DAC attenuation, 1.5 dB per step, 0..63 (default 24 = -36 dB)
  /T        play a short FM test tone after enabling
  /SPKR     also route audio to the host speaker (#SPKR pin; mono, 1-bit)
  /NOFM     do not claim the 388 FM window
  /MIDI     212 only, PCIC only: finish on index 26h (MPU-401 at 330 +
            OPL4) for the vendor OPL4TSR/OPL4DRV stack; implies /I=9
  /S=dec    socket (PCIC 0-7, OB 1-2, CS: probe only this one)
  /W=hex    attribute-window segment for CIS/COR access (PCIC; default
            D000, moved automatically if another card is mapped there —
            keep it out of your memory manager's UMB range)
  /FORCE    skip the CIS identity check; the only way to enable a dead-CIS
            card (needs /S)
  /OFF      PCIC: power the socket down; CS: release and go dormant
  /V        show the working detail: CIS strings, CORs, codec ID, mixer
```

DOS runs `.COM` before `.EXE`, so this takes over from the old 1.x `.EXE`
if both sit in one directory.

## VEWCIS — repairing a dead CIS

`VEWCIS.EXE` writes a known-good CIS back into a CF-VEW211 whose EEPROM
load has failed, and nothing else. The model must be given, because a
dead card cannot say what it is:

```
VEWCIS /211            volatile heal: into the RAM shadow, until power-down
VEWCIS /211 /BURN      permanent: commit the image to the EEPROM, verify
VEWCIS /211 /RESTORE   burn the reference image even over a valid CIS
```

> **`/BURN` and `/RESTORE` are permanent and only reversible with a
> byte-exact image of that card.** The commit strobe belongs to the MEI
> ASIC, so a dead J04, 212 or JSC101 would accept a 211 image and be
> stamped with the wrong identity. Never conclude a CIS is dead from one
> quick read (attribute memory lags socket power by up to ~110 ms on some
> hosts; VEWCIS and the enabler gate on the data since 2.4). VEWCIS
> refuses all writes on a 212.

## FMVOL — FM volume for the 211

The 211's FM has no volume control in hardware. `FMVOL.DLL`, a Jemm
loadable module, traps the OPL ports and rescales carrier Total Level
writes in flight, 0–63 steps of 0.75 dB (`FMGO`, or `JLOAD FMVOL.DLL 24`;
`JLOAD -u` unloads). Real-mode and V86 games only: DOS-extender games
bypass it. Details in [`doc/FMVOL.md`](doc/FMVOL.md). On the 212 and
JSC101 the FM passes through the codec's line input instead, so its
gain register (I18/I19) is the volume control there.

## The wavetable synth (synth/)

`OPL4SYN` stays resident and turns the 212's OPL4/YRW801 into a General
MIDI synth for games: with [MPUSHIM](https://github.com/zikolas/mpushim)
trapping an MPU-401 at 330 it plays alongside SB digital audio from one
boot. `OPL4MID` plays a .MID file on it from the command line. See
[synth/README.md](synth/README.md).

## Build

```
./build.sh          VEW21XGO.COM (NASM, host or on-box)
BUILD VEWCIS        VEWCIS.EXE (Open Watcom, 16-bit real mode)
./fmbuild.sh        FMVOL.DLL (JWasm + wlink on the host; FMBLD.BAT on a DOS box)
```

The synth builds with Open Watcom (`wcc -ms`, C89); the 1.x C enabler
this project grew from is archived in `legacy/`.

## Documentation

- [`doc/ASIC.md`](doc/ASIC.md) — the 211's MEI DA65646 ASIC: decode,
  CIS shadow, config and vendor registers, the EEPROM commit strobe.
- [`doc/FMVOL.md`](doc/FMVOL.md) — the FM volume problem and the module.
- [`probes/README.md`](probes/README.md) — every probe that established
  the above, and the byte-exact CIS images of all four cards.
- [`probes/CIS_VEW212.TXT`](probes/CIS_VEW212.TXT) — the 212 recon.

Clean-room: the cards' own CIS dumps, the public Intel 82365SL register
set, the PCMCIA CS/SS specs (RBIL, the SystemSoft CardSoft technical
guide), live-probed OmniBook Socket Services, the public CS4231A and OPL
programming models, and vendor enablers run and read back off the
hardware. No vendor driver code was read.

## License

GPL v2 — see [LICENSE](LICENSE). The enabler alone was MIT through 2.5;
the repo moved to GPL v2 when the synth, whose instrument tables derive
from the GPL/BSD ALSA opl4 driver, moved in.
