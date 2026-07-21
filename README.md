# VEW21XGO — a DOS enabler for the Panasonic CF-VEW211 / CF-VEW212 PCMCIA sound cards

A single-command DOS **enabler** for the Matsushita/Panasonic PCMCIA sound
cards: the **CF-VEW211** (1994 — CS4231A WSS codec + YMF262 OPL3) and the
**CF-VEW212 "Sound Card PRO"** (same ASIC family, plus an OPL4 wavetable).
One ~10 KB `.COM`, three host backends, auto-detected:

| Mode | Host | Status |
|---|---|---|
| `/PCIC` | Intel 82365-class controllers (IBM PC110, ThinkPad 235…) — no Card Services, no Socket Services needed | the 1.x-proven path, ported |
| `/CS` | any PCMCIA Card Services 2.1 stack (SystemSoft lineage) | registers as a CS client and **stays TSR**: hot-plug configures on insert, a later run live-reconfigures through the resident copy, `/OFF` releases |
| `/OB` | HP OmniBook 300/425/430 ROM Socket Services, no CS needed | **verified live on a 425** — polite window allocator, all the probed SS quirks baked in (incl. the 10-bit I/O window aliasing this card needs) |

With no mode switch the host is auto-detected: Card Services first (if a CS
arbiter is loaded we must go through it), then the OmniBook `SS` signature,
then an 82365 probe at 3E0h.

