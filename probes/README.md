# probes

The diagnostic programs used to reverse-engineer the CF-VEW211 live over
COMrade, kept because they document how the card was figured out. Not needed
to use `VEW21XGO.EXE`. Each builds the same way: `C:\WATCOM\BLD <name>`.

- **VEWPIO.C** — does DMA-less PIO playback work? Port of the SCP-55's CSPIO2
  probe (verdict there: no). Verdict here: **yes** — the FIFO drains and
  fast-feed leaves no underrun. Also exposed that **PRDY reads "ready"
  continuously** while overfed bytes are dropped, so PRDY cannot pace
  playback on this card.

- **VEWPLAY.C** — PIT-paced PIO `.WAV` player, and the audible proof.
  Two hard-won rules baked in: pace by the **PIT** (calibrated against the
  BIOS tick — mode 3 counts down by two), never by PRDY; and read the
  **Status register after every sample write** — per the CS4231A datasheet
  the SR read is what commits the sample to the FIFO, and writes after a
  completed sample are ignored without it. Plays 8/16-bit mono/stereo PCM
  at the nearest supported rate.

- **VEWMIX.C** — interactive TUI mixer (arrow keys, per-channel bars, F holds
  an FM test note, T plays a WAV). Used to prove the FM path **bypasses the
  CS4231A entirely**: FM survives muting DAC, Aux1, Aux2, LINE, MONO (incl.
  MODE2 bypass off) — while PCM tracks the DAC attenuator perfectly.

- **VEWASIC.C** — systematic map of the MEI DA65646 ASIC: attribute decode +
  mirrors, CIS shadow extent, config-register latch tests (found the three
  undeclared vendor registers), COR index walk, common-memory scan, I/O
  footprint. Full results in `../doc/ASIC.md`.

- **VEWVND.C** — interactive explorer for the unknowns VEWASIC found: vendor
  registers 0x204/0x206/0x208 and the write-only I/O ports base+0..+3.
  Listening-test verdict: none audibly affect FM in isolation. Plot twist:
  a sweep performed while the good CIS was resident in the shadow
  **accidentally programmed the dead EEPROM back to health** — see VEWSTRB.
  (The Windows vendor driver proves an FM volume *does* exist; a
  cross-product routing test is the planned round 2.)

- **VEWSTRB.C** — isolated the mechanism behind that accident: a tracer-byte
  search across all 16 swept controls, keeping the good CIS in the shadow
  at all times so a hit could only re-burn a good image. Verdict:
  **attribute 0x204 bit0 is a whole-shadow EEPROM commit strobe** (pure
  strobe — a pulse commits all 256 bytes, no writes needed while set).
  This is the register behind `VEWCIS /BURN`.

- **VEWFM.C** — FM-volume hunt round 3: automated 0x208×0x206 matrix +
  write-only-port ramps, millisecond-paced verified writes, all codec
  inputs opened so a routing switch would be audible. Verdict: no effect.

- **VEWXCTL.C** — round 4 (and the cross-product playground): the CS4231A's
  XCTL0/XCTL1 external-control pins (I10) in all states, live alongside
  the vendor registers and codec inputs in one TUI. Verdict: no effect —
  which, combined with the vendor driver's own super-loud FM, finally
  settles it: **the card has no hardware FM volume control**.

- **VEWDUMP.C** — one-shot register state snapshot (PCIC, attribute/config
  regs, I/O ports, full codec I0–I31) for diffing configurations. Diffing
  our enabler against the period vendor driver (`../doc/DUMP-OUR.TXT` vs
  `DUMP-VND.TXT`) revealed the vendor values in 0x206/0x208 — and the
  hidden register bank they unlock.

- **VEWHID.C** — interactive explorer for that hidden bank: base+8/+9
  decode only while [206]=0x38 AND [208]=0x05 (a two-register combination
  lock). +8 = three latching bits under a fixed ID nibble; +9 = constant
  0xBC so far. Audible function: none found; purpose unknown.

- **CIS_GOOD.BIN** — byte-exact 256-byte CIS image from a healthy CF-VEW211
  (also embedded in VEW21XGO for the shadow self-heal; suitable for
  programming a replacement 93LC56 directly).

- **CIS_TEST.BIN** — the "Hello from Claude 2026!" marker variant (VERS_1
  string replaced in-place, CISTPL_CHECKSUM corrected) used to independently
  verify the EEPROM write path end-to-end. `VEWCIS /RESTORE` undoes it.

- **CIS_PC98_J04.BIN** — reference CIS dumped (read-only) from the NEC
  PC-9801N-J04, the PC-98 sibling of this card: same ASIC + codec, no FM
  synth fitted, single config entry at 0xF40 with PC-98 IRQ numbering,
  and no MANFID tuple at all.

- **JSCPROBE.C** — the CF-JSC101 bring-up probe: power with RESET held,
  read the CIS head, program any of the ASIC's four config blocks
  (`/COR= /COR1= /COR2= /IOBASE2= /COR3=`), codec ID, OPL timer test,
  `/DING`, `/SWEEP` (prints each index before touching it — 30h stalls
  the bus), `/KEEP` and `/OFF`. Found that block 1 (attribute 0x440)
  carries the OPL3 and the codec's clock.

- **JSCDUMP.C** — interrupt-safe snapshot of what another enabler left
  behind (PCIC registers, I/O windows and their ports, codec I0–I31,
  attribute 0x000–0x1FF and 0x400–0x4FF through a borrowed window).
  Run under the vendor's SNSCDOSV.SYS + IBM Card Services, it exposed the
  four-block layout that VEW21XGO 2.8 replicates.

- **CIS_JSC101.BIN** — byte-exact 512-byte CIS image from the CF-JSC101
  (CONFIG and CFTABLE sit behind a LONGLINK_A at attribute 0x94).
