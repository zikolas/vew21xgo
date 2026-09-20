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
under `/FORCE /S=n`, never written (repair is `vewcis/`).

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

## The rest of the kit

Each lives in its own directory with its own README and build script:

- [`vewcis/`](vewcis/) — **VEWCIS** repairs a CF-VEW211 whose CIS EEPROM
  has failed, permanently and in software. Its `/BURN` is irreversible
  without a byte-exact image of the card; read its warning first.
- [`fmvol/`](fmvol/) — **FMVOL** gives the 211's FM synth the volume
  control it lacks in hardware: a Jemm module that rescales carrier
  levels in flight, real-mode and V86 games only. Not needed on the 212
  and JSC101, where the FM passes through the codec's line input and its
  gain register (I18/I19) is the control.
- [`synth/`](synth/) — **OPL4SYN** stays resident and turns the 212's
  OPL4/YRW801 into a General MIDI synth for games: with
  [MPUSHIM](https://github.com/zikolas/mpushim) trapping an MPU-401 at
  330 it plays alongside SB digital audio from one boot. **OPL4MID**
  plays a .MID file on it from the command line.
- [`probes/`](probes/) — the diagnostic programs that established
  everything above, and the byte-exact CIS images of all four cards.
- [`legacy/`](legacy/) — the 1.x C enabler this project grew from.

## Build

```
./build.sh          VEW21XGO.COM (NASM, host or on-box)
```

## Documentation

- [`doc/ASIC.md`](doc/ASIC.md) — the 211's MEI DA65646 ASIC: decode,
  CIS shadow, config and vendor registers, the EEPROM commit strobe.
- [`fmvol/README.md`](fmvol/README.md) — the FM volume problem and the module.
- [`probes/CIS_VEW212.TXT`](probes/CIS_VEW212.TXT) — the 212 recon.

Clean-room: the cards' own CIS dumps, the public Intel 82365SL register
set, the PCMCIA CS/SS specs (RBIL, the SystemSoft CardSoft technical
guide), live-probed OmniBook Socket Services, the public CS4231A and OPL
programming models, and vendor enablers run and read back off the
hardware. No vendor driver code was read.

## License

Everything here is GPL v2 — see [LICENSE](LICENSE). The enabler alone
was MIT through 2.5; the repo moved to GPL v2 when the synth, whose
instrument tables derive from the GPL/BSD ALSA opl4 driver, moved in.
