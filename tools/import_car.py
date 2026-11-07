#!/usr/bin/env python3
"""Ridgeline car importer.

Converts a car model (binary FBX 7.x or glTF 2.0 .gltf/.glb) into Ridgeline's
.rcar format plus a car.ini you can edit (name, class, price, power, ...).

    python3 tools/import_car.py MODEL.fbx|MODEL.gltf  OUT_DIR  [options]

Options:
    --textures DIR      extra folder to search for texture files
    --forward AXIS      which model axis is the car's front: +z (default) -z +x -x
    --length METRES     rescale so the car is this long (default: keep, auto cm->m)
    --name NAME         display name written to car.ini
    --max-tex N         max texture size (default 1024)

Wheels are detected from part/material names (tire, tyre, rim, wheel, disc,
hub; calliper/caliper/brake stay fixed) and split into the four corners.
Copy OUT_DIR into ~/.local/share/rxvy/ridgeline/cars/ to drive it in game.
Only import models you are allowed to use.
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fbxparse  # noqa: E402

try:
    from PIL import Image
except ImportError:
    Image = None

SPIN = re.compile(r"tire|tyre|rim|wheel|disc|disk|hub|spoke|rotor", re.I)
FIXED = re.compile(r"calliper|caliper|brake(?!disc)", re.I)
TIRE = re.compile(r"tire|tyre", re.I)

F_PAINT, F_GLASS, F_LIGHT, F_ALPHA, F_CHROME, F_TIRE, F_EMISSIVE, F_BLEND = 1, 2, 4, 8, 16, 32, 64, 128
F_PAINTMASK = 256  # paint only the coloured areas of a livery texture


# ----------------------------------------------------------------------------- math helpers
def euler_matrix(deg, order=0):
    x, y, z = [math.radians(v) for v in deg]
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    Rx = np.array([[1, 0, 0, 0], [0, cx, -sx, 0], [0, sx, cx, 0], [0, 0, 0, 1]], float)
    Ry = np.array([[cy, 0, sy, 0], [0, 1, 0, 0], [-sy, 0, cy, 0], [0, 0, 0, 1]], float)
    Rz = np.array([[cz, -sz, 0, 0], [sz, cz, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]], float)
    orders = {0: Rz @ Ry @ Rx, 1: Ry @ Rz @ Rx, 2: Rz @ Rx @ Ry, 3: Rx @ Rz @ Ry, 4: Ry @ Rx @ Rz, 5: Rx @ Ry @ Rz}
    return orders.get(order, Rz @ Ry @ Rx)


def T(v):
    m = np.eye(4)
    m[:3, 3] = v
    return m


def S(v):
    return np.diag([v[0], v[1], v[2], 1.0])


# ----------------------------------------------------------------------------- loaders
class Part:
    def __init__(self, name, mat, pos, nrm, uv, idx):
        self.name, self.mat, self.pos, self.nrm, self.uv, self.idx = name, mat, pos, nrm, uv, idx


class Material:
    def __init__(self, name):
        self.name = name
        self.color = [1.0, 1.0, 1.0, 1.0]
        self.tex = {}  # base / normal / emissive / opacity -> file path
        self.emissive = [0.0, 0.0, 0.0]


def load_fbx(path):
    root, version = fbxparse.load(path)
    objs = root.find("Objects")
    gs = fbxparse.props70(root.find("GlobalSettings"))
    unit = gs.get("UnitScaleFactor", [1.0])[0]
    by_id = {}
    for n in objs.children:
        if n.props:
            by_id[n.props[0]] = n
    parent = {}       # child id -> parent model id
    model_geom = {}   # model id -> [geometry ids]
    model_mats = {}   # model id -> [material ids] (ordered)
    mat_tex = {}      # material id -> {slot: texture id}
    tex_video = {}
    for c in root.find("Connections").findall("C"):
        kind, child, par = c.props[0], c.props[1], c.props[2]
        cn, pn = by_id.get(child), by_id.get(par)
        if cn is None:
            continue
        if cn.name == "Model" and (pn is None or pn.name == "Model"):
            parent[child] = par if pn is not None else 0
        elif cn.name == "Geometry" and pn is not None and pn.name == "Model":
            model_geom.setdefault(par, []).append(child)
        elif cn.name == "Material" and pn is not None and pn.name == "Model":
            model_mats.setdefault(par, []).append(child)
        elif cn.name == "Texture" and pn is not None and pn.name == "Material":
            slot = c.props[3] if len(c.props) > 3 else "DiffuseColor"
            mat_tex.setdefault(par, {})[slot] = child
        elif cn.name == "Video" and pn is not None and pn.name == "Texture":
            tex_video[par] = child

    def local(model):
        p = fbxparse.props70(model)
        g = lambda k, d: np.array(p.get(k, d)[:3], float)
        order = int(p.get("RotationOrder", [0])[0]) if "RotationOrder" in p else 0
        t, r, s = g("Lcl Translation", [0, 0, 0]), g("Lcl Rotation", [0, 0, 0]), g("Lcl Scaling", [1, 1, 1])
        pre, post = g("PreRotation", [0, 0, 0]), g("PostRotation", [0, 0, 0])
        roff, rp = g("RotationOffset", [0, 0, 0]), g("RotationPivot", [0, 0, 0])
        soff, sp = g("ScalingOffset", [0, 0, 0]), g("ScalingPivot", [0, 0, 0])
        R = euler_matrix(r, order)
        Rpre, Rpost = euler_matrix(pre), euler_matrix(post)
        m = T(t) @ T(roff) @ T(rp) @ Rpre @ R @ np.linalg.inv(Rpost) @ T(-rp) @ T(soff) @ T(sp) @ S(s) @ T(-sp)
        geo = T(g("GeometricTranslation", [0, 0, 0])) @ euler_matrix(g("GeometricRotation", [0, 0, 0])) @ S(g("GeometricScaling", [1, 1, 1]))
        return m, geo

    cache = {}

    def world(mid):
        if mid in cache:
            return cache[mid]
        node = by_id.get(mid)
        if node is None or node.name != "Model":
            return np.eye(4)
        m, _ = local(node)
        par = parent.get(mid, 0)
        w = (world(par) @ m) if par else m
        cache[mid] = w
        return w

    materials, mat_index = [], {}

    def material(mid):
        if mid in mat_index:
            return mat_index[mid]
        node = by_id[mid]
        m = Material(node.props[1].split("\x00")[0])
        p = fbxparse.props70(node)
        if "DiffuseColor" in p:
            m.color[:3] = [float(v) for v in p["DiffuseColor"][:3]]
        if "EmissiveColor" in p and "EmissiveFactor" in p:
            f = float(p["EmissiveFactor"][0])
            m.emissive = [float(v) * f for v in p["EmissiveColor"][:3]]
        for slot, tid in mat_tex.get(mid, {}).items():
            tnode = by_id.get(tid)
            if tnode is None:
                continue
            fn = None
            for key in ("RelativeFilename", "FileName"):
                f = tnode.find(key)
                if f and f.props and f.props[0]:
                    fn = f.props[0]
                    break
            if not fn:
                continue
            s = slot.lower()
            if "normal" in s or "bump" in s:
                m.tex["normal"] = fn
            elif "emissive" in s:
                m.tex["emissive"] = fn
            elif "transparent" in s or "opacity" in s:
                m.tex["opacity"] = fn
            elif "diffuse" in s or "base" in s:
                m.tex["base"] = fn
        mat_index[mid] = len(materials)
        materials.append(m)
        return mat_index[mid]

    parts = []
    for mid, gids in model_geom.items():
        model = by_id[mid]
        name = model.props[1].split("\x00")[0]
        W = world(mid) @ local(model)[1]
        mats = [material(x) for x in model_mats.get(mid, [])]
        for gid in gids:
            geo = by_id[gid]
            parts.extend(fbx_geometry(geo, name, mats, W))
    return parts, materials, unit


def _layer(geo, lname, dname, iname, poly_vi, vcount):
    """Return per-polygon-vertex values for a layer element."""
    le = geo.find(lname)
    if le is None:
        return None
    data = le.find(dname)
    if data is None:
        return None
    arr = np.asarray(data.props[0], float)
    comps = 3 if lname.endswith("Normal") else 2
    arr = arr.reshape(-1, comps)
    mapping = le.find("MappingInformationType").props[0]
    ref = le.find("ReferenceInformationType").props[0]
    if ref in ("IndexToDirect", "Index") and le.find(iname) is not None:
        ids = np.asarray(le.find(iname).props[0], np.int64)
        if mapping in ("ByPolygonVertex",):
            return arr[ids]
        if mapping in ("ByVertex", "ByVertice", "ByControlPoint"):
            return arr[ids][poly_vi]
    if mapping in ("ByPolygonVertex",):
        return arr
    if mapping in ("ByVertex", "ByVertice", "ByControlPoint"):
        return arr[poly_vi]
    return None


def fbx_geometry(geo, name, mats, W):
    verts = geo.find("Vertices")
    pvi = geo.find("PolygonVertexIndex")
    if verts is None or pvi is None:
        return []
    V = np.asarray(verts.props[0], float).reshape(-1, 3)
    raw = np.asarray(pvi.props[0], np.int64)
    ends = raw < 0
    vi = np.where(ends, ~raw, raw)
    N = _layer(geo, "LayerElementNormal", "Normals", "NormalsIndex", vi, len(V))
    UV = _layer(geo, "LayerElementUV", "UV", "UVIndex", vi, len(V))
    if N is None:
        N = np.zeros((len(vi), 3))
    if UV is None:
        UV = np.zeros((len(vi), 2))
    UV = UV.copy()
    UV[:, 1] = 1.0 - UV[:, 1]  # FBX v points up, textures are stored top row first
    # polygon -> material
    pm = None
    lm = geo.find("LayerElementMaterial")
    poly_starts = np.concatenate([[0], np.nonzero(ends)[0][:-1] + 1])
    npoly = len(poly_starts)
    if lm is not None and lm.find("Materials") is not None:
        marr = np.asarray(lm.find("Materials").props[0], np.int64)
        mapping = lm.find("MappingInformationType").props[0]
        pm = marr if mapping == "ByPolygon" and len(marr) == npoly else np.full(npoly, marr[0] if len(marr) else 0)
    else:
        pm = np.zeros(npoly, np.int64)
    # fan-triangulate
    poly_ends = np.nonzero(ends)[0]
    tris_by_mat = {}
    start = 0
    for p, e in enumerate(poly_ends):
        k = e - start + 1
        if k >= 3:
            lst = tris_by_mat.setdefault(int(pm[p]), [])
            for j in range(1, k - 1):
                lst.append((start, start + j, start + j + 1))
        start = e + 1
    Wn = np.linalg.inv(W[:3, :3]).T
    P = (np.c_[V[vi], np.ones(len(vi))] @ W.T)[:, :3]
    Nn = N @ Wn.T
    ln = np.linalg.norm(Nn, axis=1, keepdims=True)
    Nn = Nn / np.maximum(ln, 1e-9)
    if np.linalg.det(W[:3, :3]) < 0:
        flip = True
    else:
        flip = False
    out = []
    for m, tris in tris_by_mat.items():
        t = np.asarray(tris, np.int64)
        if flip:
            t = t[:, [0, 2, 1]]
        used = np.unique(t)
        remap = np.full(len(vi), -1, np.int64)
        remap[used] = np.arange(len(used))
        mat = mats[m] if m < len(mats) else (mats[0] if mats else -1)
        out.append(Part(name, mat, P[used], Nn[used], UV[used], remap[t].reshape(-1)))
    return out


def load_gltf(path):
    if path.endswith(".glb"):
        data = open(path, "rb").read()
        jlen = struct.unpack_from("<I", data, 12)[0]
        js = json.loads(data[20:20 + jlen])
        blen = struct.unpack_from("<I", data, 20 + jlen)[0]
        buffers = [data[28 + jlen:28 + jlen + blen]]
    else:
        js = json.load(open(path))
        buffers = [open(os.path.join(os.path.dirname(path), b["uri"]), "rb").read() for b in js["buffers"]]
    comp = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
    ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}

    def acc(i):
        a = js["accessors"][i]
        bv = js["bufferViews"][a["bufferView"]]
        buf = buffers[bv.get("buffer", 0)]
        dt = np.dtype(comp[a["componentType"]])
        n = ncomp[a["type"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = bv.get("byteStride", 0) or dt.itemsize * n
        cnt = a["count"]
        if stride == dt.itemsize * n:
            arr = np.frombuffer(buf, dt, cnt * n, off).reshape(cnt, n)
        else:
            arr = np.stack([np.frombuffer(buf, dt, n, off + k * stride) for k in range(cnt)])
        if a.get("normalized"):
            arr = arr.astype(float) / np.iinfo(dt).max
        return arr.astype(float) if dt != np.uint32 else arr

    images = js.get("images", [])
    textures = js.get("textures", [])

    def tex_path(info):
        if not info:
            return None
        src = textures[info["index"]].get("source")
        if src is None:
            return None
        return images[src].get("uri")

    materials = []
    for m in js.get("materials", []):
        mm = Material(m.get("name", "mat"))
        pbr = m.get("pbrMetallicRoughness", {})
        mm.color = list(pbr.get("baseColorFactor", [1, 1, 1, 1]))
        b = tex_path(pbr.get("baseColorTexture"))
        if b:
            mm.tex["base"] = b
        n = tex_path(m.get("normalTexture"))
        if n:
            mm.tex["normal"] = n
        e = tex_path(m.get("emissiveTexture"))
        if e:
            mm.tex["emissive"] = e
        mm.emissive = list(m.get("emissiveFactor", [0, 0, 0]))
        if m.get("alphaMode") == "BLEND":
            mm.blend = True
        if m.get("alphaMode") == "MASK":
            mm.mask = True
        materials.append(mm)

    def node_local(n):
        if "matrix" in n:
            return np.array(n["matrix"], float).reshape(4, 4).T
        t = np.array(n.get("translation", [0, 0, 0]), float)
        x, y, z, w = n.get("rotation", [0, 0, 0, 1])
        R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0],
                      [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0],
                      [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0], [0, 0, 0, 1]])
        return T(t) @ R @ S(n.get("scale", [1, 1, 1]))

    parts = []

    def visit(i, Wp, pname):
        n = js["nodes"][i]
        W = Wp @ node_local(n)
        name = n.get("name", pname)
        if "mesh" in n:
            for prim in js["meshes"][n["mesh"]]["primitives"]:
                if prim.get("mode", 4) != 4:
                    continue
                at = prim["attributes"]
                P = acc(at["POSITION"])
                N = acc(at["NORMAL"]) if "NORMAL" in at else np.zeros_like(P)
                UV = acc(at["TEXCOORD_0"]) if "TEXCOORD_0" in at else np.zeros((len(P), 2))
                idx = acc(prim["indices"]).reshape(-1).astype(np.int64) if "indices" in prim else np.arange(len(P))
                Pw = (np.c_[P, np.ones(len(P))] @ W.T)[:, :3]
                Nw = N @ np.linalg.inv(W[:3, :3])
                Nw /= np.maximum(np.linalg.norm(Nw, axis=1, keepdims=True), 1e-9)
                if np.linalg.det(W[:3, :3]) < 0:
                    idx = idx.reshape(-1, 3)[:, [0, 2, 1]].reshape(-1)
                parts.append(Part(name + "/" + js["meshes"][n["mesh"]].get("name", ""), prim.get("material", -1), Pw, Nw, UV, idx))
        for c in n.get("children", []):
            visit(c, W, name)

    scene = js["scenes"][js.get("scene", 0)]
    for r in scene["nodes"]:
        visit(r, np.eye(4), "root")
    return parts, materials, 1.0


# ----------------------------------------------------------------------------- textures
def find_file(fn, dirs):
    if not fn:
        return None
    base = os.path.basename(fn.replace("\\", "/"))
    for d in dirs:
        for cand in (os.path.join(d, fn), os.path.join(d, base)):
            if os.path.isfile(cand):
                return cand
    low = base.lower()
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in files:
                if f.lower() == low:
                    return os.path.join(root, f)
    return None


def pack_texture(path, max_size, opacity=None):
    im = Image.open(path).convert("RGBA")
    if opacity:
        op = Image.open(opacity).convert("L").resize(im.size)
        im.putalpha(op)
    w, h = im.size
    s = min(1.0, max_size / max(w, h))
    if s < 1.0:
        im = im.resize((max(1, int(w * s)), max(1, int(h * s))), Image.LANCZOS)
    return im


# ----------------------------------------------------------------------------- main
def classify(mat, names):
    n = (mat.name + " " + " ".join(names)).lower()
    f = 0
    if re.search(r"paint|carpaint|coloured_mat|body_col", n) and not re.search(r"glass|window", n):
        f |= F_PAINT
    if re.search(r"glass|window|windscreen|windshield", n) and not re.search(r"light", n):
        f |= F_GLASS
    if re.search(r"light|lamp", n):
        f |= F_LIGHT
    if re.search(r"chrome|alumin|metal|exhaust|rim", n):
        f |= F_CHROME
    if re.search(r"tire|tyre", n):
        f |= F_TIRE
    if re.search(r"alpha|grille|grid|badge|decal|mask", n) or mat.tex.get("opacity"):
        f |= F_ALPHA
    if getattr(mat, "blend", False):
        f |= F_BLEND
    if getattr(mat, "mask", False):
        f |= F_ALPHA
    return f


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("model")
    ap.add_argument("out")
    ap.add_argument("--textures", action="append", default=[])
    ap.add_argument("--forward", default="+z")
    ap.add_argument("--length", type=float, default=0)
    ap.add_argument("--name", default="")
    ap.add_argument("--max-tex", type=int, default=1024)
    ap.add_argument("--drop", default="", help="regex of part names to drop (e.g. lights bars)")
    ap.add_argument("--paint", default="", help="regex of material names that are body paint (overrides detection)")
    ap.add_argument("--no-glass", default="", help="regex of material names that are NOT glass (e.g. wiper)")
    a = ap.parse_args()
    if Image is None:
        sys.exit("Pillow is required: pip install pillow")

    if a.model.lower().endswith(".fbx"):
        parts, materials, unit = load_fbx(a.model)
    else:
        parts, materials, unit = load_gltf(a.model)
    if a.drop:
        dr = re.compile(a.drop, re.I)
        parts = [p for p in parts if not dr.search(p.name) and not (p.mat >= 0 and dr.search(materials[p.mat].name))]
    print("parts:", len(parts), "materials:", len(materials))

    # ---- orientation: rotate about Y so the requested axis becomes +Z
    rot = {"+z": 0, "-z": 180, "+x": -90, "-x": 90}[a.forward]
    c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    Ry = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    for p in parts:
        p.pos = p.pos @ Ry.T
        p.nrm = p.nrm @ Ry.T

    allp = np.concatenate([p.pos for p in parts])
    lo, hi = allp.min(0), allp.max(0)
    length = hi[2] - lo[2]
    scale = 1.0
    if a.length > 0:
        scale = a.length / length
    elif length > 40:  # centimetres
        scale = 0.01
    for p in parts:
        p.pos = p.pos * scale
    allp = np.concatenate([p.pos for p in parts])
    lo, hi = allp.min(0), allp.max(0)
    center = np.array([(lo[0] + hi[0]) / 2, 0, (lo[2] + hi[2]) / 2])

    # ---- wheels: split wheel geometry into four corners by triangle centroid
    def part_names(p):
        return p.name + " " + (materials[p.mat].name if 0 <= p.mat < len(materials) else "")

    wheel_tris = {k: [] for k in range(4)}
    out_parts = []
    spin_parts, fixed_parts, body_parts = [], [], []
    for p in parts:
        nm = part_names(p)
        if FIXED.search(nm) and not SPIN.search(p.name if not FIXED.search(p.name) else ""):
            fixed_parts.append(p)
        elif SPIN.search(nm):
            spin_parts.append(p)
        else:
            body_parts.append(p)
    if not spin_parts:
        sys.exit("no wheel parts found (looked for tire/rim/wheel in names)")
    ground = min(p.pos[:, 1].min() for p in spin_parts)
    for p in parts:
        p.pos = p.pos - np.array([center[0], ground, center[2]])

    def corner_of(cent):
        # +x is left, +z is front: 0 FL, 1 FR, 2 RL, 3 RR
        front = cent[:, 2] > 0
        left = cent[:, 0] > 0
        return np.where(front, np.where(left, 0, 1), np.where(left, 2, 3))

    # tyre bounds per corner -> wheel centre / radius / width
    tire_src = [p for p in spin_parts if TIRE.search(part_names(p)) and not re.search("steering", part_names(p), re.I)] or spin_parts
    wb = [[np.full(3, 1e9), np.full(3, -1e9)] for _ in range(4)]
    for p in tire_src:
        tri = p.idx.reshape(-1, 3)
        cent = p.pos[tri].mean(1)
        cor = corner_of(cent)
        for k in range(4):
            sel = tri[cor == k].reshape(-1)
            if len(sel):
                wb[k][0] = np.minimum(wb[k][0], p.pos[sel].min(0))
                wb[k][1] = np.maximum(wb[k][1], p.pos[sel].max(0))
    wheels = []
    for k in range(4):
        lo_, hi_ = wb[k]
        if lo_[0] > 1e8:
            sys.exit("could not find wheel %d - try --forward" % k)
        cen = (lo_ + hi_) / 2
        rad = (hi_[1] - lo_[1]) / 2
        wid = hi_[0] - lo_[0]
        wheels.append((cen, rad, wid))
        print("wheel %d: centre %s radius %.3f width %.3f" % (k, np.round(cen, 3), rad, wid))

    def emit_split(p, role_base, body_out):
        """Triangles inside a wheel cylinder go to that wheel; the rest stays on the body."""
        tri = p.idx.reshape(-1, 3)
        cent = p.pos[tri].mean(1)
        assign = np.full(len(tri), -1)
        for k in range(4):
            cen, rad, wid = wheels[k]
            d = np.hypot(cent[:, 1] - cen[1], cent[:, 2] - cen[2])
            inside = (d < rad * 1.06) & (np.abs(cent[:, 0] - cen[0]) < wid * 0.75 + 0.05)
            assign[(assign < 0) & inside] = k
        for k in range(-1, 4):
            t = tri[assign == k]
            if not len(t):
                continue
            used = np.unique(t)
            remap = np.full(len(p.pos), -1, np.int64)
            remap[used] = np.arange(len(used))
            if k < 0:
                body_out.append((0, p.mat, p.pos[used], p.nrm[used], p.uv[used], remap[t].reshape(-1)))
            else:
                out_parts.append((role_base + k, p.mat, p.pos[used] - wheels[k][0], p.nrm[used], p.uv[used], remap[t].reshape(-1)))

    for p in body_parts:
        out_parts.append((0, p.mat, p.pos, p.nrm, p.uv, p.idx))
    for p in spin_parts:
        emit_split(p, 1, out_parts)
    for p in fixed_parts:
        emit_split(p, 5, out_parts)

    # merge by (role, material)
    merged = {}
    for role, mat, P, N, UV, I in out_parts:
        key = (role, mat)
        merged.setdefault(key, []).append((P, N, UV, I))
    final = []
    for (role, mat), lst in merged.items():
        Ps, Ns, UVs, Is, base = [], [], [], [], 0
        for P, N, UV, I in lst:
            Ps.append(P); Ns.append(N); UVs.append(UV); Is.append(I + base); base += len(P)
        final.append((role, mat, np.concatenate(Ps), np.concatenate(Ns), np.concatenate(UVs), np.concatenate(Is)))

    allp = np.concatenate([f[2] for f in final if f[0] == 0])
    lo, hi = allp.min(0), allp.max(0)
    dims = hi - lo
    print("dimensions L %.2f W %.2f H %.2f m" % (dims[2], dims[0], dims[1]))

    # ---- textures
    dirs = [os.path.dirname(os.path.abspath(a.model))] + a.textures
    tex_list, tex_index = [], {}

    def tex_id(fn, opacity=None, big=False):
        path = find_file(fn, dirs)
        if not path:
            return -1
        op = find_file(opacity, dirs) if opacity else None
        key = (path, op)
        if key not in tex_index:
            try:
                im = pack_texture(path, a.max_tex * (2 if big else 1), op)
            except Exception as e:  # noqa: BLE001
                print("  texture failed:", path, e)
                return -1
            tex_index[key] = len(tex_list)
            tex_list.append(im)
        return tex_index[key]

    mat_records = []
    for i, m in enumerate(materials):
        names = [p.name for p in parts if p.mat == i][:4]
        flags = classify(m, names)
        if a.paint and re.search(a.paint, m.name, re.I):
            flags |= F_PAINT
            flags &= ~(F_GLASS | F_CHROME | F_LIGHT)
        if a.no_glass and re.search(a.no_glass, m.name, re.I):
            flags &= ~(F_GLASS | F_BLEND)
        base = tex_id(m.tex.get("base"), m.tex.get("opacity"), big=bool(flags & F_PAINT))
        nrm = tex_id(m.tex.get("normal"))
        emi = tex_id(m.tex.get("emissive"))
        # a paint texture that is mostly grey/white can be tinted; a coloured livery cannot
        if flags & F_PAINT and base >= 0:
            arr = np.asarray(tex_list[base].convert("RGB").resize((64, 64)), float) / 255
            sat = (arr.max(2) - arr.min(2)).mean()
            if sat > 0.18:
                # coloured livery: keep it, but let the paint shop recolour just its coloured areas
                flags = (flags | F_PAINTMASK) if (a.paint and re.search(a.paint, m.name, re.I)) else (flags & ~F_PAINT)
        if base >= 0 and tex_list[base].mode == "RGBA":
            alpha = np.asarray(tex_list[base].getchannel("A").resize((64, 64)), float)
            if alpha.min() < 128 and not flags & F_GLASS:
                flags |= F_ALPHA
        if any(e > 0.01 for e in m.emissive) or emi >= 0:
            flags |= F_EMISSIVE
        mat_records.append((m.name, m.color, base, nrm, emi, m.emissive, flags))

    os.makedirs(a.out, exist_ok=True)
    out = open(os.path.join(a.out, "model.rcar"), "wb")
    out.write(b"RCAR")
    out.write(struct.pack("<I", 1))
    out.write(struct.pack("<3f", float(dims[2]), float(dims[0]), float(dims[1])))
    for cen, rad, wid in wheels:
        out.write(struct.pack("<5f", float(cen[0]), float(cen[1]), float(cen[2]), float(rad), float(wid)))
    out.write(struct.pack("<I", len(tex_list)))
    for im in tex_list:
        raw = im.tobytes()
        comp = zlib.compress(raw, 6)
        out.write(struct.pack("<HHI", im.size[0], im.size[1], len(comp)))
        out.write(comp)
    out.write(struct.pack("<I", len(mat_records)))
    for name, col, base, nrm, emi, emis, flags in mat_records:
        nb = name.encode("utf-8")[:255]
        out.write(struct.pack("<H", len(nb)) + nb)
        out.write(struct.pack("<4f", *[float(x) for x in (list(col) + [1.0])[:4]]))
        out.write(struct.pack("<3i", base, nrm, emi))
        out.write(struct.pack("<3f", *[float(x) for x in emis[:3]]))
        out.write(struct.pack("<I", flags))
    out.write(struct.pack("<I", len(final)))
    tris = 0
    for role, mat, P, N, UV, I in final:
        out.write(struct.pack("<IiII", role, int(mat), len(P), len(I)))
        v = np.concatenate([P, N, UV], 1).astype(np.float32)
        out.write(v.tobytes())
        out.write(I.astype(np.uint32).tobytes())
        tris += len(I) // 3
    out.close()
    print("wrote %s: %d parts, %d triangles, %d textures" % (os.path.join(a.out, "model.rcar"), len(final), tris, len(tex_list)))

    ini = os.path.join(a.out, "car.ini")
    if not os.path.exists(ini):
        with open(ini, "w") as f:
            f.write("# Ridgeline car definition - edit the numbers to tune the car\n")
            f.write("name=%s\nmake=Imported\nclass=B\nprice=50000\npower_kw=300\nmass=1400\ntop_kmh=280\n" % (a.name or os.path.basename(a.out)))
            f.write("drive=RWD\ngrip=1.1\noffroad=0.4\nredline=7500\ncylinders=6\n")
            f.write("credit=\n")
        print("wrote", ini)


if __name__ == "__main__":
    main()
