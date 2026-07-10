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

- **CIS_GOOD.BIN** — byte-exact 256-byte CIS image from a healthy CF-VEW211
  (also embedded in VEW21XGO for the shadow self-heal; suitable for
  programming a replacement 93LC56 directly).
