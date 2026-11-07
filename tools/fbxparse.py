"""Minimal binary FBX (7.x) reader: nodes, properties and the scene graph pieces
Ridgeline needs (geometry, models, materials, textures, connections).
Pure Python + numpy, no external FBX SDK."""
import struct
import zlib
import numpy as np


class Node:
    __slots__ = ("name", "props", "children")

    def __init__(self, name, props, children):
        self.name, self.props, self.children = name, props, children

    def find(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def findall(self, name):
        return [c for c in self.children if c.name == name]


_ARRAY = {b"f": ("<f4", 4), b"d": ("<f8", 8), b"l": ("<i8", 8), b"i": ("<i4", 4), b"b": ("<u1", 1)}


def _read_prop(data, p):
    t = data[p:p + 1]
    p += 1
    if t == b"Y":
        return struct.unpack_from("<h", data, p)[0], p + 2
    if t == b"C":
        return data[p] != 0, p + 1
    if t == b"I":
        return struct.unpack_from("<i", data, p)[0], p + 4
    if t == b"F":
        return struct.unpack_from("<f", data, p)[0], p + 4
    if t == b"D":
        return struct.unpack_from("<d", data, p)[0], p + 8
    if t == b"L":
        return struct.unpack_from("<q", data, p)[0], p + 8
    if t in _ARRAY:
        n, enc, clen = struct.unpack_from("<III", data, p)
        p += 12
        raw = data[p:p + clen]
        p += clen
        if enc == 1:
            raw = zlib.decompress(raw)
        dt, _ = _ARRAY[t]
        return np.frombuffer(raw, dtype=dt, count=n), p
    if t in (b"S", b"R"):
        n = struct.unpack_from("<I", data, p)[0]
        p += 4
        v = data[p:p + n]
        p += n
        if t == b"S":
            v = v.decode("utf-8", "replace")
        return v, p
    raise ValueError("unknown FBX property type %r at %d" % (t, p - 1))


def _read_node(data, p, wide):
    if wide:
        end, nprops, plen = struct.unpack_from("<QQQ", data, p)
        p += 24
    else:
        end, nprops, plen = struct.unpack_from("<III", data, p)
        p += 12
    nlen = data[p]
    p += 1
    if end == 0:
        return None, p
    name = data[p:p + nlen].decode("ascii", "replace")
    p += nlen
    props = []
    for _ in range(nprops):
        v, p = _read_prop(data, p)
        props.append(v)
    children = []
    sentinel = 25 if wide else 13
    while p < end - sentinel or (p < end and data[p:end] != b"\0" * (end - p)):
        child, p = _read_node(data, p, wide)
        if child is None:
            break
        children.append(child)
    return Node(name, props, children), end


def load(path):
    data = open(path, "rb").read()
    if not data.startswith(b"Kaydara FBX Binary"):
        raise ValueError("not a binary FBX file (ASCII FBX is not supported)")
    version = struct.unpack_from("<I", data, 23)[0]
    wide = version >= 7500
    p = 27
    top = []
    while p < len(data):
        n, p = _read_node(data, p, wide)
        if n is None:
            break
        top.append(n)
    return Node("root", [], top), version


def props70(node):
    """Properties70 -> {name: [values...]}"""
    out = {}
    p = node.find("Properties70") if node else None
    if p:
        for q in p.findall("P"):
            out[q.props[0]] = q.props[4:]
    return out
