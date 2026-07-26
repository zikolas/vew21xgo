# FMVOL — giving the CF-VEW211 an FM volume control it does not have

The Panasonic CF-VEW211's FM synthesis is **deafening**, and there is no
way to turn it down. Not "we couldn't find one" — there genuinely isn't
one. This is the story of why, and of the software that fixes it anyway.

## The hardware problem

The card has two independent audio paths that meet late:

```
   CS4231A codec  ──── DAC, mixer, volume ────┐
                                              ├──►  output amp ──► line out
   YMF262 (OPL3) ──► YAC512 DAC ──────────────┘
```

PCM audio goes through the codec, which has a perfectly good volume
control (`/VOL` in the enabler drives its DAC attenuation). **FM does
not.** The YMF262's digital output is converted by a YAC512 and summed
into the output amp *after* the codec entirely — so every mixer register
in the CS4231A is powerless over it. Muting the codec's DAC, Aux1, Aux2,
Line, Mono and even MOM leaves the FM at full volume.

That leaves the two chips themselves, and neither offers anything:

* The **YMF262 has no master volume register.** The OPL3 programming
  model simply doesn't include one. Level is a property of each
  *operator*, not of the chip.
* The **YAC512 is a dumb serial DAC** — no attenuation input, no gain
  pin, nothing programmable.

We went looking anyway, exhaustively:

| Searched | Result |
|---|---|
| Every CS4231A mixer input, including XCTL0/XCTL1 pins | no effect |
| Undeclared vendor registers at attr `0x204` / `0x206` / `0x208`, all values and individual bits | no effect |
| The hidden register bank at `base+8/+9` (behind the `0x206=0x38` + `0x208=0x05` combination lock) | no effect |
| Write-only ports `0x530`–`0x533` | no effect |
| A second, known-good CF-VEW211 | equally loud |
| Panasonic's own DOS driver | equally loud |

The last two matter most: this is **design, not damage**. The card was
built this way. (The Windows driver's "FM volume" slider turned out to be
software scaling the very registers described below.)

Full detail in [`ASIC.md`](ASIC.md).

## The one remaining lever

OPL level lives in each operator's **Total Level** register — `40h`–`55h`,
six bits, 0.75 dB per step, 0 = loudest and 63 = silent.

The catch is ownership: TL belongs to whatever program is playing. A game
rewrites it constantly — on every instrument change, often per note for
velocity. Setting it beforehand achieves exactly nothing; the game
overwrites it microseconds later.

**So the fix is to stop trying to set it, and start editing it in
flight.**

## The software solution

`FMVOL.DLL` is a Jemm Loadable Module — a small handler that runs at
ring 0 inside **JEMM386**. It traps the four OPL ports (`388h`–`38Bh`)
and rescales every Total Level write on its way to the chip:

```
   game ──► OUT 389h ──► [trap] ──► is this a carrier's Total Level?
                                       yes → add attenuation, clamp at 63
                                       no  → pass through untouched
                                    ──► real chip
```

Everything that is not a Total Level reaches the chip byte-for-byte, so
games sound exactly as they should — just quieter, by a number you pick.

### Carriers only — the part that matters

You cannot simply attenuate every operator. In FM synthesis a
**modulator** shapes the *timbre*; turning it down doesn't make the note
quieter, it makes it a different instrument. Only **carriers** reach the
output, and only their levels are volume.

Which operators are carriers depends on each channel's algorithm, so
FMVOL shadows the chip's configuration as the game writes it:

| Register | Watched for |
|---|---|
| `C0h`–`C8h` | connection bit (CNT): 0 = FM chain, 1 = additive |
| `BDh` bit 5 | rhythm mode |
| `104h` | OPL3 4-operator mode |

and applies the rules:

* **2-operator** — op2 is always a carrier; op1 only when the channel is
  additive (CNT=1).
* **Rhythm mode** — hi-hat, snare, tom and cymbal are single-operator
  sounds, so every one of their operators is heard. The bass drum stays
  an ordinary pair.
* **4-operator** — op4 always; op1 with CNT1; op2 with CNT2; op3 only
  when both are set.

When a connection bit changes, that channel's two levels are re-sent with
the correct scaling and the game's own register latch is restored, so the
extra writes are invisible to it.

### Using it

```
FMGO                 enable the card, load JEMM386, load the trap at 16 steps
FMGO 32              ... at 32 steps

JLOAD FMVOL.DLL 24   load directly, 24 steps
JLOAD -u FMVOL.DLL   unload; the card returns to its own levels
```

