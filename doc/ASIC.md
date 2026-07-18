# MEI DA65646 ASIC — software-visible map (Panasonic CF-VEW211)

Everything the host can see of the Matsushita custom ASIC (IC6, 100-pin QFP,
marked `(C)MEI DA65646...`), established empirically with `probes/VEWASIC.C`
on 2026-07-09 (IBM PC110, 82365SL-class PCIC, raw report: section outputs
reproduced below). The card was probed from a clean power-up so all "reset"
values are true power-on states.

## Address decode summary

| Space | Decode | Contents |
|---|---|---|
| Attribute `0x000–0x1FE` (even) | 256 bytes | **CIS shadow RAM** — host-writable, volatile |
| Attribute `0x200–0x208` (even) | 5 registers | config register file (see below) |
| Attribute `0x20A+` | none | reads `0xFF` |
| Attribute, whole space | **mirrors every 0x1000** | only A0–A11 decoded |
| Common memory | **nothing** (0–256 KB scanned) | reads `0xFF`, no write latch — the 32K SRAM (CXK58257) is *not* host-visible |
| I/O `base+0 … base+3` | reads `0x00`, no bits latch | ASIC ports — write-only or unimplemented (function unknown) |
| I/O `base+4 … base+7` | CS4231A | IAR / IDR / Status / PIO data |
| I/O `base+8, base+9` | **no decode** (`0xFF`) | despite the CIS declaring a 10-byte range |
| I/O `0x388–0x38B` | YMF262 (OPL3) | always mapped, in every config index |

## CIS shadow

* Loaded from the on-board 93LC56 EEPROM at socket power-up. On this unit the
  load fails and the shadow fills with a constant `0x07`.
* Host writes to even attribute addresses `0x000–0x1FE` latch immediately and
  read back; contents survive warm reboots (card keeps power) but not socket
  power-down. Nothing above `0x1FE` accepts writes except the config registers.
* Writes do **not** reach the EEPROM (verified: power cycle discards them).
  No write-through/unlock mechanism found yet — candidates are the vendor
  bits below. Accurite BURNER (HeadstartCard Z86M17 protocol) does not reach
  it either (verified negative).

## Config register file (attribute space, even addresses)

Power-on state: `C0 00 00 00 00` then `FF...` from 0x20A.

| Addr | Name | Reset | Bits that latch | Notes |
|---|---|---|---|---|
| 0x200 | COR | **0xC0** | `0xEF` (bit4 never latches) | Powers up in SRESET (bit7) + LevlREQ (bit6). Bit6 reads back 1 **always** (hardwired level-mode). Index in bits 5–0. |
| 0x202 | CCSR | 0x00 | `0x08` only | **Only the Audio bit exists.** IntrAck/Intr/PwrDwn/etc. unimplemented. Audio bit gates mixer audio onto #SPKR (confirmed audibly). |
| 0x204 | vendor | 0x00 | `0x01` (1 bit) | **EEPROM COMMIT STROBE** (isolated by probes/VEWSTRB.C, 2026-07-10): pulsing bit0 writes the **entire 256-byte CIS shadow** back to the 93LC56. Pure strobe — no shadow writes needed while set; verified across power cycles. Panasonic's factory programming hook, and the mechanism behind the accidental repair. Used by `VEWCIS /BURN`. |
| 0x206 | vendor | 0x00 | `0xFE` (7 bits!) | **Undeclared.** Seven writable bits — prime candidate for an FM attenuator / volume DAC or control latch. |
| 0x208 | vendor | 0x00 | `0x0F` (4 bits) | **Undeclared.** Function unknown. |

The `0x20A+` region reads a stuck `0xFF` (not registers; a naive bit-set test
false-positives there).

## COR index decode

Walked with the codec probed at each index's expected base:

