#!/usr/bin/env python3
"""Bezel-uniformity check: the whole point of the round skin.

The front bezel is uniform iff the round outer skin is concentric with the round
reveal (the screen opening). expect.py's point-in-solid probes can't express that,
so this slices out/body.stl at the front face, circle-fits both boundaries, and
checks their centres coincide. If they do, the bezel width is outer_r - reveal_r
everywhere.

Run it when the display or reveal_dy changes:  python3 bezel.py
"""
import math
import sys

import check

TOL = 0.10          # mm: max centre offset before the bezel is visibly uneven


def section(tris, z):
    pts = []
    for t in tris:
        for a, b in ((0, 1), (1, 2), (2, 0)):
            za, zb = t[a][2], t[b][2]
            if (za - z) * (zb - z) < 0:
                f = (z - za) / (zb - za)
                pts.append((t[a][0] + f * (t[b][0] - t[a][0]),
                            t[a][1] + f * (t[b][1] - t[a][1])))
    return pts


def fit_circle(pts):
    """Kasa least-squares circle fit -> (cx, cy, r, rms)."""
    n = len(pts)
    sx = sum(p[0] for p in pts); sy = sum(p[1] for p in pts)
    sxx = sum(p[0]**2 for p in pts); syy = sum(p[1]**2 for p in pts)
    sxy = sum(p[0]*p[1] for p in pts)
    sxz = sum(p[0]*(p[0]**2+p[1]**2) for p in pts)
    syz = sum(p[1]*(p[0]**2+p[1]**2) for p in pts)
    sz = sum((p[0]**2+p[1]**2) for p in pts)
    M = [[sxx, sxy, sx], [sxy, syy, sy], [sx, sy, n]]
    V = [sxz, syz, sz]
    for i in range(3):                       # gaussian elimination
        p = M[i][i]
        M[i] = [m/p for m in M[i]]; V[i] /= p
        for k in range(3):
            if k != i:
                f = M[k][i]
                M[k] = [M[k][j]-f*M[i][j] for j in range(3)]; V[k] -= f*V[i]
    cx, cy = V[0]/2, V[1]/2
    r = math.sqrt(V[2] + cx*cx + cy*cy)
    rms = math.sqrt(sum((math.hypot(p[0]-cx, p[1]-cy)-r)**2 for p in pts)/n)
    return cx, cy, r, rms


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "out/body.stl"
    tris = check.load(path)
    z = check.bbox(tris)[1][2] - 0.6         # 0.6 below the front face -- tracks cell
                                             # thickness (was a stale 17.8: the 702030
                                             # +1mm raised the front 18.4->19.4 and this
                                             # slice cut into the module taper below it)
    pts = section(tris, z)
    inner = [p for p in pts if math.hypot(*p) < 21.0]
    outer = [p for p in pts if math.hypot(*p) >= 21.5]
    icx, icy, ir, irms = fit_circle(inner)
    ocx, ocy, orr, orms = fit_circle(outer)
    off = math.hypot(ocx-icx, ocy-icy)
    print("%s  (front-face slice z=%.1f)" % (path, z))
    print("  reveal : centre (%+.2f, %+.2f)  r %.2f  rms %.3f" % (icx, icy, ir, irms))
    print("  outer  : centre (%+.2f, %+.2f)  r %.2f  rms %.3f" % (ocx, ocy, orr, orms))
    print("  concentricity offset %.3f mm ; uniform bezel %.2f mm" % (off, orr-ir))
    if irms > 0.1:
        print("NOTE: reveal is not round (rms %.3f) -- a round skin can't give a "
              "uniform bezel over it." % irms)
    if off > TOL:
        print("FAIL: outer skin is %.2f mm off the reveal centre (> %.2f) -- bezel "
              "is uneven. Set reveal_dy to the reveal centre's +Y offset." % (off, TOL))
        sys.exit(1)
    print("  OK")


if __name__ == "__main__":
    main()
