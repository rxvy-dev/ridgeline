#!/usr/bin/env python3
"""Turns Poly Haven downloads (CC0) into Ridgeline's assets/ folder.

    python3 tools/import_assets.py DOWNLOAD [DOWNLOAD ...]

Each DOWNLOAD is a zip, a folder or an .hdr file straight from polyhaven.com.
The asset id is read from the file names and decides where it goes:

  ground textures (JPG):  aerial_grass_rock -> grass, forest_ground_04 -> forest,
                          brown_mud_leaves_01 -> leaves, cliff_side -> cliff,
                          asphalt_02 -> asphalt, coast_sand_01 -> sand, gravel_road -> gravel
  sky (.hdr)           :  assets/sky/sky.hdr
  models (glTF)        :  rocks are split into one prop per rock, plants stay whole

Use --as ID=SLOT to put any other texture into a slot (e.g. --as rock_face_03=cliff).
"""
import argparse, json, os, re, shutil, subprocess, sys, tempfile, zipfile
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "assets")
SLOTS = {"aerial_grass_rock": "grass", "forest_ground_04": "forest", "brown_mud_leaves_01": "leaves", "cliff_side": "cliff",
         "asphalt_02": "asphalt", "coast_sand_01": "sand", "gravel_road": "gravel"}
PLANTS = re.compile(r"fern|grass|shrub|plant|flower|nettle|moss_0|sapling", re.I)
ROCKS = re.compile(r"rock|boulder|stone|cliff", re.I)


def asset_id(name):
    n = os.path.basename(name).lower()
    n = re.sub(r"\.(zip|gltf|glb|hdr|exr|jpg|png)$", "", n)
    n = re.sub(r"_(1|2|4|8)k.*$", "", n)
    n = re.sub(r"_(diff|nor_gl|nor_dx|rough|arm|disp|ao|spec|col).*$", "", n)
    n = re.sub(r"^[0-9a-f]{6,}-", "", n)  # upload prefixes
    return n


def unpack(path, tmp):
    if os.path.isdir(path):
        return path
    if path.lower().endswith(".zip"):
        d = os.path.join(tmp, os.path.basename(path)[:-4])
        with zipfile.ZipFile(path) as z:
            z.extractall(d)
        return d
    return path


def files(d):
    for r, _, fs in os.walk(d):
        for f in fs:
            yield os.path.join(r, f)


