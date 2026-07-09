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
| 0x204 | vendor | 0x00 | `0x01` (1 bit) | **Undeclared in CIS.** Function unknown. |
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
register space: **the CF-VEW211 appears to have no hardware FM volume
control.** To be confirmed against the period Windows driver on a good-CIS
card (pending — if its "FM" slider works, watch what it writes; if it maps
to codec Aux1, it never worked on this card's topology either).

## Open questions

1. What vendor bits 0x204.0, 0x206.7–1, 0x208.3–0 actually do (they latch
   but audibly do nothing). Still candidates for an EEPROM write-enable —
   persistence pass (set bit → write shadow → power cycle) not yet done.
2. Same for the write-only I/O ports base+0..+3.
3. Why the CIS declares 10 I/O bytes but the card decodes 8.
4. Whether any register drives the TC4W66F analog switch, or whether it's
   hard-wired (e.g. to the CCSR audio bit / #SPKR path).
5. Windows-driver behaviour of the "FM volume" slider (needs the good-CIS
   card).

## Raw report

See `VEWASIC.C` output (ASIC.TXT) captured 2026-07-09; rerunnable any time —
the probe is idempotent and leaves the card ready for VEW21XGO to re-enable.
