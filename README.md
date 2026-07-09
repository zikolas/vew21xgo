# VEW21XGO — a DOS enabler for the Panasonic CF-VEW211 PCMCIA sound card

A single-command DOS **point enabler** that brings the **Panasonic CF-VEW211**
PC Card sound card (Matsushita, 1994 — a WSS-style codec + OPL3 FM design) to
life with no Card Services and no Socket Services. It programs the PCMCIA host
controller and the card directly, then gets out of the way.

Its party trick: **it works even when the card's CIS is dead.** The unit this
was written for reads a stuck fill byte (`0x07`) across its entire tuple
region, so no Card Services stack or CIS-matching enabler can ever identify
it — but the card logic behind the broken CIS is perfectly healthy. VEW21XGO
carries the card's complete configuration table, taken from a known-good
CF-VEW211 CIS dump, and never needs to read the on-card CIS at all. An intact
card is matched by its MANFID (`0x0032/0x0001`); a broken one by its dead-CIS
signature (a uniform fill wall where tuples should be).

Clean-room: built from a known-good CIS dump, the public Intel 82365SL PCIC
register set, and the public AD1848/CS4231 codec + OPL FM programming models.
No vendor driver code. Developed and tested on a ThinkPad 235; it should work
on Intel 82365-class controllers generally.

## Features

| | Feature |
|:---:|---|
| ✅ | **OPL3 FM synthesis** at the standard AdLib port `0x388` — games just work |
| ✅ | **WSS codec** (AD1848/CS4231 family) mapped at `0x530` / `0xE80` / `0xF40` / `0x604` |
| ✅ | **Dead-CIS rescue** — enables a card whose CIS storage has failed |
| ✅ | **Mixer un-mute** — the codec powers up silent; VEW21XGO opens DAC + Aux paths |
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
VEW21XGO [/IO=530] [/I=0] [/BEEP] [/S=0..7] [/W=D000] [/OFF]
  /IO=hex    codec base — one of 530 (default) / E80 / F40 / 604 (picks the
             matching card config index; the codec registers sit at base+4,
             the OPL3 is always at 388)
  /I=dec     IRQ to route to the socket — 7, 9, 10 or 11 only (the card is
             level-mode only; default 0 = none, FM needs no IRQ)
  /BEEP      play a short FM test note after enabling
  /S=dec     socket 0..7 (default: auto-scan)
  /W=hex     attribute-window segment used to reach the card's config
             registers (default D000, auto-relocates if in use)
  /OFF       power the card's socket down and exit
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

WSS **digital** audio wants ISA DMA, which PCMCIA sockets don't have — so as
with other PC Card sound devices of the era, FM + mixer is the practical DOS
feature set. The codec itself responds fully (ID `0x8A`) if a PIO-mode driver
wants to try.

## Build

Open Watcom, 16-bit real mode:

```
BUILD.BAT        (or: wcc -ms VEW21XGO.C && wlink system dos name VEW21XGO.EXE file VEW21XGO.OBJ)
```

## License

MIT — see [LICENSE](LICENSE).