def save_jpg(src, dst, size, flip_green=False):
    im = Image.open(src).convert("RGB")
    if max(im.size) > size:
        im = im.resize((size, size * im.height // im.width), Image.LANCZOS)
    if flip_green:
        r, g, b = im.split()
        g = g.point(lambda v: 255 - v)
        im = Image.merge("RGB", (r, g, b))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    im.save(dst, quality=88, optimize=True)
    print("  ->", os.path.relpath(dst, ROOT), im.size)


def do_texture(aid, slot, d, credits):
    fs = list(files(d)) if os.path.isdir(d) else [d]
    diff = [f for f in fs if re.search(r"(_diff|_col|albedo|basecolor)", f, re.I) and f.lower().endswith((".jpg", ".png"))]
    nor = [f for f in fs if re.search(r"nor_gl|_nor_\d", f, re.I) and f.lower().endswith((".jpg", ".png"))]
    nordx = [f for f in fs if re.search(r"nor_dx", f, re.I) and f.lower().endswith((".jpg", ".png"))]
    if not diff:
        print("  no colour map found in", d); return
    save_jpg(sorted(diff)[-1], os.path.join(OUT, "textures", slot + "_diff.jpg"), 2048)
    if nor:
        save_jpg(sorted(nor)[-1], os.path.join(OUT, "textures", slot + "_nor.jpg"), 2048)
    elif nordx:
        save_jpg(sorted(nordx)[-1], os.path.join(OUT, "textures", slot + "_nor.jpg"), 2048, flip_green=True)
    credits.append((aid, "texture", slot))


def gltf_nodes(gltf):
    g = json.load(open(gltf))
    names = []
    roots = g["scenes"][g.get("scene", 0)]["nodes"]
    def walk(i, depth):
        n = g["nodes"][i]
        if "mesh" in n:
            names.append(n.get("name", "node%d" % i))
        for c in n.get("children", []):
            walk(c, depth + 1)
    for r in roots:
        walk(r, 0)
    return names


def do_model(aid, d, props, credits):
    gl = [f for f in files(d) if f.lower().endswith((".gltf", ".glb"))]
    if not gl:
        print("  no glTF in", d); return
    gltf = gl[0]
    plant = bool(PLANTS.search(aid)) and not ROCKS.search(aid)
    names = gltf_nodes(gltf) if gltf.endswith(".gltf") else []
    pieces = [None]
    if not plant and len(names) > 1:
        pieces = names  # a rock set: one prop per rock
    for k, piece in enumerate(pieces):
        name = aid if piece is None else "%s_%s" % (aid, chr(97 + k) if k < 26 else str(k))
        out = os.path.join(OUT, "props", name)
        cmd = [sys.executable, os.path.join(HERE, "import_car.py"), gltf, out, "--no-wheels", "--max-tex", "1024"]
        if piece is not None:
            cmd += ["--only", "^" + re.escape(piece) + "($|/)"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(os.path.join(out, "model.rcar")):
            print("  failed:", name, r.stdout[-300:], r.stderr[-300:]); continue
        for f in ("car.ini", "LICENSE.txt"):
            if os.path.exists(os.path.join(out, f)):
                os.remove(os.path.join(out, f))
        props.append((name, "plant" if plant else "rock", 0.8 if plant else 0.85, 1.3 if plant else 1.2))
        print("  ->", os.path.relpath(out, ROOT))
    credits.append((aid, "model", "plant" if plant else "rock"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("downloads", nargs="+")
    ap.add_argument("--as", dest="mapping", action="append", default=[], help="ID=SLOT")
    a = ap.parse_args()
    slots = dict(SLOTS)
    for m in a.mapping:
        k, v = m.split("=")
        slots[k] = v
    props, credits = [], []
    listf = os.path.join(OUT, "props", "list.txt")
    if os.path.exists(listf):
        for line in open(listf):
            p = line.split()
            if len(p) == 4 and not p[0].startswith("#"):
                props.append((p[0], p[1], float(p[2]), float(p[3])))
    with tempfile.TemporaryDirectory() as tmp:
        for path in a.downloads:
            aid = asset_id(path)
            print(path, "->", aid)
            d = unpack(path, tmp)
            if path.lower().endswith(".hdr") or (os.path.isdir(d) and any(f.lower().endswith(".hdr") for f in files(d))):
                src = path if path.lower().endswith(".hdr") else [f for f in files(d) if f.lower().endswith(".hdr")][0]
                os.makedirs(os.path.join(OUT, "sky"), exist_ok=True)
                shutil.copy(src, os.path.join(OUT, "sky", "sky.hdr"))
                credits.append((aid, "hdri", "sky"))
                print("  -> assets/sky/sky.hdr")
            elif os.path.isdir(d) and any(f.lower().endswith((".gltf", ".glb")) for f in files(d)):
                do_model(aid, d, props, credits)
            elif aid in slots:
                do_texture(aid, slots[aid], d, credits)
            else:
                print("  unknown asset - use --as %s=SLOT" % aid)
    seen, uniq = set(), []
    for p in props:
        if p[0] not in seen:
            seen.add(p[0]); uniq.append(p)
    os.makedirs(os.path.join(OUT, "props"), exist_ok=True)
    with open(listf, "w") as f:
        f.write("# folder type min_scale max_scale (written by tools/import_assets.py)\n")
        for p in uniq:
            f.write("%s %s %.2f %.2f\n" % p)
    cred = os.path.join(OUT, "CREDITS.md")
    old = open(cred).read() if os.path.exists(cred) else "# Asset credits\n\nAll assets in this folder are from [Poly Haven](https://polyhaven.com) and are **CC0** (public domain).\n\n"
    for aid, kind, use in credits:
        line = "- [%s](https://polyhaven.com/a/%s) (%s, used as %s)\n" % (aid, aid, kind, use)
        if line not in old:
            old += line
    open(cred, "w").write(old)


if __name__ == "__main__":
    main()
