#!/bin/sh
# Build FMVOL.DLL on the host, no DOS box needed.
#
# The JLM only has to be BUILT with these tools; the .DLL itself is
# portable, so the target machine needs nothing but JEMM386 + JLOAD.
# (JWASMD exists on the PC110 but not the 235 - this avoids caring.)
set -e
JWASM=${JWASM:-$HOME/tools/jwasm-src/build/GccUnixR/jwasm}
WLINK=${WLINK:-$HOME/tools/ow2/armo64/wlink}

"$JWASM" -nologo -coff -Fo=FMVOL.obj FMVOL.ASM
"$WLINK" @FMVOL.LNK

# wlink emits a plain PE signature; JLOAD only accepts JLMs marked PX.
python3 - <<'PY'
d = bytearray(open('FMVOL.DLL','rb').read())
e = int.from_bytes(d[0x3c:0x40], 'little')
assert d[e:e+2] == b'PE', 'unexpected signature: %r' % bytes(d[e:e+4])
d[e+1] = ord('X')
open('FMVOL.DLL','wb').write(d)
print('FMVOL.DLL: %d bytes, marked PX' % len(d))
PY
