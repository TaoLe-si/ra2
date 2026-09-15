"""_probe_lighting.py -- 临時探针：从 rules(md).ini 里抠 [Lighting] 等光照相关段。"""
from __future__ import annotations
import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import inidump
import mixdump

BUILD = r"E:\ra2source\build"
MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]

def get_raw(name):
    p = os.path.join(BUILD, name)
    if os.path.exists(p):
        return open(p, "rb").read()
    for m in reversed(MIXES):
        data = mixdump.read_file(m, name)
        if data:
            return data
    return None

if __name__ == "__main__":
    wanted = ["Lighting", "AudioVisual", "General"]
    for src in ("rules.ini", "rulesmd.ini"):
        raw = get_raw(src)
        if not raw:
            print("!! 找不到", src)
            continue
        secs, mal = inidump.parse(raw.decode("latin-1"))
        print("==== %s (%d 段) ====" % (src, len(secs)))
        for name, ents in secs:
            if name.lower() in [w.lower() for w in wanted]:
                print("[%s]" % name)
                for k, v in ents:
                    print("  %s = %s" % (k, v))