It also still handles its founding case: **a card whose CIS is dead** (a
failed EEPROM load — the tuple region reads one stuck fill byte, so no
CIS-matching software can ever identify it). Such a card is recognized by
its uniform-fill signature and reported — but because a blank CIS is *not*
proof the card is a VEW21x (any card's failed EEPROM reads identically),
it is only configured under **`/FORCE /S=n`**, from the built-in
configuration, **read-only**. The companion `VEWCIS` tool repairs the CIS
permanently, in software (see below).

Clean-room: built from healthy cards' own CIS dumps, the public Intel
82365SL register set, the PCMCIA CS/SS specs (RBIL + the SystemSoft
CardSoft technical guide), live-probed OmniBook Socket Services behavior,
and the public Crystal CS4231A + OPL FM programming models. No vendor
driver code.

## Usage

Run it once. In PCIC and OB modes the configuration sticks in the
controller and the program exits; in CS mode it stays resident and also
configures the card on hot-plug. Then point your game at **AdLib at 388**:

```
VEW21XGO /T
```

With no `/S`, VEW21XGO scans the sockets and configures the first
CF-VEW211/212 it finds, tagged `(auto)` in its output. (A card with a
dead/blank CIS is reported but not configured unless you add `/FORCE`.)

```
VEW21XGO [/PCIC|/CS|/OB] [/IO=530] [/I=0] [/VOL=24] [/T[ONE]] [/SPKR]
         [/NOFM] [/S=n] [/W=D000] [/F[ORCE]] [/OFF] [/?]

  /IO=hex   codec base — 530 (default) / E80 / F40 / 604 (picks the
            matching COR index; codec registers at base+4, OPL3 always
            388). The alternates are 211-only: a CF-VEW212 declares just
            the 530 configuration and is forced to it, with a note.
  /I=dec    IRQ to route — 7, 9, 10 or 11 only (level-mode cards;
            default 0 = none, FM needs no IRQ)
  /VOL=dec  DAC (PCM) attenuation, 1.5 dB per step, 0 (full, clips the
            card's output amp) .. 63; default 24 = -36 dB by ear
            (line-out users into amplified speakers may prefer /VOL=8)
  /TONE     play a ~1 s FM test tone after enabling (a quiet bell ding —
            deliberately, because FM has no hardware volume on this card)
  /SPKR     also route the card's audio to the host's internal speaker
            (CCSR Audio bit -> #SPKR pin; PCIC mode adds the bridge-side
            route; mono, 1-bit, harsh by nature)
  /NOFM     don't claim the 388h FM window
  /S=dec    socket (PCIC 0-7; OB 1-2 — 3/4 hold the OmniBook's permanent
            storage cards and are never probed; CS: pin to this socket)
  /W=hex    attribute-window segment for the CIS/COR access (PCIC;
            default D000, auto-relocates if another card is mapped there)
  /FORCE    configure without the CIS identity check — also the only way
            to enable a card whose CIS is dead/blank (needs /S)
  /OFF      PCIC: power the socket down; CS: release + go dormant
  /?        show this usage (also /H, /HELP)
```

Note DOS runs `.COM` before `.EXE`: drop `VEW21XGO.COM` next to the old
1.x `.EXE` and the unified enabler takes over the name (delete or rename
the `.EXE` to avoid confusion).

## Features

| | Feature |
|:---:|---|
| ✅ | **Three host backends in one binary** — direct PCIC, Card Services client (hot-plug TSR), OmniBook Socket Services |
| ✅ | **Both cards** — CF-VEW211 and CF-VEW212 "Sound Card PRO", matched by MANFID |
| ✅ | **OPL3 FM synthesis** at the standard AdLib port `0x388` — games just work |
| ✅ | **WSS codec** (CS4231A) at `0x530` (211 also: `0xE80` / `0xF40` / `0x604`) |
| ✅ | **Dead-CIS cards recognized** — flagged by their fill signature; configured from built-in knowledge only under `/FORCE`, never written to |
| ✅ | **PCM volume** (`/VOL`) — DAC attenuation in 1.5 dB steps |
| ✅ | **Mixer un-mute** — the codec powers up silent; write-verified, retried ms-paced (cold-codec quirk) |
| ✅ | **Host-speaker audio** (`/SPKR`) — via the PCMCIA #SPKR pin (1-bit, lo-fi by nature) |
| ✅ | **FM test tone** (`/T`) — instant audible proof the card is alive |
| ✅ | **Permanent CIS repair** — `VEWCIS /211|/212 /BURN` programs a known-good CIS back into the card's EEPROM |

## VEWCIS — the standalone CIS repair tool

`VEWCIS.EXE` heals the CIS and touches nothing else — no COR write, no I/O
mapping, no mixer. The enabler itself never writes the CIS (by design,
since 2.1): repair lives here, and because **a dead card cannot say which
model it is**, every operation that writes the shadow requires the model
on the command line:

```
VEWCIS /211            volatile heal: inject the selected CIS image into
  (or /212)            the shadow; card left powered, un-configured,
                       self-describing until next power-down
VEWCIS /211 /BURN      PERMANENT repair: power-cycles to read the true
                       EEPROM state, injects if dead, pulses the ASIC's
                       EEPROM commit strobe (attr 0x204 bit0 — the
                       factory programming hook found in recon), then
                       power-cycles again and verifies the EEPROM reloads
                       the pristine CIS on its own.  Refuses to burn a
                       card that is already healthy.
VEWCIS /211 /RESTORE   like /BURN but unconditional: burns the selected
                       reference image even over a valid CIS (undo test
                       images / factory-reset to the known-good dump)
VEWCIS /?              show usage (also /H, /HELP)
```

> ⚠️ **`/BURN` and `/RESTORE` write binding, permanent changes to the
> card's on-card EEPROM** — not the volatile RAM shadow. They are only
> reversible if you hold a correct byte-exact CIS image for *that exact
> card*. There is no undo.
>
> **This is not CF-VEW211/212-specific.** The EEPROM commit strobe (attr
> `0x204`) is a feature of the card's **MEI DA65646 ASIC**, and other
> cards built on that same ASIC respond to it identically — for example
> the NEC **PC-9801N-J04** sound card (same MEI ASIC + CS4231A, different
> identity and config). VEWCIS recognises a *healthy* card by its MANFID,
> but a card whose CIS is **dead reads as a uniform fill no matter what
> card it is** — so a `/BURN` or `/RESTORE` aimed at a dead card you have
> **not independently confirmed to be a CF-VEW211/212** can permanently
> stamp a *different* MEI-ASIC card with a VEW211/212 identity.
>
> Before burning: be sure the card is a CF-VEW211/212. If in any doubt,
> **capture its current CIS first** (a raw attribute-memory dump) and keep
> it, and prefer the **volatile heal** (`/211` or `/212` with no `/BURN`)
> — that leaves the EEPROM untouched and evaporates on power-down.

The 211 unit this project was written for was successfully repaired
exactly that way (the first time by accident — long story, documented in
`doc/ASIC.md`).

Both reference images are **byte-exact captures from healthy units**
(`probes/CIS_211_PRISTINE.BIN`, `probes/CIS_VEW212.BIN`; the older
third-party 211 dump is kept as `probes/CIS_GOOD.BIN` for history). A 212
quirk the image deliberately preserves: its CIS shadow is 128 dense bytes
*mirrored* —
the card presents bytes 128–255 as a repeat of 0–127 — so the embedded
image carries the mirror, making injection alias-safe even if writes
alias the same way (`probes/CIS_VEW212.TXT` has the full story).

## The cards

| Card | MANFID | Config index | Codec base | FM |
|---|:---:|:---:|:---:|:---:|
| CF-VEW211 | `0032/0001` | `0x20` (default) | `0x530` | `0x388` |
| | | `0x21` | `0xE80` | `0x388` |
| | | `0x22` | `0xF40` | `0x388` |
| | | `0x23` | `0x604` | `0x388` |
| CF-VEW212 | `0032/0501` | `0x20` (only) | `0x530` | `0x388` |

Config registers (COR + CCSR) live at attribute offset `0x200` on both.
The COR reads back with the LevlREQ bit pinned high — these cards only do
level-mode interrupts, hence the `{7, 9, 10, 11}` IRQ set from the CIS.
The 212's lone config entry is byte-identical to the 211's default.

**Digital audio works — without DMA.** The CS4231A accepts PIO sample
transfer, and `probes/VEWPLAY.C` is a working PIT-paced `.WAV` player
(see `probes/README.md` for the protocol gotchas that make it work).
**FM has no hardware volume control** on the 211 — the OPL3's audio
(YMF262 → YAC512 DAC) is summed after the codec, and an exhaustive
register hunt found nothing that attenuates it (`doc/ASIC.md`).

The 212 additionally carries an **OPL4 (YMF278) MIDI wavetable** — its
register mapping is an ongoing recon effort (`probes/CIS_VEW212.TXT` has
the plan; the prime suspect for the wave register pair is the `base+8/+9`
region that on the 211 was an empty hidden register bank).

## History: the 1.x C enabler

The original PCIC-only point enabler this project grew from lives in
[`legacy/VEW21XGO.C`](legacy/) (Open Watcom), kept for reference — see
`legacy/README.md`. Its versions 1.3–1.4 healed a dead CIS automatically
on every run; that behavior moved to `VEWCIS` when the 2.x assembly
enabler took over (an enabler silently writing a possibly-wrong identity
into a card it cannot actually identify stopped being charming once a
second card model existed). The compiled 1.x `.EXE` is no longer
committed; build it from the archived source if you want it.

## Documentation

* `doc/ASIC.md` — full software-visible map of the 211's MEI DA65646 ASIC
  (address decode, CIS shadow behaviour, config registers including three
  undeclared vendor registers — one of which is the **EEPROM commit
  strobe** behind `/BURN` — COR index decode quirks, audio architecture),
  plus the raw probe output and a PDF rendering.
* `probes/CIS_VEW212.TXT` — the CF-VEW212 CIS capture, decode, and the
  OPL4 recon plan.
* `probes/` — the diagnostic programs that established all of the above.

## Build

The unified enabler (NASM, host or on-box):

```
./build.sh       (or: nasm -f bin VEW21XGO.ASM -o VEW21XGO.COM)
```

VEWCIS (Open Watcom, 16-bit real mode):

```
BUILD VEWCIS     (or just BUILD — VEWCIS is the default target)
```

The legacy 1.x enabler builds the same way from `legacy/VEW21XGO.C`.

## License

MIT — see [LICENSE](LICENSE).
