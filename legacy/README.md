# legacy — the original 1.x C point enabler

`VEW21XGO.C` here is the **original PCIC-only** DOS point enabler this
project grew from (Open Watcom, 16-bit real mode). It is kept for
reference and history; it is **superseded** by the unified assembly
enabler `../VEW21XGO.ASM` (→ `VEW21XGO.COM`), which adds the Card
Services and OmniBook Socket Services backends and both card models.

Notable difference from the current tool: 1.x (versions 1.3–1.4) healed a
dead CIS automatically on every run. That behaviour moved out to the
standalone `../VEWCIS.C` when the 2.x enabler took over — an enabler
silently writing a possibly-wrong identity into a card it cannot actually
identify stopped being wise once a second card model existed.

The compiled `VEW21XGO.EXE` is intentionally **not** committed; build it
from this source if you need it:

```
wcc -ms VEW21XGO.C -fo=VEW21XGO.OBJ
wlink system dos name VEW21XGO.EXE file VEW21XGO.OBJ
```
