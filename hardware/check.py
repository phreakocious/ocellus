#!/usr/bin/env python3
"""Geometry assertions for OpenSCAD-exported STLs. Python stdlib only.

In CAD the bug that matters is "the boolean did not do what I thought".
These catch it: manifold (printable), bbox (right size), and point-in-solid
probes (right material in the right places).
"""
import math
import struct
import sys

EPS = 1e-7


def load(path):
    """Parse binary or ASCII STL -> list of triangles."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:5] == b"solid" and b"facet" in data[:2048]:
        text = data.decode("utf8", "ignore")
        verts = []
        for line in text.splitlines():
            line = line.strip()
            if line.startswith("vertex"):
                verts.append(tuple(float(v) for v in line.split()[1:4]))
        return [tuple(verts[i:i + 3]) for i in range(0, len(verts), 3)]
    count = struct.unpack("<I", data[80:84])[0]
    tris = []
    for i in range(count):
        off = 84 + i * 50 + 12
        tris.append(tuple(
            struct.unpack("<3f", data[off + k * 12: off + 12 + k * 12]) for k in range(3)
        ))
    return tris


def bbox(tris):
    pts = [p for t in tris for p in t]
    lo = tuple(min(p[i] for p in pts) for i in range(3))
    hi = tuple(max(p[i] for p in pts) for i in range(3))
    return lo, hi


def is_manifold(tris):
    """Every edge shared by exactly two triangles. Non-manifold = unprintable.

    Compare vertices EXACTLY. A CGAL-exported STL writes each shared vertex
    bit-identically, so exact keys match. Do not "round for float safety" --
    this model has genuinely distinct vertices 1e-6 apart, and snapping them to
    any coarser grid merges them into phantom 4-way edges. Rounding here reports
    a watertight solid as broken.
    """
    edges = {}
    for t in tris:
        for a, b in ((0, 1), (1, 2), (2, 0)):
            key = tuple(sorted((t[a], t[b])))
            edges[key] = edges.get(key, 0) + 1
    return all(n == 2 for n in edges.values())


def _ray_hits(tris, origin, direction):
    """Moller-Trumbore. Hit count, or None if the ray grazed an edge."""
    hits = 0
    for t in tris:
        v0, v1, v2 = t
        e1 = [v1[i] - v0[i] for i in range(3)]
        e2 = [v2[i] - v0[i] for i in range(3)]
        h = [
            direction[1] * e2[2] - direction[2] * e2[1],
            direction[2] * e2[0] - direction[0] * e2[2],
            direction[0] * e2[1] - direction[1] * e2[0],
        ]
        a = sum(e1[i] * h[i] for i in range(3))
        if abs(a) < EPS:
            continue
        f = 1.0 / a
        s = [origin[i] - v0[i] for i in range(3)]
        u = f * sum(s[i] * h[i] for i in range(3))
        q = [
            s[1] * e1[2] - s[2] * e1[1],
            s[2] * e1[0] - s[0] * e1[2],
            s[0] * e1[1] - s[1] * e1[0],
        ]
        v = f * sum(direction[i] * q[i] for i in range(3))
        w = 1.0 - u - v
        # grazing an edge/vertex double-counts and inverts the answer -- bail
        if min(abs(u), abs(v), abs(w)) < 1e-6 and -1e-6 < u < 1 and -1e-6 < v < 1:
            return None
        if u < 0 or v < 0 or w < 0:
            continue
        if f * sum(e2[i] * q[i] for i in range(3)) > EPS:
            hits += 1
    return hits


def solid_at(tris, pt):
    """True if pt is inside the solid. Odd crossing count = inside."""
    for k in range(8):
        ang = 0.7 * k
        direction = (math.cos(ang), math.sin(ang) * 0.31, math.sin(ang) * 0.11 + 0.017)
        hits = _ray_hits(tris, pt, direction)
        if hits is not None:
            return hits % 2 == 1
    raise RuntimeError("every ray grazed an edge at %r" % (pt,))


def check(path, expect):
    tris = load(path)
    fails = []
    print("%s: %d triangles" % (path, len(tris)))

    if not is_manifold(tris):
        fails.append("not manifold (an edge is not shared by exactly 2 triangles)")

    lo, hi = bbox(tris)
    print("  bbox  %s ..%s" % (
        " ".join("%7.2f" % c for c in lo), " ".join("%8.2f" % c for c in hi)))
    tol = expect.get("tol", 0.15)
    for label, got, want in (("lo", lo, expect.get("bbox_lo")),
                             ("hi", hi, expect.get("bbox_hi"))):
        if want is None:
            continue
        for i, axis in enumerate("xyz"):
            if abs(got[i] - want[i]) > tol:
                fails.append("bbox %s.%s = %.2f, expected %.2f" % (label, axis, got[i], want[i]))

    for name, pt in expect.get("solid", []):
        if solid_at(tris, pt):
            print("  solid  %-24s %r" % (name, pt))
        else:
            fails.append("expected SOLID at '%s' %r, found void" % (name, pt))
    for name, pt in expect.get("void", []):
        if solid_at(tris, pt):
            fails.append("expected VOID at '%s' %r, found solid" % (name, pt))
        else:
            print("  void   %-24s %r" % (name, pt))

    for f in fails:
        print("FAIL: %s" % f)
    if fails:
        sys.exit(1)
    print("  OK")
