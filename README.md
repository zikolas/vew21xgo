# VEW21XGO — a DOS enabler for the Panasonic CF-VEW211 PCMCIA sound card

A single-command DOS **point enabler** that brings the **Panasonic CF-VEW211**
PC Card sound card (Matsushita, 1994 — CS4231A codec + YMF262 OPL3) to life
with no Card Services and no Socket Services. It programs the PCMCIA host
controller and the card directly, then gets out of the way.

Its party trick: **it revives a card whose CIS is dead — and the companion
tool can repair it permanently, in software.** The unit this was written
for has a failed CIS EEPROM: its tuple region reads a stuck fill byte, so
no Card Services stack or CIS-matching enabler can ever identify it. But
the card's ASIC shadows the CIS in host-writable RAM — so VEW21XGO carries
a byte-exact image from a healthy card and, whenever it finds the dead
pattern, **injects the full CIS back into the shadow**: from that moment
the card identifies itself normally to *any* software that asks. Intact
cards are matched by MANFID (`0x0032/0x0001`); dead ones by their
uniform-fill signature, then healed.

Better still, the recon (see `doc/ASIC.md`) uncovered the ASIC's factory
programming hook — an undeclared register whose bit 0 commits the whole
shadow back to the CIS EEPROM — so **`VEWCIS /BURN` performs a one-command
permanent repair**: after it, the card cold-boots self-describing with no
software help at all. (This card was successfully repaired exactly that
way… the first time by accident. Long story, documented.)

Clean-room: built from a known-good CIS dump, the public Intel 82365SL PCIC
register set, the Crystal CS4231A datasheet, and the public OPL FM
programming model. No vendor driver code. Developed and tested on an IBM
PC110; it should work on Intel 82365-class controllers generally.

## Features

| | Feature |
|:---:|---|
| ✅ | **CIS self-healing** — dead-CIS card becomes self-describing every run |
| ✅ | **Permanent CIS repair** — `VEWCIS /BURN` programs the healed CIS back into the card's EEPROM (verified across a power cycle) |
| ✅ | **OPL3 FM synthesis** at the standard AdLib port `0x388` — games just work |
| ✅ | **WSS codec** (CS4231A) mapped at `0x530` / `0xE80` / `0xF40` / `0x604` |
| ✅ | **PCM volume** (`/VOL`) — DAC attenuation in 1.5 dB steps |
| ✅ | **Host-speaker audio** (`/SPKR`) — card audio on the laptop's own speaker via the PCMCIA #SPKR pin (1-bit, lo-fi by nature) |
| ✅ | **Mixer un-mute** — the codec powers up silent; VEW21XGO opens the paths |
| ✅ | **FM beep test** (`/BEEP`) — instant audible proof the card is alive |

## Usage

Run it once; the configuration sticks in the controller until power-off or
suspend (it is **not** a TSR). Then point your game at **AdLib at 388**:

```
VEW21XGO /BEEP
```

With no `/S`, VEW21XGO scans every socket and configures the first CF-VEW211
(or dead-CIS card) it finds, tagged `(auto)` in its output.

```
VEW21XGO [/IO=530] [/I=0] [/VOL=4] [/BEEP] [/SPKR] [/S=0..7] [/W=D000] [/OFF]
  /IO=hex    codec base — one of 530 (default) / E80 / F40 / 604 (picks the
             matching card config index; the codec registers sit at base+4,
             the OPL3 is always at 388)
  /I=dec     IRQ to route to the socket — 7, 9, 10 or 11 only (the card is
             level-mode only; default 0 = none, FM needs no IRQ)
  /VOL=dec   DAC (PCM) attenuation, 1.5 dB per step, 0 (full, clips the
             card's output amp) .. 63; default 24 = -36 dB, a comfortable
             headphone level established by ear (line-out users into
             amplified speakers may prefer /VOL=8 or so)
  /BEEP      play a short FM test note after enabling
  /SPKR      also route the card's audio to the host's internal speaker
             (CCSR Audio bit -> #SPKR pin + PCIC speaker route; mono, harsh)
  /S=dec     socket 0..7 (default: auto-scan)
  /W=hex     attribute-window segment used to reach the card's config
             registers (default D000, auto-relocates if in use)
  /OFF       power the card's socket down and exit
```

## VEWCIS — the standalone repair tool

`VEWCIS.EXE` heals the CIS and touches nothing else — no COR write, no I/O
mapping, no mixer. Use it when you want a different stack (Card Services,
EZ-Play, the period vendor drivers) to own the card afterwards:

```
VEWCIS            volatile heal: inject the CIS shadow, leave the card
                  powered + un-configured + self-describing
VEWCIS /BURN      PERMANENT repair: power-cycles to read the true EEPROM
                  state, injects if dead, pulses the ASIC's commit strobe
                  (attr 0x204 bit0), then power-cycles again and verifies
                  the EEPROM reloads the pristine CIS on its own.
                  Refuses to burn a card that is already healthy.
VEWCIS /RESTORE   like /BURN but unconditional: burns the reference image
                  even over a valid CIS (undo test images / factory-reset
                  to the known-good dump)
```

## The card

| Config index | Codec base | FM | Notes |
|:---:|:---:|:---:|---|
| `0x20` | `0x530` | `0x388` | default |
| `0x21` | `0xE80` | `0x388` | |
| `0x22` | `0xF40` | `0x388` | |
| `0x23` | `0x604` | `0x388` | |

Config registers (COR + CCSR) live at attribute offset `0x200`. The COR reads
back with the LevlREQ bit pinned high — the card only does level-mode
interrupts, hence the `{7, 9, 10, 11}` IRQ set from its CIS.

**Digital audio works — without DMA.** The CS4231A accepts PIO sample
transfer, and `probes/VEWPLAY.C` is a working PIT-paced `.WAV` player
(see `probes/README.md` for the two protocol gotchas that make it work).
**FM has no hardware volume control** — the OPL3's audio (YMF262 → YAC512
DAC) is summed after the codec, and an exhaustive register hunt found
nothing that attenuates it (`doc/ASIC.md`).

## Documentation

* `doc/ASIC.md` — full software-visible map of the card's MEI DA65646 ASIC
  (address decode, CIS shadow behaviour, config registers including three
  undeclared vendor registers — one of which is the **EEPROM commit strobe**
  behind `/BURN` — COR index decode quirks, audio architecture), plus the
  raw probe output and a PDF rendering.
* `probes/` — the diagnostic programs that established all of the above.

## Build

Open Watcom, 16-bit real mode:

```
BUILD.BAT        (or: wcc -ms VEW21XGO.C && wlink system dos name VEW21XGO.EXE file VEW21XGO.OBJ)
```

## License

MIT — see [LICENSE](LICENSE).