| Index | Codec decodes? | COR readback | Note |
|---|---|---|---|
| 0x00, 0x01, 0x10 | no | 0x40 / 0x41 / 0x40 | unconfigured / undefined |
| 0x20 | yes @ 0x530 | 0x60 | default entry |
| 0x21 | yes @ 0xE80 | 0x61 | |
| 0x22 | yes @ 0xF40 | 0x62 | |
| 0x23 | yes @ 0x604 | 0x63 | |
| **0x24** | **yes @ 0x530** | 0x64 | **bit2 of the index is ignored** — decode is partial |
| 0x2F, 0x3F | not at 0x530 | 0x6F (bit4 dropped) | consistent with COR bit4 not implemented |

So the ASIC compares roughly `index & 0x23`: bit5 = "configured", bits 1–0
select the base. The full config table from the CIS is validated in silicon.

## Confirmed audio architecture

* **PCM**: host → CS4231A PIO (see VEWPLAY notes: PRDY unusable for pacing,
  status-read commits each sample) → codec DAC → mixer → output amp. Volume
  via codec (I6/I7 etc.) works over the full range.
* **FM**: YMF262 → YAC512 DAC → analog summed **after** the codec into the
  output amp. Unaffected by every codec mixer control (proven by muting all
  inputs incl. MODE2 LINE/MONO). No volume control found yet — the vendor
  bits at 0x204/0x206/0x208 and the write-only ports base+0..+3 are the
  remaining candidates (board has a TC4W66F analog switch in the output
  section that something must drive).
* **#SPKR**: CCSR bit3 → 1-bit audio onto the socket's speaker pin (host
  side: PCIC Misc Ctl 1 bit4 routes it to the laptop speaker). Fixed level,
  harsh by nature.

## Listening pass results (2026-07-09, VEWVND v1/v2)

All seven unknown-control candidates were swept audibly (FM test note held,
values stepped and every bit toggled individually):

* vendor regs 0x204 / 0x206 / 0x208 — **no audible effect** on FM or PCM
* write-only I/O ports base+0 … base+3 — **no audible effect**

With the codec mixer previously ruled out, this exhausts the card's visible
register space: **the CF-VEW211 has no hardware FM volume control.**
Cross-checked on a second, known-good unit (2026-07-11): FM is equally loud
there under both the period vendor driver and VEW21XGO — unit-independent,
by design, not a fault of the repaired specimen. The Windows-era "FM volume"
slider (observed working on Win98) is driver-side TL scaling.

## The accidental EEPROM repair (2026-07-10)

After the VEWVND bit sweeps, the card began **loading a valid CIS from the
EEPROM on every cold power-up** — verified byte-for-byte against the good
image across multiple power cycles including a 45-second unpowered dwell.
Every power-up before the sweeps (dozens, over two days) produced the 0x07
dead fill.

Conclusion: **an EEPROM write path exists**, and one of the undocumented
controls swept by VEWVND v1/v2 (vendor registers 0x204 / 0x206 / 0x208, or
the write-only ports base+0..+3) acts as a *commit-shadow-to-EEPROM* strobe
— presumably Panasonic's factory programming hook. The sweep happened while
the known-good CIS was resident in the shadow, so the good image was burned
in: the card is now permanently repaired.

**RESOLVED (2026-07-10): the strobe is attribute 0x204 bit0.** Isolated by
`probes/VEWSTRB.C` — a tracer-byte search that kept the good CIS resident
in the shadow at all times, so any accidental commit only re-burned a good
image. First candidate hit; semantics refined to a **pure whole-shadow
commit on pulse** (tracer planted *before* the pulse, no writes while set,
still committed). Pristine image restored and verified across multiple
cold power cycles afterwards.

The repair recipe is therefore: *fill shadow → pulse 0x204 bit0 → wait ~3 s
→ power-cycle to verify* — implemented as **`VEWCIS /BURN`**, a complete
software-only permanent CIS repair for this card family (with a safety
rail: it power-cycles first and refuses to burn a card whose EEPROM already
loads a valid CF-VEW211 CIS). The standing warning remains: **never pulse
0x204.0 while the shadow holds garbage.**