Attenuation is `0`–`63` in 0.75 dB steps: **8 ≈ −6 dB, 16 ≈ −12 dB,
24 ≈ −18 dB, 32 ≈ −24 dB**. `0` traps without scaling, which is useful as
a clean A/B. There is no live API — changing the level means unload and
reload.

### Verified

* Test tone with the trap unloaded: audible. At 63 (maximum): **silent**.
* **Monkey Island 2 at 32 steps: sounds correct, just quieter** — the
  real proof, because a mistake in carrier detection shows up as muffled
  or altered instruments long before it shows up as wrong volume.

## Limitations

**1. Real-mode games only.** This is the significant one. A JLM traps
V86 and real-mode guests. Games running under a DOS extender —
DOS/4GW, DOS32A, PMODE/W and friends — execute in protected mode and
their port writes never reach the trap. FMVOL will appear to do nothing
at all.

| Works | Does not work |
|---|---|
| Monkey Island 1 & 2, most SCUMM | DOOM, Duke Nukem 3D, Descent |
| Wolfenstein 3D, Commander Keen | Quake, Warcraft II |
| Prince of Persia, Lemmings | anything printing `DOS/4GW` at startup |
| Sierra AGI/SCI titles | |

A quick test: if the game shows a DOS extender banner when it launches,
the trap cannot see it.

**2. JEMM386 must be loaded**, which costs conventional memory and rules
out running another memory manager (EMM386, QEMM) at the same time.

**3. Only the `388h`–`38Bh` FM ports are trapped.** That is the whole
story on this card, whose OPL3 is always at `388h`, but a card exposing
FM at an SB-base alias would need those ports adding.

**4. Trapping costs time.** Every OPL access becomes a fault into ring 0
instead of a bus cycle. It is imperceptible for music, but this is not a
mechanism you would want on a port being hammered in a tight loop.

**5. Rhythm and 4-operator handling is implemented but lightly tested** —
most DOS music is plain 2-operator, so those paths have had less
exercise by ear.

## Fixing the protected-mode limitation, in future

The V86 trap is only half of the picture, and the other half is a known
quantity — our own notes from the CD-20X work already describe it, and
SBEMU solves the same problem in the same place:

> The right home is the trap plumbing that **dual-installs every trap**:
> the V86 trap for real-mode guests, and a protected-mode port trap for
> extended ones.

Concretely, the route is:

1. **Load `HDPMI32` as the DPMI host** so extended games use it instead
   of the extender's own built-in DPMI. HDPMI provides a port-trapping
   interface for exactly this purpose.
2. **Register the same handler twice** — the existing JLM path for V86
   guests, plus an HDPMI protected-mode trap on the same four ports —
   sharing one copy of the shadow state and the carrier logic, which is
   already independent of how the write was intercepted.

The scaling engine needs no changes at all; only the plumbing that
delivers writes to it. That keeps the interesting part — knowing which
operator is a carrier — in one place.

A cruder fallback, if the dual-trap proves awkward: a **TL-scaling
resident that hooks the timer** and periodically re-asserts scaled levels
rather than intercepting writes. It would fight the game slightly and
lose per-note velocity on scaled operators, but it works regardless of
CPU mode. This is the approach the vendor's Windows driver effectively
took, and it is the reason their slider worked where the hardware had
nothing to offer.

## Building

The DLL is portable — a target machine needs only `JEMM386.EXE` and
`JLOAD.EXE`. Only the *build* needs tools:

```
./fmbuild.sh          on the host: JWasm + Open Watcom wlink
FMBLD.BAT             on a DOS box: JWASMD + WLINK
```

Two things that will bite otherwise:

* **`wlink` emits a plain `PE\0\0` signature; JLOAD only accepts JLMs
  marked `PX\0\0`.** Both build paths patch the byte at file offset
  `0x79` afterwards.
* **Build before loading JEMM386.** JWASMD needs a DPMI host and fails
  with `HDPMI32: insufficient memory` once JEMM is resident. The host
  build sidesteps this entirely.

## Credits and provenance

Written from scratch for this project. The JLM entry points and I/O-trap
interface follow Jemm's documented `JLM.INC`; the OPL2/OPL3 register
model is the public Yamaha programming model; the module is patterned on
our own `CDXMIR` JLM from the CD-20X work. No vendor driver code was read
or disassembled.

MIT — see [LICENSE](../LICENSE).
