import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile                     # noqa: E402

path, want = sys.argv[1], int(sys.argv[2], 16)
limb = int(sys.argv[3]) if len(sys.argv) > 3 else 0

m = MixFile(path)
data = None
for owner, h, off, size, depth in iter_leaves(m):
    if h == want and size >= 1024 and owner.read(off, 15) == b"Voxel Animation":
        data = owner.read(off, size)
        break
v = VxlFile(data)
t = v.tailers[limb]
print(v.summary())
print("body_start=%d body_size=%d  tailer ofs=(%d,%d,%d)  %dx%dx%d"
      % (v.body_start, v.body_size, t.span_start_ofs, t.span_end_ofs,
         t.span_data_ofs, t.x, t.y, t.z))

b = v.body
for o in range(0, len(b), 16):
    chk = ""
    if o % 16 == 0:
        pass
    print("  %04X  %s  |%s|" % (o, b[o:o + 16].hex(" "),
                                "".join(chr(c) if 32 <= c < 127 else "." for c in b[o:o + 16])))