## The combination-locked hidden register bank (2026-07-11)

The vendor DOS driver's register state (`DUMP-VND.TXT` vs our `DUMP-OUR.TXT`)
revealed that Panasonic's software programs the "function unknown" vendor
registers — and that under its configuration, the missing I/O bytes at
base+8/+9 decode (the CIS's 10-byte range is real after all).

Reproduced and bisected on a clean boot: **base+8/+9 decode if and only if
[206] = 0x38 AND [208] = 0x05 simultaneously** — a two-register combination
lock; clearing either re-locks the pair to 0xFF. (16-bit I/O window settings
are irrelevant.) This is why every single-register sweep missed it: the
unlock itself is silent, and the combination only existed for an instant
during the matrix sweep with nobody reading the ports.

Unlocked bank contents so far:

* **base+8**: upper nibble reads a fixed 0xA (ID/signature); low nibble has
  **three latching control bits (0, 2, 3)**; bit 1 accepts writes but never
  reads back (write-only or strobe).
* **base+9**: reads a constant 0xBC; deaf to writes so far.
* Audible effect of every +8 bit and value, FM note held (VEWHID
  interactive test): **none found**. Function unknown — candidates: factory
  test hooks, power management, or controls for paths we haven't exercised.
* The vendor driver also runs with CCSR audio ON (inside speaker), IRQ 9,
  16-bit I/O windows, DAC muted-until-playback, Aux/LINE muted (matching
  our v1.4 policy) — see the two dump files in this directory.

**Warning for future probing: never run register experiments with Card
Services resident** — SS/CS polls the PCIC via the shared index/data pair
and races every access (this produced phantom results — unlatchable
registers, impossible status reads — that cost a full evening). `MEM /C`
first, always.

## The CS4231A-KQ write-timing quirk (2026-07-10)

This card's codec **silently drops microsecond-paced MCE format sequences
issued from a cold codec** — the same bytes land fine written slowly. This
single quirk masqueraded for days as: cold-boot silence at 22 kHz, an
"8 kHz kickstart" ritual, level-independent distortion ("uncalibrated DAC"
sound), a thermal/dying-crystal theory (retracted — XTAL2 is fine), and a
suspected voltage event (retracted). Fix (VEWPLAY v5): pace MCE sequences
in milliseconds and **verify every register write with read-back + retry**.
The NEC J04 sibling's codec batch tolerates fast writes — pure silicon
lottery. Rule for all CS4231 code on these cards: never trust a write you
didn't read back.

## Open questions

1. ~~Which control is the EEPROM commit strobe~~ — **answered: 0x204 bit0**.
2. ~~What 0x206/0x208 do~~ — **answered in part: together they are the
   combination lock for the hidden base+8/+9 bank** (0x38/0x05). Whether
   other values have additional meaning: unknown.
3. ~~Why the CIS declares 10 I/O bytes but the card decodes 8~~ —
   **answered: all 10 decode once the combination lock is set** (base+8/+9
   are the hidden bank).
4. ~~FM volume~~ — **CLOSED: no hardware control exists** — exhaustively
   confirmed (codec incl. XCTL pins, all vendor registers incl. the hidden
   bank, all ports, both by sweep and by the vendor driver's own super-loud
   FM), and finally cross-checked on a **known-good unit** (2026-07-11):
   equally loud under vendor driver and VEW21XGO. Windows-era volume was
   driver-side TL scaling.
5. What the hidden bank's three latching bits + write-only bit actually
   control (not FM level; not audibly anything yet).
6. Whether any register drives the TC4W66F analog switch, or whether it's
   hard-wired (e.g. to the CCSR audio bit / #SPKR path).

## Raw report

See `VEWASIC.C` output (ASIC.TXT) captured 2026-07-09; rerunnable any time —
the probe is idempotent and leaves the card ready for VEW21XGO to re-enable.
