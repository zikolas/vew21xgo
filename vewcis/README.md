# VEWCIS — repairing a dead CIS

`VEWCIS.EXE` writes a known-good CIS back into a CF-VEW211 whose EEPROM
load has failed, and nothing else: no COR write, no I/O mapping, no
mixer. The enabler never writes the CIS; repair lives here. The model
must be given, because a dead card cannot say what it is.

```
VEWCIS /211            volatile heal: inject the image into the RAM
                       shadow; the card is self-describing until the
                       next power-down, the EEPROM is untouched
VEWCIS /211 /BURN      permanent: power-cycle to read the true EEPROM
                       state, inject if dead, pulse the ASIC's EEPROM
                       commit strobe (attribute 0x204 bit 0), power-cycle
                       again and verify the EEPROM reloads the image on
                       its own. Refuses a card that is already healthy.
VEWCIS /211 /RESTORE   like /BURN but unconditional: burn the reference
                       image even over a valid CIS
VEWCIS /?              usage (also /H, /HELP)
```

> **`/BURN` and `/RESTORE` are permanent.** They are only reversible if
> you hold a byte-exact CIS image of *that* card. The commit strobe is a
> feature of the MEI ASIC, not of the 211, so a dead J04, 212 or JSC101
> reads as the same uniform fill and would accept a 211 image and carry
> the wrong identity afterwards. Be sure of the card before burning; if
> in doubt capture its attribute memory first and prefer the volatile
> heal.
>
> Never conclude a CIS is dead from one quick read. Attribute memory
> lags socket power by a host-specific time (up to ~110 ms measured) and
> the PCIC READY bit can assert before the CIS reads true. VEWCIS and the
> enabler gate every post-power read on the data itself since 2.4, so a
> DEAD verdict from a current build is trustworthy.
>
> VEWCIS recognises a healthy 212 by MANFID only to refuse every write on
> it: no commit mechanism has been demonstrated on that ASIC.

The embedded reference image is a byte-exact capture from a healthy unit
(`../probes/CIS_211_PRISTINE.BIN`). The 211 this project was written for
was repaired exactly this way; the story is in `../doc/ASIC.md`.

## Build

Open Watcom, 16-bit real mode, on a DOS box:

```
BUILD            (= BUILD VEWCIS; any other name builds that .C)
```

GPL v2 — see [LICENSE](../LICENSE).
