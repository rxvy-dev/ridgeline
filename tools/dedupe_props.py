#!/usr/bin/env python3
"""Store each prop texture once: textures shared between .rcar props (rock sets
split into single rocks, for example) move to assets/props/_tex/<hash>.ztex and the
.rcar keeps a short reference (a texture record with width = height = 0 whose data
is the file name). Safe to run again; already-referenced textures are left alone."""
import glob, hashlib, os, struct, sys

root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "props")
texdir = os.path.join(root, "_tex")
os.makedirs(texdir, exist_ok=True)
before = after = 0
for f in sorted(glob.glob(os.path.join(root, "*", "model.rcar"))):
    d = open(f, "rb").read()
    before += len(d)
    p = 4 + 4 + 12 + 4 * 20
    head = d[:p]
    nt = struct.unpack_from("<I", d, p)[0]
    p += 4
    recs = []
    for _ in range(nt):
        w, h, l = struct.unpack_from("<HHI", d, p)
        blob = d[p + 8:p + 8 + l]
        p += 8 + l
        if w == 0 and h == 0:
            recs.append(struct.pack("<HHI", 0, 0, l) + blob)
            continue
        name = hashlib.sha1(blob).hexdigest()[:16] + ".ztex"
        path = os.path.join(texdir, name)
        if not os.path.exists(path):
            open(path, "wb").write(struct.pack("<HHI", w, h, l) + blob)
        nb = name.encode()
        recs.append(struct.pack("<HHI", 0, 0, len(nb)) + nb)
    out = head + struct.pack("<I", nt) + b"".join(recs) + d[p:]
    open(f, "wb").write(out)
    after += len(out)
shared = sum(os.path.getsize(x) for x in glob.glob(os.path.join(texdir, "*.ztex")))
print("props: %.1f MB -> %.1f MB models + %.1f MB shared textures" % (before / 1e6, after / 1e6, shared / 1e6))
