#!/bin/sh
# Build VEWSPY.DLL on the host, no DOS box needed (see ../fmvol/fmbuild.sh).
set -e
cd "$(dirname "$0")"
JWASM=${JWASM:-$HOME/tools/jwasm-src/build/GccUnixR/jwasm}
WLINK=${WLINK:-$HOME/tools/ow2/armo64/wlink}
JLMINC=${JLMINC:-$HOME/tools/jemm/include}

"$JWASM" -nologo -coff -I"$JLMINC" -Fo=VEWSPY.obj VEWSPY.ASM
"$WLINK" @VEWSPY.LNK

# wlink emits a plain PE signature; JLOAD only accepts JLMs marked PX.
python3 - <<'PY'
d = bytearray(open('VEWSPY.DLL','rb').read())
e = int.from_bytes(d[0x3c:0x40], 'little')
assert d[e:e+2] == b'PE', 'unexpected signature: %r' % bytes(d[e:e+4])
d[e+1] = ord('X')
open('VEWSPY.DLL','wb').write(d)
print('VEWSPY.DLL: %d bytes, marked PX' % len(d))
PY
