#!/usr/bin/env python3
"""Generates the API test fixtures (SMD / VTA sources and QC files) next to this script.

    python3 make_fixtures.py

Pure standard library, fully deterministic (no randomness, no timestamps).  All files are written with LF line endings,
floats as %.6f.  The generated files are committed; re-run this only when a fixture has to change.

SMD conventions (see README.md): contiguous node indices with parent < child, reference SMDs have a single `time 0`
frame, animation SMDs carry the same nodes and `time 0..N-1` frames, VTA vertex index = unique SMD position in
first-seen order (normal of the first corner at that position).
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))


# ----------------------------------------------------------------------------------------------------------------------
# output helpers
# ----------------------------------------------------------------------------------------------------------------------
def fmt(v):
    s = "%.6f" % v
    return "0.000000" if s == "-0.000000" else s


def write_file(rel, text):
    path = os.path.join(HERE, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not text.endswith("\n"):
        text += "\n"
    with open(path, "w", newline="\n") as fh:
        fh.write(text)


# ----------------------------------------------------------------------------------------------------------------------
# math (3x3 row-major matrices, column vectors)
# ----------------------------------------------------------------------------------------------------------------------
def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vmul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def vnorm(a):
    n = math.sqrt(vdot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n)


def mmul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def mvec(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


def euler(x, y, z):
    """Valve RadianEuler (x=roll, y=pitch, z=yaw) -> rotation matrix Rz*Ry*Rx."""
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    rx = ((1, 0, 0), (0, cx, -sx), (0, sx, cx))
    ry = ((cy, 0, sy), (0, 1, 0), (-sy, 0, cy))
    rz = ((cz, -sz, 0), (sz, cz, 0), (0, 0, 1))
    return mmul(rz, mmul(ry, rx))


IDENT = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
HALF_PI = 1.570796
PI = 3.141593


# ----------------------------------------------------------------------------------------------------------------------
# skeleton
# ----------------------------------------------------------------------------------------------------------------------
class Skeleton:
    """bones: list of (name, parent index, local pos, local euler rot)."""

    def __init__(self, bones):
        self.bones = bones
        for i, b in enumerate(bones):
            assert b[1] < i
        self.names = [b[0] for b in bones]
        self.bind = [(b[2], b[3]) for b in bones]
        self.world = self.world_of(self.bind)

    def index(self, name):
        return self.names.index(name)

    def world_of(self, local):
        out = []
        for i, (p, r) in enumerate(local):
            rm = euler(*r)
            par = self.bones[i][1]
            if par < 0:
                out.append((rm, p))
            else:
                prm, pt = out[par]
                out.append((mmul(prm, rm), vadd(pt, mvec(prm, p))))
        return out

    def pos(self, name):
        return self.world[self.index(name)][1]

    def at(self, name, local_point):
        """World position of a point given in a bone's local frame."""
        rm, t = self.world[self.index(name)]
        return vadd(t, mvec(rm, local_point))

    def dir(self, name, local_dir):
        return mvec(self.world[self.index(name)][0], local_dir)

    def nodes_text(self):
        return "".join('%d "%s" %d\n' % (i, b[0], b[1]) for i, b in enumerate(self.bones))

    def skeleton_frame(self, local):
        return "".join("%d  %s  %s\n" % (i, " ".join(fmt(v) for v in p), " ".join(fmt(v) for v in r))
                       for i, (p, r) in enumerate(local))


# ----------------------------------------------------------------------------------------------------------------------
# triangle soup: corner = (bone, pos, normal, uv, links or None); links = [(bone, weight), ...]
# ----------------------------------------------------------------------------------------------------------------------
def corner(bone, pos, nrm, uv, links=None):
    return (bone, pos, nrm, uv, links)


def corner_text(c):
    bone, pos, nrm, uv, links = c
    s = "%d  %s  %s  %s" % (bone, " ".join(fmt(v) for v in pos), " ".join(fmt(v) for v in nrm),
                             " ".join(fmt(v) for v in uv))
    if links is not None:
        s += " %d" % len(links) + "".join(" %d %s" % (b, fmt(w)) for b, w in links)
    return s


def primary_bone(links):
    return max(links, key=lambda bw: bw[1])[0]


def linked(sk, pos, nrm, uv, spec):
    """spec: [(bone name, weight)] -> corner with explicit links (primary bone = largest weight)."""
    links = [(sk.index(n), w) for n, w in spec]
    return corner(primary_bone(links), pos, nrm, uv, links)


def plain(sk, name, pos, nrm, uv):
    return corner(sk.index(name), pos, nrm, uv, None)


def xf_corner_data(xf, pos, nrm):
    if xf is None:
        return pos, nrm
    rm, t = xf
    return vadd(t, mvec(rm, pos)), mvec(rm, nrm)


def add_quad(tris, mat, c0, c1, c2, c3):
    """Quad c0..c3 (counter-clockwise seen from outside) -> two triangles sharing corners c0 and c2."""
    tris.append((mat, [c0, c1, c2]))
    tris.append((mat, [c0, c2, c3]))


def box(tris, sk, bone, mat, lo, hi, xf=None, mats=None, links_fn=None):
    """Axis aligned box (optionally transformed by xf = (R, t)) with per-face normals.
    mats: optional list of 6 materials (+x,-x,+y,-y,+z,-z).  links_fn(world_pos) -> links spec or None."""
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    faces = [
        ((1, 0, 0), [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]),
        ((-1, 0, 0), [(x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)]),
        ((0, 1, 0), [(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)]),
        ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
        ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((0, 0, -1), [(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)]),
    ]
    uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    for fi, (n, pts) in enumerate(faces):
        cs = []
        for p, uv in zip(pts, uvs):
            wp, wn = xf_corner_data(xf, p, n)
            links = links_fn(wp) if links_fn else None
            if links is None:
                cs.append(corner(bone, wp, wn, uv))
            else:
                cs.append(corner(primary_bone(links), wp, wn, uv, links))
        add_quad(tris, mats[fi] if mats else mat, *cs)


def ring_basis(axis):
    axis = vnorm(axis)
    ref = (1.0, 0.0, 0.0) if abs(axis[0]) < 0.9 else (0.0, 1.0, 0.0)
    u = vnorm(vsub(ref, vmul(axis, vdot(ref, axis))))
    v = vcross(axis, u)
    return u, v


def tube(tris, mat, centers, radii, sides, ring_corner):
    """Smooth shaded open tube through `centers`.  ring_corner(ring, side, pos, normal, uv) -> corner.
    The seam repeats side 0 with u = 1 (same position and normal, different uv)."""
    n = len(centers)
    rings = []
    for i in range(n):
        if i == 0:
            axis = vsub(centers[1], centers[0])
        elif i == n - 1:
            axis = vsub(centers[n - 1], centers[n - 2])
        else:
            axis = vsub(centers[i + 1], centers[i - 1])
        u, v = ring_basis(axis)
        pts = []
        for j in range(sides + 1):
            th = 2.0 * math.pi * (j % sides) / sides
            nrm = vadd(vmul(u, math.cos(th)), vmul(v, math.sin(th)))
            pos = vadd(centers[i], vmul(nrm, radii[i]))
            pts.append(ring_corner(i, j % sides, pos, nrm, (j / sides, i / (n - 1))))
        rings.append(pts)
    for i in range(n - 1):
        for j in range(sides):
            a, b, c, d = rings[i][j], rings[i][j + 1], rings[i + 1][j + 1], rings[i + 1][j]
            tris.append((mat, [a, b, c]))
            tris.append((mat, [a, c, d]))


def sphere(tris, mat, center, radius, segs, rings, corner_fn):
    """Lat/long sphere (poles are single triangles).  corner_fn(pos, normal, uv) -> corner."""
    def pt(k, j):
        phi = math.pi * k / rings
        th = 2.0 * math.pi * (j % segs) / segs
        nrm = (math.sin(phi) * math.cos(th), math.sin(phi) * math.sin(th), math.cos(phi))
        return vadd(center, vmul(nrm, radius)), nrm

    def c(k, j, u=None):
        p, n = pt(k, j)
        return corner_fn(p, n, ((j / segs) if u is None else u, k / rings))

    for j in range(segs):
        tris.append((mat, [c(0, j, (j + 0.5) / segs), c(1, j), c(1, j + 1)]))
    for k in range(1, rings - 1):
        for j in range(segs):
            a, b, cc, d = c(k, j), c(k, j + 1), c(k + 1, j + 1), c(k + 1, j)
            tris.append((mat, [a, d, cc]))
            tris.append((mat, [a, cc, b]))
    for j in range(segs):
        tris.append((mat, [c(rings, j, (j + 0.5) / segs), c(rings - 1, j + 1), c(rings - 1, j)]))


# ----------------------------------------------------------------------------------------------------------------------
# SMD / VTA writers
# ----------------------------------------------------------------------------------------------------------------------
def smd_reference(sk, tris):
    out = ["version 1", "nodes"]
    out.append(sk.nodes_text().rstrip("\n"))
    out += ["end", "skeleton", "time 0", sk.skeleton_frame(sk.bind).rstrip("\n"), "end", "triangles"]
    for mat, cs in tris:
        out.append(mat)
        for c in cs:
            out.append(corner_text(c))
    out.append("end")
    return "\n".join(out) + "\n"


def smd_animation(sk, frames):
    """frames: list of per-bone [(pos, rot), ...] (one entry per frame)."""
    out = ["version 1", "nodes", sk.nodes_text().rstrip("\n"), "end", "skeleton"]
    for t, fr in enumerate(frames):
        out.append("time %d" % t)
        out.append(sk.skeleton_frame(fr).rstrip("\n"))
    out.append("end")
    return "\n".join(out) + "\n"


def unique_positions(tris):
    """First-seen order of distinct (formatted) positions, with the normal of the first corner at that position."""
    seen = {}
    order = []
    for _, cs in tris:
        for c in cs:
            key = tuple(fmt(v) for v in c[1])
            if key not in seen:
                seen[key] = len(order)
                order.append((c[1], c[2]))
    return order


def vta_file(sk, tris, frames):
    """frames: list of (label, {vertex index: (pos, normal)}) -- the changed vertices of flex frames 1..K."""
    base = unique_positions(tris)
    out = ["version 1", "nodes", sk.nodes_text().rstrip("\n"), "end", "skeleton", "time 0"]
    for k, (label, _) in enumerate(frames):
        out.append("time %d # %s" % (k + 1, label))
    out += ["end", "vertexanimation", "time 0"]
    for i, (p, n) in enumerate(base):
        out.append("%d %s %s" % (i, " ".join(fmt(v) for v in p), " ".join(fmt(v) for v in n)))
    for k, (label, changed) in enumerate(frames):
        out.append("time %d" % (k + 1))
        for i in sorted(changed):
            p, n = changed[i]
            out.append("%d %s %s" % (i, " ".join(fmt(v) for v in p), " ".join(fmt(v) for v in n)))
    out.append("end")
    return "\n".join(out) + "\n"


def lerp(a, b, t):
    return a + (b - a) * t


def bind_frames(sk, count, fn):
    """Animation frames: fn(frame, bone name, (pos, rot)) -> (pos, rot)."""
    return [[fn(f, sk.names[i], sk.bind[i]) for i in range(len(sk.bones))] for f in range(count)]


def drot(local, dx=0.0, dy=0.0, dz=0.0):
    (p, r) = local
    return (p, (r[0] + dx, r[1] + dy, r[2] + dz))


def dpos(local, dx=0.0, dy=0.0, dz=0.0):
    (p, r) = local
    return ((p[0] + dx, p[1] + dy, p[2] + dz), r)


# ----------------------------------------------------------------------------------------------------------------------
# QC helper
# ----------------------------------------------------------------------------------------------------------------------
def qc_file(name, header_comment, body):
    text = "// %s\n" % header_comment
    text += '$modelname "api/%s.mdl"\n$cdmaterials "models/api"\n' % name
    text += body.strip("\n") + "\n"
    write_file("%s.qc" % name, text)


# ======================================================================================================================
# skeletons
# ======================================================================================================================
CROOT = Skeleton([("root", -1, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0))])

CHAIN = Skeleton([
    ("root", -1, (0.0, 0.0, 0.0), (0.0, 0.0, HALF_PI)),
    ("mid", 0, (0.0, 0.0, 10.0), (0.261799, 0.0, 0.0)),
    ("tip", 1, (0.0, 0.0, 10.0), (0.0, 0.261799, 0.0)),
    ("tip_end", 2, (0.0, 0.0, 6.0), (0.0, 0.0, 0.0)),
])

BIPED = Skeleton([
    ("root", -1, (0.0, 0.0, 0.0), (0.0, 0.0, HALF_PI)),
    ("pelvis", 0, (0.0, 0.0, 34.0), (0.0, 0.0, 0.0)),
    ("spine", 1, (0.0, 0.0, 10.0), (0.087266, 0.0, 0.0)),
    ("head", 2, (0.0, 0.0, 18.0), (0.0, -0.087266, 0.0)),
    ("l_arm", 2, (0.0, 8.0, 14.0), (-HALF_PI, 0.0, 0.0)),
    ("r_arm", 2, (0.0, -8.0, 14.0), (HALF_PI, 0.0, 0.0)),
    ("l_thigh", 1, (0.0, 4.0, 0.0), (0.0, PI, 0.0)),
    ("l_shin", 6, (0.0, 0.0, 16.0), (0.0, 0.174533, 0.0)),
    ("l_foot", 7, (0.0, 0.0, 16.0), (0.0, -HALF_PI, 0.0)),
    ("r_thigh", 1, (0.0, -4.0, 0.0), (0.0, PI, 0.0)),
    ("r_shin", 9, (0.0, 0.0, 16.0), (0.0, 0.174533, 0.0)),
    ("r_foot", 10, (0.0, 0.0, 16.0), (0.0, -HALF_PI, 0.0)),
])

LODSK = Skeleton([
    ("root", -1, (0.0, 0.0, 0.0), (0.0, 0.0, HALF_PI)),
    ("mid", 0, (0.0, 0.0, 12.0), (0.0, 0.261799, 0.0)),
    ("tip", 1, (0.0, 0.0, 12.0), (0.0, 0.0, 0.0)),
])

RAGDOLL = Skeleton([
    ("pelvis", -1, (0.0, 0.0, 30.0), (0.0, 0.0, HALF_PI)),
    ("spine", 0, (0.0, 0.0, 10.0), (0.0, 0.087266, 0.0)),
    ("skull", 1, (0.0, 0.0, 14.0), (0.0, 0.0, 0.0)),
])

FACERIG = Skeleton([
    ("head", -1, (0.0, 0.0, 60.0), (0.0, 0.0, HALF_PI)),
    ("jaw", 0, (0.0, 1.5, -3.0), (0.174533, 0.0, 0.0)),
    ("l_eye", 0, (-2.5, 4.0, 1.5), (0.0, 0.0, 0.0)),
    ("r_eye", 0, (2.5, 4.0, 1.5), (0.0, 0.0, 0.0)),
])


# ======================================================================================================================
# meshes
# ======================================================================================================================
def crate_mesh(mat="crate_wood"):
    tris = []
    box(tris, CROOT, 0, mat, (-8.0, -8.0, 0.0), (8.0, 8.0, 16.0))
    return tris


def multi_mesh():
    """Three materials; wall/roof/floor triangles share positions (box edges + a gable roof on top)."""
    tris = []
    sk = CROOT
    # walls: material house_wall (+x,-x,+y,-y), floor house_floor (-z), top house_roof (+z) ... plus a wedge roof
    box(tris, sk, 0, "house_wall", (-10.0, -8.0, 0.0), (10.0, 8.0, 12.0),
        mats=["house_wall", "house_wall", "house_wall", "house_wall", "house_roof", "house_floor"])
    # gable roof prism sharing the top rim of the box (material house_roof); smooth normals along the ridge
    ridge = [(-10.0, 0.0, 18.0), (10.0, 0.0, 18.0)]
    l0, l1, r0, r1 = (-10.0, -8.0, 12.0), (10.0, -8.0, 12.0), (-10.0, 8.0, 12.0), (10.0, 8.0, 12.0)
    nl = vnorm((0.0, -6.0, 8.0))
    nr = vnorm((0.0, 6.0, 8.0))
    add_quad(tris, "house_roof",
             plain(sk, "root", l0, nl, (0.0, 0.0)), plain(sk, "root", l1, nl, (1.0, 0.0)),
             plain(sk, "root", ridge[1], nl, (1.0, 1.0)), plain(sk, "root", ridge[0], nl, (0.0, 1.0)))
    add_quad(tris, "house_roof",
             plain(sk, "root", r1, nr, (0.0, 0.0)), plain(sk, "root", r0, nr, (1.0, 0.0)),
             plain(sk, "root", ridge[0], nr, (1.0, 1.0)), plain(sk, "root", ridge[1], nr, (0.0, 1.0)))
    # gable end triangles (wall material, share the box's top corners but with a different normal)
    tris.append(("house_wall", [plain(sk, "root", l1, (1.0, 0.0, 0.0), (0.0, 0.0)),
                                plain(sk, "root", r1, (1.0, 0.0, 0.0), (1.0, 0.0)),
                                plain(sk, "root", ridge[1], (1.0, 0.0, 0.0), (0.5, 1.0))]))
    tris.append(("house_wall", [plain(sk, "root", r0, (-1.0, 0.0, 0.0), (0.0, 0.0)),
                                plain(sk, "root", l0, (-1.0, 0.0, 0.0), (1.0, 0.0)),
                                plain(sk, "root", ridge[0], (-1.0, 0.0, 0.0), (0.5, 1.0))]))
    return tris


def chain_mesh(sk=CHAIN, mat="tube_skin"):
    """Smooth tube along root -> mid -> tip -> tip_end with 1, 2, 3 and 4 weights per vertex."""
    centers = [sk.pos("root"),
               vmul(vadd(sk.pos("root"), sk.pos("mid")), 0.5),
               sk.pos("mid"), sk.pos("tip"), sk.pos("tip_end")]
    radii = [4.0, 3.6, 3.2, 2.6, 1.8]
    names = sk.names

    def rc(i, j, pos, nrm, uv):
        if i == 0:
            return corner(0, pos, nrm, uv)                                    # no link list: bone 0, weight 1
        if i == 1:
            return linked(sk, pos, nrm, uv, [("root", 0.7), ("mid", 0.3)])
        if i == 2:
            if j % 3 == 0:
                return linked(sk, pos, nrm, uv, [("root", 0.3), ("mid", 0.7)])
            if j % 3 == 1:
                return linked(sk, pos, nrm, uv, [("root", 0.2), ("mid", 0.5), ("tip", 0.3)])
            return linked(sk, pos, nrm, uv, [("mid", 1.0)])                   # explicit single link
        if i == 3:
            if j == 0:
                return linked(sk, pos, nrm, uv, [("mid", 0.2), ("tip", 0.45), ("tip_end", 0.25), ("root", 0.1)])
            return linked(sk, pos, nrm, uv, [("mid", 0.4), ("tip", 0.6)])
        return linked(sk, pos, nrm, uv, [("tip", 0.25), ("tip_end", 0.75)])

    tris = []
    tube(tris, mat, centers, radii, 6, rc)
    return tris


def chain_anim(frames_n=8):
    sk = CHAIN

    def fn(f, name, local):
        ph = 2.0 * math.pi * f / frames_n
        if name == "mid":
            return drot(local, dx=0.2 * math.sin(ph))
        if name == "tip":
            return drot(local, dy=0.3 * math.sin(ph + 1.0))
        if name == "tip_end":
            return drot(local, dz=0.2 * math.sin(ph + 2.0))
        return local

    return smd_animation(sk, bind_frames(sk, frames_n, fn))


# ---- biped ---------------------------------------------------------------------------------------------------------
def biped_parts(mat_body="biped_skin", parts=("torso", "legs", "arms")):
    sk = BIPED
    tris = []
    up = (0.0, 0.0, 1.0)

    def links_ring(specs):
        def rc(i, j, pos, nrm, uv):
            spec = specs[i]
            if len(spec) == 1:
                return corner(sk.index(spec[0][0]), pos, nrm, uv)
            return linked(sk, pos, nrm, uv, spec)
        return rc

    if "torso" in parts:
        centers = [sk.pos("pelvis"), sk.pos("spine"), sk.pos("head"), vadd(sk.pos("head"), (0.0, 0.0, 10.0))]
        tube(tris, mat_body, centers, [7.0, 6.0, 4.5, 4.0], 4,
             links_ring([[("pelvis", 0.8), ("root", 0.2)], [("spine", 0.6), ("pelvis", 0.4)],
                         [("head", 0.5), ("spine", 0.5)], [("head", 1.0)]]))
    if "arms" in parts:
        for side in ("l", "r"):
            a = side + "_arm"
            centers = [sk.pos(a), sk.at(a, (0.0, 0.0, 14.0))]
            tube(tris, mat_body, centers, [2.5, 2.0], 4,
                 links_ring([[(a, 0.7), ("spine", 0.3)], [(a, 1.0)]]))
    if "legs" in parts:
        for side in ("l", "r"):
            th, sh, ft = side + "_thigh", side + "_shin", side + "_foot"
            centers = [sk.pos(th), sk.pos(sh), sk.pos(ft), sk.at(ft, (0.0, 0.0, 7.0))]
            tube(tris, mat_body, centers, [3.0, 2.6, 2.2, 2.0], 4,
                 links_ring([[(th, 0.7), ("pelvis", 0.3)], [(sh, 0.5), (th, 0.5)], [(ft, 0.6), (sh, 0.4)],
                             [(ft, 1.0)]]))
    return tris


def biped_idle(frames=10):
    sk = BIPED

    def fn(f, name, local):
        ph = 2.0 * math.pi * f / frames
        if name == "spine":
            return drot(local, dx=0.05 * math.sin(ph))
        if name == "head":
            return drot(local, dy=0.04 * math.sin(ph + 0.5))
        if name in ("l_arm", "r_arm"):
            return drot(local, dz=0.06 * math.sin(ph))
        return local

    return smd_animation(sk, bind_frames(sk, frames, fn))


def biped_walk(frames=25):
    sk = BIPED

    def fn(f, name, local):
        ph = 2.0 * math.pi * f / (frames - 1)
        if name == "root":
            return dpos(local, dx=1.5 * f, dy=0.5 * f)         # forward + sideways drift (extracted with LX LY)
        if name == "pelvis":
            return dpos(local, dz=0.8 * math.sin(2.0 * ph))
        swing = 0.5
        if name == "l_thigh":
            return drot(local, dz=swing * math.sin(ph))
        if name == "r_thigh":
            return drot(local, dz=-swing * math.sin(ph))
        if name == "l_shin":
            return drot(local, dy=0.4 * max(0.0, math.sin(ph + 1.2)))
        if name == "r_shin":
            return drot(local, dy=0.4 * max(0.0, -math.sin(ph + 1.2)))
        if name == "l_arm":
            return drot(local, dz=-0.4 * math.sin(ph))
        if name == "r_arm":
            return drot(local, dz=0.4 * math.sin(ph))
        return local

    return smd_animation(sk, bind_frames(sk, frames, fn))


def biped_lean(kind, frames=12):
    """Blend poses for blend.qc: kind -1 / 0 / +1 leans the spine and swings the legs differently."""
    sk = BIPED

    def fn(f, name, local):
        ph = 2.0 * math.pi * f / frames
        if name == "spine":
            return drot(local, dx=0.25 * kind + 0.05 * math.sin(ph))
        if name == "head":
            return drot(local, dx=-0.1 * kind)
        if name == "pelvis":
            return dpos(local, dy=1.5 * kind)
        if name == "l_thigh":
            return drot(local, dz=(0.3 + 0.2 * kind) * math.sin(ph))
        if name == "r_thigh":
            return drot(local, dz=-(0.3 + 0.2 * kind) * math.sin(ph))
        return local

    return smd_animation(sk, bind_frames(sk, frames, fn))


def biped_wave(frames=15):
    sk = BIPED

    def fn(f, name, local):
        ph = 2.0 * math.pi * f / frames
        if name == "r_arm":
            return drot(local, dy=-1.2 + 0.4 * math.sin(2.0 * ph), dz=0.3 * math.sin(ph))
        if name == "spine":
            return drot(local, dz=0.1 * math.sin(ph))
        if name == "head":
            return drot(local, dz=0.15 * math.sin(ph + 0.7))
        return local

    return smd_animation(sk, bind_frames(sk, frames, fn))


# ---- bodygroup parts -----------------------------------------------------------------------------------------------
def bg_hat(tall=False):
    sk = BIPED
    tris = []
    hp = sk.pos("head")
    top = 8.0 if tall else 4.0
    # brim (box rotated with the head bone's frame) and a crown
    rm, t = sk.world[sk.index("head")]
    xf = (rm, t)
    box(tris, sk, sk.index("head"), "bg_cloth", (-6.0, -6.0, 8.0), (6.0, 6.0, 9.0), xf=xf)
    box(tris, sk, sk.index("head"), "bg_cloth", (-3.5, -3.5, 9.0), (3.5, 3.5, 9.0 + top), xf=xf)
    return tris


def bg_gloves():
    sk = BIPED
    tris = []
    for side in ("l", "r"):
        a = side + "_arm"
        rm, t = sk.world[sk.index(a)]
        box(tris, sk, sk.index(a), "bg_glove", (-2.6, -2.6, 11.0), (2.6, 2.6, 15.0), xf=(rm, t),
            links_fn=lambda wp, a=a: [(sk.index(a), 1.0)])
    return tris


# ---- lod -----------------------------------------------------------------------------------------------------------
def lod_mesh(sides, ring_count, with_trim=True):
    sk = LODSK
    pts = [sk.pos("root"), sk.pos("mid"), sk.pos("tip")]
    # centres interpolated between the joints
    centers = []
    for i in range(ring_count):
        s = i * 2.0 / (ring_count - 1)
        seg = min(int(s), 1)
        centers.append(tuple(lerp(pts[seg][k], pts[seg + 1][k], s - seg) for k in range(3)))
    radii = [4.5 - 0.5 * i * 4.0 / max(ring_count - 1, 1) for i in range(ring_count)]

    def rc(i, j, pos, nrm, uv):
        s = i * 2.0 / (ring_count - 1)
        if s == 0.0:
            return corner(0, pos, nrm, uv)
        if s == 2.0:
            return corner(2, pos, nrm, uv)
        if s == 1.0:
            return linked(sk, pos, nrm, uv, [("root", 0.3), ("mid", 0.7)])
        if s < 1.0:
            return linked(sk, pos, nrm, uv, [("root", 1.0 - 0.7 * s), ("mid", 0.7 * s)])
        return linked(sk, pos, nrm, uv, [("mid", 0.8 - 0.3 * (s - 1.0)), ("tip", 0.2 + 0.3 * (s - 1.0))])

    tris = []
    tube(tris, "lod_skin", centers, radii, sides, rc)
    if with_trim:
        box(tris, sk, sk.index("mid"), "lod_trim", (-5.0, -5.0, sk.pos("mid")[2] - 1.0),
            (5.0, 5.0, sk.pos("mid")[2] + 1.0))
    return tris


# ---- ragdoll / collision -------------------------------------------------------------------------------------------
def ragdoll_mesh():
    sk = RAGDOLL
    centers = [vsub(sk.pos("pelvis"), (0.0, 0.0, 4.0)), sk.pos("pelvis"), sk.pos("spine"), sk.pos("skull"),
               vadd(sk.pos("skull"), (0.0, 0.0, 8.0))]
    radii = [6.0, 6.5, 5.5, 3.5, 3.0]

    def rc(i, j, pos, nrm, uv):
        spec = [[("pelvis", 1.0)], [("pelvis", 0.6), ("spine", 0.4)], [("spine", 0.6), ("pelvis", 0.4)],
                [("skull", 0.5), ("spine", 0.5)], [("skull", 1.0)]][i]
        if len(spec) == 1:
            return corner(sk.index(spec[0][0]), pos, nrm, uv)
        return linked(sk, pos, nrm, uv, spec)

    tris = []
    tube(tris, "ragdoll_skin", centers, radii, 6, rc)
    return tris


def ragdoll_phys():
    """One convex box per bone, every triangle bone-parented (corners without links)."""
    sk = RAGDOLL
    tris = []
    for name, lo, hi in (("pelvis", (-6.0, -6.0, -4.0), (6.0, 6.0, 8.0)),
                         ("spine", (-5.0, -5.0, 0.0), (5.0, 5.0, 14.0)),
                         ("skull", (-3.5, -3.5, 0.0), (3.5, 3.5, 8.0))):
        rm, t = sk.world[sk.index(name)]
        box(tris, sk, sk.index(name), "phys", lo, hi, xf=(rm, t))
    return tris


def ragdoll_idle(frames=6):
    sk = RAGDOLL

    def fn(f, name, local):
        ph = 2.0 * math.pi * f / frames
        if name == "spine":
            return drot(local, dy=0.1 * math.sin(ph))
        if name == "skull":
            return drot(local, dx=0.15 * math.sin(ph + 0.4))
        return local

    return smd_animation(sk, bind_frames(sk, frames, fn))


def phys_box_mesh():
    tris = []
    box(tris, CROOT, 0, "phys", (-8.0, -8.0, 0.0), (8.0, 8.0, 16.0))
    return tris


# ---- face / eyes ---------------------------------------------------------------------------------------------------
FACE_CENTER = None


def face_mesh():
    sk = FACERIG
    center = sk.pos("head")
    radius = 6.0
    tris = []

    def cf(pos, nrm, uv):
        # lower hemisphere partly follows the jaw
        z = pos[2] - center[2]
        if z < -2.0:
            return linked(sk, pos, nrm, uv, [("jaw", 0.7), ("head", 0.3)])
        if z < 0.0:
            return linked(sk, pos, nrm, uv, [("jaw", 0.4), ("head", 0.6)])
        return corner(0, pos, nrm, uv)

    sphere(tris, "face_skin", center, radius, 6, 4, cf)
    return tris


def face_flexes(tris):
    """Frames 1..3 of face.vta: blink (upper vertices down), smile (side vertices out/up), jaw_open (lower vertices down)."""
    base = unique_positions(tris)
    sk = FACERIG
    cz = sk.pos("head")[2]
    cy = sk.pos("head")[1]
    blink, smile, jaw = {}, {}, {}
    for i, (p, n) in enumerate(base):
        z = p[2] - cz
        if 1.0 <= z <= 4.5 and abs(p[0] - sk.pos("head")[0]) < 4.0:
            blink[i] = (vadd(p, (0.0, 0.0, -0.8)), vnorm(vadd(n, (0.0, 0.0, -0.2))))
        if -3.0 <= z <= 1.0 and abs(p[0] - sk.pos("head")[0]) > 3.0:
            dx = 0.8 if p[0] > sk.pos("head")[0] else -0.8
            smile[i] = (vadd(p, (dx, 0.0, 0.6)), vnorm(vadd(n, (dx * 0.2, 0.0, 0.1))))
        if z < -1.0:
            jaw[i] = (vadd(p, (0.0, 0.0, -1.5 + 0.3 * (z + 6.0) / 6.0)), vnorm(vadd(n, (0.0, 0.0, -0.3))))
    return [("blink", blink), ("smile", smile), ("jaw_open", jaw)]


def eyes_mesh():
    sk = FACERIG
    tris = []
    for side, bone in (("l", "l_eye"), ("r", "r_eye")):
        c = sk.pos(bone)
        bi = sk.index(bone)
        sphere(tris, "eyeball_" + side, c, 1.2, 4, 3, lambda p, n, uv, bi=bi: corner(bi, p, n, uv))
    # eyelid patches: a small fan above each eye (material eyelid_<side>), bound to the head
    for side, bone in (("l", "l_eye"), ("r", "r_eye")):
        c = sk.pos(bone)
        z0 = c[2] + 0.9
        pts = [(c[0] - 1.3, c[1] + 1.2, z0), (c[0] + 1.3, c[1] + 1.2, z0),
               (c[0] + 1.3, c[1] + 1.5, z0 + 1.4), (c[0] - 1.3, c[1] + 1.5, z0 + 1.4)]
        n = vnorm((0.0, 1.0, 0.3))
        add_quad(tris, "eyelid_" + side,
                 corner(0, pts[0], n, (0.0, 0.0)), corner(0, pts[1], n, (1.0, 0.0)),
                 corner(0, pts[2], n, (1.0, 1.0)), corner(0, pts[3], n, (0.0, 1.0)))
    return tris


def eyes_flexes(tris):
    base = unique_positions(tris)
    sk = FACERIG
    frames = []
    lids = {"l": {}, "r": {}}
    for i, (p, n) in enumerate(base):
        # lid vertices are the ones with y >= eye y + 1.2 (only the eyelid quads reach that far)
        for side, bone in (("l", "l_eye"), ("r", "r_eye")):
            c = sk.pos(bone)
            if abs(p[0] - c[0]) < 1.4 and p[1] >= c[1] + 1.19 and p[2] >= c[2] + 0.89:
                lids[side][i] = (p, n)
    for side in ("r", "l"):
        low, high = {}, {}
        for i, (p, n) in lids[side].items():
            low[i] = (vadd(p, (0.0, 0.0, -0.9)), n)
            high[i] = (vadd(p, (0.0, 0.0, 0.5)), n)
        frames.append(("upper_%s_lowerer" % ("right" if side == "r" else "left"), low))
        frames.append(("upper_%s_raiser" % ("right" if side == "r" else "left"), high))
    return frames


def face_idle(frames=6):
    sk = FACERIG

    def fn(f, name, local):
        ph = 2.0 * math.pi * f / frames
        if name == "jaw":
            return drot(local, dx=0.05 * math.sin(ph))
        if name == "head":
            return drot(local, dy=0.03 * math.sin(ph))
        return local

    return smd_animation(sk, bind_frames(sk, frames, fn))


# ---- DMX combination operator fixtures -----------------------------------------------------------------------------
# A two-row strip of 12 vertices (one bone, one material) as a keyvalues2 DMX model with delta states, a
# DmeCombinationOperator and a per-vertex balance. tests/api/test_combination.cpp builds the same strip through the
# API: keep the numbers in step (the equivalence tests fail when they differ). All values are exact in text and float.
#
# Vertex i sits in column i // 2, row i % 2. The triangles are a strip with a consistent facing: (k, k + 2, k + 1) for
# even k, (k, k + 1, k + 2) for odd k. The SMD loader (the builder's model) orders the unified vertices by last use, the
# DMX loader by first use (of the faces (a, c, b) of the polygons (a, b, c)); for this strip both orders are the vertex
# order, so the builder gets the polygons unchanged.
COMBO_VERTS = 12
COMBO_BALANCE = [0.0, 0.125, 0.25, 0.75, 0.875, 1.0]  # per column
# delta states: name -> {vertex: (dx, dy, dz)}
COMBO_SHAPES = [
    ("browDown", {1: (0, 0, -0.5), 3: (0, 0, -0.5), 5: (0, 0, -0.5), 7: (0, 0, -0.5), 9: (0, 0, -0.5), 11: (0, 0, -0.5)}),
    ("browUp", {1: (0, 0, 1), 3: (0, 0, 1), 5: (0, 0, 1), 7: (0, 0, 1), 9: (0, 0, 1), 11: (0, 0, 1)}),
    ("smile", {0: (-0.5, 0, 0.25), 1: (-0.5, 0, 0.25), 10: (0.5, 0, 0.25), 11: (0.5, 0, 0.25)}),
    ("jaw", {0: (0, -1, 0), 2: (0, -1, 0), 4: (0, -1, 0), 6: (0, -1, 0), 8: (0, -1, 0), 10: (0, -1, 0)}),
    ("oo", {4: (0, 0, 0.5), 5: (0, 0, 0.5), 6: (0, 0, 0.5), 7: (0, 0, 0.5)}),
    ("ee", {4: (0.25, 0, 0), 6: (-0.25, 0, 0)}),
    ("aa", {5: (0, -0.25, 0), 7: (0, -0.25, 0)}),
    ("lowLid", {2: (0, 0, 0.25), 4: (0, 0, 0.25)}),
    ("upLid", {3: (0, 0, -0.25), 5: (0, 0, -0.25)}),
    ("smile_jaw", {0: (0.25, 0, 0), 10: (-0.25, 0, 0)}),
    ("browUp_smile", {1: (0, 0.25, 0.25), 11: (0, 0.25, 0.25)}),
]
# controls: (name, raw controls, stereo, eyelid, (flexMin, flexMax))
COMBO_CONTROLS = [
    ("brow", ["browDown", "browUp"], False, False, (-1.0, 1.0)),
    ("smile", ["smile"], True, False, (0.0, 1.0)),
    ("jaw", ["jaw"], False, False, (0.0, 1.0)),
    ("vowel", ["oo", "ee", "aa"], False, False, (-1.0, 1.0)),
    ("lid", ["lowLid", "upLid"], False, True, (-1.0, 1.0)),
]
# dominators: (dominators, suppressed)
COMBO_DOMINATORS = [(["jaw"], ["smile"])]
# "advanced flex" rules: (kind, flex name, expression)
COMBO_FLEX_RULES = [
    ("local", "amount", None),
    ("expr", "amount", "smile * 0.5 + 0.25"),
    ("expr", "smileR", "smile"),
    ("expr", "smileL", "smile * 0.5 + %amount * 0.5"),
    ("pass", "jaw", None),
    ("expr", "browUp", "max(brow, 0)"),
    ("expr", "browDown", "max(-brow, 0)"),
]


def combo_vertex(i):
    return (2.0 * (i // 2), 2.0 * (i % 2), 0.0)


def combo_uv(i):
    return (0.125 * (i // 2), 0.5 * (i % 2))


def combo_triangles():
    """The polygons of the DMX face set; the builder test passes the same triangles."""
    return [(k, k + 2, k + 1) if k % 2 == 0 else (k, k + 1, k + 2) for k in range(COMBO_VERTS - 2)]


def dmx_id(name):
    import uuid
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "studiomdl_v2/api/fixtures/" + name))


def dmx_array(indent, key, kind, values):
    pad = "\t" * indent
    lines = ['%s"%s" "%s_array" ' % (pad, key, kind), "%s[" % pad]
    lines += ['%s\t"%s"%s' % (pad, v, "," if i + 1 < len(values) else "") for i, v in enumerate(values)]
    lines.append("%s]" % pad)
    return lines


def dmx_combo(with_flex_rules):
    """The DMX text of the combo model. with_flex_rules: the combination operator targets a DmeFlexRules ("advanced
    flex") instead of the mesh."""
    scene, model, dag, mesh, vdata = (dmx_id(n) for n in ("scene", "model", "dag", "mesh", "vdata"))
    flexrules_id = dmx_id("flexrules")
    corners = [v for tri in combo_triangles() for v in tri]
    n_verts = COMBO_VERTS
    out = ['<!-- dmx encoding keyvalues2 1 format model 18 -->']
    A = out.append

    # scene with the combination operator
    A('"DmElement"')
    A("{")
    A('\t"id" "elementid" "%s"' % scene)
    A('\t"name" "string" "Scene"')
    A('\t"skeleton" "element" "%s"' % model)
    A('\t"combinationOperator" "DmeCombinationOperator"')
    A("\t{")
    A('\t\t"id" "elementid" "%s"' % dmx_id("combinationOperator"))
    A('\t\t"name" "string" "combinationOperator"')
    A('\t\t"controls" "element_array" ')
    A("\t\t[")
    for ci, (name, raws, stereo, eyelid, (fmin, fmax)) in enumerate(COMBO_CONTROLS):
        A("\t\t\t\"DmeCombinationInputControl\"")
        A("\t\t\t{")
        A('\t\t\t\t"id" "elementid" "%s"' % dmx_id("control_" + name))
        A('\t\t\t\t"name" "string" "%s"' % name)
        out.extend(dmx_array(4, "rawControlNames", "string", raws))
        A('\t\t\t\t"stereo" "bool" "%d"' % int(stereo))
        A('\t\t\t\t"eyelid" "bool" "%d"' % int(eyelid))
        A('\t\t\t\t"flexMax" "float" "%s"' % fmt(fmax))
        A('\t\t\t\t"flexMin" "float" "%s"' % fmt(fmin))
        out.extend(dmx_array(4, "wrinkleScales", "float", ["0"] * len(raws)))
        A("\t\t\t}" + ("," if ci + 1 < len(COMBO_CONTROLS) else ""))
    A("\t\t]")
    out.extend(dmx_array(2, "controlValues", "vector3", ["0 0 0.5"] * len(COMBO_CONTROLS)))
    out.extend(dmx_array(2, "controlValuesLagged", "vector3", ["0 0 0.5"] * len(COMBO_CONTROLS)))
    A('\t\t"usesLaggedValues" "bool" "0"')
    A('\t\t"dominators" "element_array" ')
    A("\t\t[")
    for di, (dominators, suppressed) in enumerate(COMBO_DOMINATORS):
        A("\t\t\t\"DmeCombinationDominationRule\"")
        A("\t\t\t{")
        A('\t\t\t\t"id" "elementid" "%s"' % dmx_id("dominator_%d" % di))
        A('\t\t\t\t"name" "string" "dominator"')
        out.extend(dmx_array(4, "dominators", "string", dominators))
        out.extend(dmx_array(4, "suppressed", "string", suppressed))
        A("\t\t\t}" + ("," if di + 1 < len(COMBO_DOMINATORS) else ""))
    A("\t\t]")
    A('\t\t"targets" "element_array" ')
    A("\t\t[")
    A('\t\t\t"element" "%s"' % (flexrules_id if with_flex_rules else mesh))
    A("\t\t]")
    A("\t}")
    A("\t")
    A('\t"model" "element" "%s"' % model)
    A("}")
    A("")

    # model: one joint, the dag "root"
    A('"DmeModel"')
    A("{")
    A('\t"id" "elementid" "%s"' % model)
    A('\t"name" "string" "combo"')
    A('\t"children" "element_array" ')
    A("\t[")
    A('\t\t"element" "%s"' % dag)
    A("\t]")
    A('\t"baseStates" "element_array" ')
    A("\t[")
    A('\t\t"DmeTransformList"')
    A("\t\t{")
    A('\t\t\t"id" "elementid" "%s"' % dmx_id("baselist"))
    A('\t\t\t"name" "string" "base"')
    A('\t\t\t"transforms" "element_array" ')
    A("\t\t\t[")
    A('\t\t\t\t"DmeTransform"')
    A("\t\t\t\t{")
    A('\t\t\t\t\t"id" "elementid" "%s"' % dmx_id("basetransform"))
    A('\t\t\t\t\t"name" "string" "root"')
    A('\t\t\t\t\t"position" "vector3" "0 0 0"')
    A('\t\t\t\t\t"orientation" "quaternion" "0 0 0 1"')
    A("\t\t\t\t}")
    A("\t\t\t]")
    A("\t\t}")
    A("\t]")
    A('\t"transform" "DmeTransform"')
    A("\t{")
    A('\t\t"id" "elementid" "%s"' % dmx_id("modeltransform"))
    A('\t\t"name" "string" ""')
    A('\t\t"position" "vector3" "0 0 0"')
    A('\t\t"orientation" "quaternion" "0 0 0 1"')
    A("\t}")
    A("\t")
    A('\t"jointList" "element_array" ')
    A("\t[")
    A('\t\t"element" "%s"' % dag)
    A("\t]")
    A("}")
    A("")

    # dag "root" with the mesh as its shape
    A('"DmeDag"')
    A("{")
    A('\t"id" "elementid" "%s"' % dag)
    A('\t"name" "string" "root"')
    A('\t"shape" "element" "%s"' % mesh)
    A('\t"transform" "DmeTransform"')
    A("\t{")
    A('\t\t"id" "elementid" "%s"' % dmx_id("dagtransform"))
    A('\t\t"name" "string" "root"')
    A('\t\t"position" "vector3" "0 0 0"')
    A('\t\t"orientation" "quaternion" "0 0 0 1"')
    A("\t}")
    A("\t")
    A("}")
    A("")

    # mesh: face set, delta states and their (unused) weights
    A('"DmeMesh"')
    A("{")
    A('\t"id" "elementid" "%s"' % mesh)
    A('\t"name" "string" "combo"')
    A('\t"visible" "bool" "1"')
    A('\t"bindState" "element" "%s"' % vdata)
    A('\t"currentState" "element" "%s"' % vdata)
    A('\t"baseStates" "element_array" ')
    A("\t[")
    A('\t\t"element" "%s"' % vdata)
    A("\t]")
    A('\t"faceSets" "element_array" ')
    A("\t[")
    A('\t\t"DmeFaceSet"')
    A("\t\t{")
    A('\t\t\t"id" "elementid" "%s"' % dmx_id("faceset"))
    A('\t\t\t"name" "string" "combo_mat"')
    A('\t\t\t"material" "DmeMaterial"')
    A("\t\t\t{")
    A('\t\t\t\t"id" "elementid" "%s"' % dmx_id("material"))
    A('\t\t\t\t"name" "string" "combo_mat"')
    A('\t\t\t\t"mtlName" "string" "combo_mat"')
    A("\t\t\t}")
    A("\t\t\t")
    faces = []
    for t in range(len(corners) // 3):
        faces += [str(3 * t), str(3 * t + 1), str(3 * t + 2), "-1"]
    out.extend(dmx_array(3, "faces", "int", faces))
    A("\t\t}")
    A("\t]")
    A('\t"deltaStates" "element_array" ')
    A("\t[")
    for si, (name, deltas) in enumerate(COMBO_SHAPES):
        verts = sorted(deltas)
        A('\t\t"DmeVertexDeltaData"')
        A("\t\t{")
        A('\t\t\t"id" "elementid" "%s"' % dmx_id("delta_" + name))
        A('\t\t\t"name" "string" "%s"' % name)
        out.extend(dmx_array(3, "vertexFormat", "string", ["positions"]))
        out.extend(dmx_array(3, "positions", "vector3", ["%s %s %s" % tuple(fmt(c) for c in deltas[v]) for v in verts]))
        out.extend(dmx_array(3, "positionsIndices", "int", [str(v) for v in verts]))
        A("\t\t}" + ("," if si + 1 < len(COMBO_SHAPES) else ""))
    A("\t]")
    out.extend(dmx_array(1, "deltaStateWeights", "vector2", ["0 0"] * len(COMBO_SHAPES)))
    out.extend(dmx_array(1, "deltaStateWeightsLagged", "vector2", ["0 0"] * len(COMBO_SHAPES)))
    A("}")
    A("")

    # the bind state: positions, one normal, texture coordinates, balance; indices per corner
    A('"DmeVertexData"')
    A("{")
    A('\t"id" "elementid" "%s"' % vdata)
    A('\t"name" "string" "bind"')
    out.extend(dmx_array(1, "vertexFormat", "string", ["positions", "normals", "textureCoordinates", "balance"]))
    A('\t"flipVCoordinates" "bool" "1"')
    A('\t"jointCount" "int" "0"')
    out.extend(dmx_array(1, "positions", "vector3", ["%s %s %s" % tuple(fmt(c) for c in combo_vertex(i))
                                                      for i in range(n_verts)]))
    out.extend(dmx_array(1, "positionsIndices", "int", [str(v) for v in corners]))
    out.extend(dmx_array(1, "normals", "vector3", ["0 0 1"]))
    out.extend(dmx_array(1, "normalsIndices", "int", ["0"] * len(corners)))
    out.extend(dmx_array(1, "textureCoordinates", "vector2",
                         ["%s %s" % tuple(fmt(c) for c in combo_uv(i)) for i in range(n_verts)]))
    out.extend(dmx_array(1, "textureCoordinatesIndices", "int", [str(v) for v in corners]))
    out.extend(dmx_array(1, "balance", "float", [fmt(COMBO_BALANCE[i // 2]) for i in range(n_verts)]))
    out.extend(dmx_array(1, "balanceIndices", "int", [str(v) for v in corners]))
    A("}")

    if with_flex_rules:
        A("")
        A('"DmeFlexRules"')
        A("{")
        A('\t"id" "elementid" "%s"' % flexrules_id)
        A('\t"name" "string" "flexRules"')
        A('\t"deltaStates" "element_array" ')
        A("\t[")
        for ri, (kind, flex, expression) in enumerate(COMBO_FLEX_RULES):
            type_name = {"expr": "DmeFlexRuleExpression", "pass": "DmeFlexRulePassThrough",
                         "local": "DmeFlexRuleLocalVar"}[kind]
            A('\t\t"%s"' % type_name)
            A("\t\t{")
            A('\t\t\t"id" "elementid" "%s"' % dmx_id("rule_%d" % ri))
            A('\t\t\t"name" "string" "%s"' % flex)
            A('\t\t\t"result" "float" "0"')
            if kind == "expr":
                A('\t\t\t"expr" "string" "%s"' % expression)
            A("\t\t}" + ("," if ri + 1 < len(COMBO_FLEX_RULES) else ""))
        A("\t]")
        out.extend(dmx_array(1, "deltaStateWeights", "vector2", ["0 0"] * len(COMBO_FLEX_RULES)))
        A("}")
    return "\n".join(out) + "\n"


# ======================================================================================================================
# generation
# ======================================================================================================================
def main():
    # ---- prop_static -------------------------------------------------------------------------------------------
    write_file("src/crate.smd", smd_reference(CROOT, crate_mesh()))
    qc_file("prop_static", "Unskinned static prop: one material, one bone, per-face normals, $staticprop + $surfaceprop", '''
$surfaceprop "wood"
$staticprop

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30
''')

    # ---- prop_multi --------------------------------------------------------------------------------------------
    write_file("src/house.smd", smd_reference(CROOT, multi_mesh()))
    qc_file("prop_multi", "Non-static prop, three materials with triangles of different materials sharing positions", '''
$surfaceprop "wood"

$body "house" "src/house.smd"

$sequence "idle" "src/house.smd" fps 30
''')

    # ---- skinned -----------------------------------------------------------------------------------------------
    chain_tris = chain_mesh()
    write_file("src/chain_ref.smd", smd_reference(CHAIN, chain_tris))
    write_file("src/anims/chain_idle.smd", chain_anim(8))
    qc_file("skinned", "3-bone chain tube (+ end bone) with 1/2/3/4 weights per vertex, bone rotations and offsets", '''
$surfaceprop "flesh"

$body "tube" "src/chain_ref.smd"

$sequence "idle" "src/chain_ref.smd" fps 30
$sequence "wiggle" "src/anims/chain_idle.smd" fps 30 loop
''')

    # ---- bodygroups --------------------------------------------------------------------------------------------
    write_file("src/bg_base.smd", smd_reference(BIPED, biped_parts("bg_body")))
    write_file("src/bg_hat.smd", smd_reference(BIPED, bg_hat(False)))
    write_file("src/bg_hat_tall.smd", smd_reference(BIPED, bg_hat(True)))
    write_file("src/bg_gloves.smd", smd_reference(BIPED, bg_gloves()))
    qc_file("bodygroups", "$body + two $bodygroups (2 studios; studio + blank) + $bodygrouppreset + 2-family $texturegroup", '''
$surfaceprop "flesh"

$body "base" "src/bg_base.smd"

$bodygroup "hat"
{
	studio "src/bg_hat.smd"
	studio "src/bg_hat_tall.smd"
}

$bodygroup "gloves"
{
	studio "src/bg_gloves.smd"
	blank
}

$texturegroup "skins"
{
	{ "bg_body" "bg_cloth" "bg_glove" }
	{ "bg_body_alt" "bg_cloth_alt" "bg_glove_alt" }
}

$bodygrouppreset "formal"
{
	"hat" 1
	"gloves" 0
}

$bodygrouppreset "casual"
{
	"hat" 0
	"gloves" 1
}

$sequence "idle" "src/bg_base.smd" fps 30
''')

    # ---- biped based: anims, blend, rig ------------------------------------------------------------------------
    write_file("src/biped.smd", smd_reference(BIPED, biped_parts()))
    write_file("src/anims/biped_idle.smd", biped_idle(10))
    write_file("src/anims/biped_walk.smd", biped_walk(25))
    qc_file("anims", "Reference + 10 frame idle + 25 frame walk with LX LY motion extraction, $animation + $sequence pairs", '''
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$animation "a_idle" "src/anims/biped_idle.smd" fps 15 loop
$animation "@walk" "src/anims/biped_walk.smd" fps 30 loop LX LY
$animation "@ref" "src/biped.smd" fps 30

$sequence "idle" "a_idle" fadein 0.2 fadeout 0.3 snap
$sequence "walk" "@walk"
$sequence "ref" "@ref"
''')

    write_file("src/anims/lean_left.smd", biped_lean(-1))
    write_file("src/anims/lean_center.smd", biped_lean(0))
    write_file("src/anims/lean_right.smd", biped_lean(1))
    qc_file("blend", "$poseparameter + a 3-way blend sequence (blendwidth 3, blend move_x -1 1) with event and activity", '''
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$poseparameter "move_x" -1 1

$animation "a_left" "src/anims/lean_left.smd" fps 30 loop
$animation "a_center" "src/anims/lean_center.smd" fps 30 loop
$animation "a_right" "src/anims/lean_right.smd" fps 30 loop
$animation "@idle" "src/biped.smd" fps 30

$sequence "lean" {
	"a_left"
	"a_center"
	"a_right"
	blendwidth 3
	blend move_x -1 1
	activity ACT_WALK 1
	event AE_FOOTSTEP 4 "left"
	event AE_FOOTSTEP 10 "right"
	fadein 0.1
}

$sequence "idle" "@idle"
''')

    write_file("src/anims/biped_wave.smd", biped_wave(15))
    qc_file("rig", "Attachments, $hboxset/$hbox/$hgroup, $ikchain + $ikautoplaylock, $weightlist layers, $bonemerge", '''
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$attachment "head_top" "head" 0 0 12 rotate 0 90 0
$attachment "eye_pos" "head" 4 0 2 x_and_z_axes 1 0 0 0 0 1
$attachment "hand_abs" "l_arm" 0 0 14 absolute
$attachment "root_rigid" "root" 0 0 0 rigid
$attachment "foot_wa" "l_foot" 0 0 2 rotate 0 0 45 world_align

$hgroup 1 "head"
$hgroup 2 "spine"
$hgroup 3 "pelvis"
$hgroup 4 "l_arm"
$hgroup 5 "r_arm"
$hgroup 6 "l_thigh"
$hgroup 7 "r_thigh"

$hboxset "default"
$hbox 1 "head" -5 -5 0 5 5 10
$hbox 2 "spine" -7 -7 0 7 7 18 0 10 0 -1 "chest_box"
$hbox 3 "pelvis" -7 -7 -2 7 7 10
$hbox 4 "l_arm" -2.5 -2.5 0 2.5 2.5 14
$hbox 5 "r_arm" -2.5 -2.5 0 2.5 2.5 14
$hbox 6 "l_thigh" -3 -3 0 3 3 16
$hbox 7 "r_thigh" -3 -3 0 3 3 16

$hboxset "crouch"
$hbox 1 "head" -5 -5 0 5 5 10
$hbox 3 "pelvis" -7 -7 -2 7 7 10

$bonemerge "head"

$weightlist "upperbody" {
	"root" 0
	"pelvis" 0
	"spine" 1
	"head" 1
	"l_arm" 1
	"r_arm" 1
}

$animation "@idle" "src/biped.smd" fps 30
$animation "@wave" "src/anims/biped_wave.smd" fps 30 weightlist "upperbody"
$animation "@wave_layered" "src/anims/biped_wave.smd" fps 30 weightlist "upperbody"

$sequence "idle" "@idle"
$sequence "wave" "@wave"
$sequence "wave_layered" {
	"@wave_layered"
	addlayer "idle"
	blendlayer "wave" 0 2 10 14 xfade
}

$ikchain "l_leg" "l_foot" knee 1 0 0 height 18 floor 0 pad 8
$ikchain "r_leg" "r_foot" knee 1 0 0 center 0 0 0
$ikautoplaylock "l_leg" 1.0 0.1
$ikautoplaylock "r_leg" 0.9 0.2
''')

    qc_file("anim_cmds", "Animation options and commands: loop/fixup/compress, subtract, align, ik rules, rotate/counterrotate, forceboneposrot, bonedriver, localhierarchy, weight lists", '''
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$weightlist "upper" {
	"root" 0
	"spine" 1 posweight 0.5
	"head" 1
}
$defaultweightlist {
	"root" 1
	"pelvis" 1
}

$attachment "muzzle" "r_arm" 0 0 10
$ikchain "l_leg" "l_foot" knee 1 0 0 height 18 floor 0 pad 8
$ikchain "r_leg" "r_foot" knee 1 0 0 center 0 0 0

$animation "ref_pose" "src/biped.smd" fps 30
$animation "a_idle" "src/anims/biped_idle.smd" fps 24 loop fixuploop 8 9 noautoik
$animation "a_walk" "src/anims/biped_walk.smd" fps 30 loop LX LY origin 1 2 3 rotate 90 scale 1.5 motionrollback 0.5 walkframe 12 X Y post
$animation "a_wave" "src/anims/biped_wave.smd" fps 30 subtract ref_pose 0 weightlist "upper" frame 2 12
$animation "a_aligned" "src/anims/lean_center.smd" fps 30 alignto a_walk alignbone "pelvis" a_walk X Y 0 5 snap
$animation "a_ik" "src/anims/lean_left.smd" fps 30 startloop 3 nostallframes 4 noanimblock noanimblockstall angles 10 20 30
$animation "a_ik_rules" "src/anims/lean_right.smd" fps 30 percentstartloop 0.25 fudgeloop blockname BindPose framestart 1 ikrule "l_leg" footstep range 0 3 7 11 floor 1 height 10 pad 6 contact 2 target 1 usesequence ikrule "r_leg" touch "l_foot" range 0 2 4 9 radius 3 usesource ikrule "l_leg" release range 4 5 6 7 ikfixup "r_leg" attachment "muzzle" range 0 1 2 3 fakeorigin 1 2 3 fakerotate 10 20 30 ikrule "r_leg" unlatch range 1 2 3 4
$animation "a_bones" "src/anims/lean_center.smd" fps 30 rotateto 45 counterrotate "head" counterrotateto 0 90 0 "spine" forceboneposrot "head" pos 0 0 1 rot 0 30 0 local bonedriver "head" axis y value 1 range 0 1 2 3 bonedriver "spine" localhierarchy "l_arm" "spine" range 1 2 3 4 localhierarchy "r_arm" "spine"
$animation "a_misc" "src/anims/lean_left.smd" fps 30 derivative 2 lineardelta
$animation "a_spline" "src/anims/lean_right.smd" fps 30 splinedelta numframes 6 reverse
$animation "a_appended" "src/anims/lean_center.smd" fps 30 appendanim a_idle matchblend a_walk 0 0 0 3 match a_walk worldspaceblend a_walk worldspaceblendloop a_walk 2 walkalignto 6 a_walk LX walkalign 8 a_walk LX LY 0 4 noanimation noanim_keepduration

$sequence "idle" "a_idle"
$sequence "walk" "a_walk"
$sequence "wave" "a_wave"
$sequence "aligned" "a_aligned"
$sequence "ik" "a_ik"
$sequence "ik_rules" "a_ik_rules"
$sequence "bones" "a_bones"
$sequence "misc" "a_misc"
$sequence "spline" "a_spline"
$sequence "appended" "a_appended"
$sequence "ref" "ref_pose"
''')

    qc_file("anim_seq", "Sequence options: activity, modifiers, events, tags, fades, flags, nodes/transitions, layers, 2D blend with pose parameters, pose cycle, blendref/comp/center, iklock, keyvalues", '''
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$poseparameter "move_x" -1 1
$poseparameter "turn" -180 180 loop 360
$poseparameter "aim" 0 90 wrap
$attachment "muzzle" "r_arm" 0 0 10
$ikchain "l_leg" "l_foot" knee 1 0 0 height 18 floor 0 pad 8

$animation "base" "src/anims/lean_center.smd" fps 30 loop
$animation "g0" "src/anims/lean_left.smd" fps 30 loop
$animation "g1" "src/anims/lean_right.smd" fps 30 loop
$animation "g2" "src/anims/lean_center.smd" fps 30 loop
$animation "g3" "src/anims/lean_left.smd" fps 30 loop
$animation "single" "src/anims/biped_walk.smd" fps 30 loop

$sequence "walk" {
	"single"
	activity ACT_WALK 2
	activitymodifier "crouch"
	actmod "sneak"
	event AE_FOOTSTEP 4 "left"
	event 5004 10
	animtag "step" 0.5
	fadein 0.3
	fadeout 0.4
	node "stand"
	exitphase 0.5
	hidden
	realtime
	snap
	post
	iklock "l_leg" 1 0.5
	keyvalues {
		"bounds" { "min" "0 0 0" "max" "1 1 1" }
	}
}

$sequence "grid" {
	"g0"
	"g1"
	"g2"
	"g3"
	blendwidth 2
	blend move_x -1 1
	blend turn -180 180
	blendref base
	blendcomp base
	blendcenter g2
}

$sequence "calc" {
	"g0"
	"g1"
	"g2"
	blendwidth 3
	blend aim 0 90
	posecycle move_x
}

$sequence "trans" {
	"base"
	transition "stand" "crouch"
	autoplay
}
$sequence "rtrans" {
	"base"
	rtransition "crouch" "stand"
	delta
}
$sequence "world" {
	"base"
	worldspace
	rootdriver "root"
}
$sequence "worldrel" {
	"base"
	worldrelative
	predelta
	addlayer "walk" local
	addlayer "trans"
	blendlayer "walk" 0 2 6 8 spline noblend local poseparameter move_x
	blendlayer "trans" 1 2 3 4
}
''')

    qc_file("anim_error_activity", "FAILS ON PURPOSE (zero activity weight): the error must carry the QC file and line", '''
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$sequence "idle" "src/biped.smd" fps 30
$sequence "walk" "src/biped.smd" fps 30 activity ACT_WALK 0
''')

    # ---- lod ---------------------------------------------------------------------------------------------------
    write_file("src/lod_hi.smd", smd_reference(LODSK, lod_mesh(6, 7)))
    write_file("src/lod_mid.smd", smd_reference(LODSK, lod_mesh(4, 5)))
    write_file("src/lod_low.smd", smd_reference(LODSK, lod_mesh(3, 3, with_trim=False)))
    write_file("src/lod_shadow.smd", smd_reference(LODSK, lod_mesh(3, 2, with_trim=False)))
    qc_file("lod", "$lod with replacemodel (fewer triangles, same skeleton), removemesh, $shadowlod", '''
$surfaceprop "metal"

$body "lod_body" "src/lod_hi.smd"

$lod 20
{
	replacemodel "src/lod_hi" "src/lod_mid.smd"
}

$lod 50
{
	replacemodel "src/lod_hi" "src/lod_low.smd"
	removemesh "lod_trim"
}

$shadowlod
{
	replacemodel "src/lod_hi" "src/lod_shadow.smd"
}

$sequence "idle" "src/lod_hi.smd" fps 30
''')

    # LOD options: replacebone, bonetreecollapse, replacematerial, removemesh with the mesh still present in the LOD
    # source, nofacial/facial, $shadowlod with use_shadowlod_materials. Options are listed in the order the builder
    # applies them (replacemodel, removemodel, replacebone, bonetreecollapse, replacematerial, removemesh, facial).
    qc_file("lod_options", "$lod options: replacebone, bonetreecollapse, replacematerial, removemesh (mesh present), nofacial, use_shadowlod_materials", '''
$surfaceprop "metal"

$body "lod_body" "src/lod_hi.smd"

$lod 15
{
	replacemodel "src/lod_hi" "src/lod_mid.smd" reverse
	replacebone "tip" "mid"
	replacematerial "lod_skin" "lod_skin_far"
	removemesh "lod_trim"
	nofacial
}

$lod 40
{
	replacemodel "src/lod_hi" "src/lod_low.smd"
	bonetreecollapse "mid"
	facial
}

$shadowlod
{
	replacemodel "src/lod_hi" "src/lod_shadow.smd"
	use_shadowlod_materials
}

$sequence "idle" "src/lod_hi.smd" fps 30
''')

    # removemodel: the LOD stops drawing one of two bodies
    qc_file("lod_models", "$lod with replacemodel and removemodel of a second body", '''
$surfaceprop "metal"

$body "lod_body" "src/lod_hi.smd"
$body "lod_extra" "src/lod_low.smd"

$lod 25
{
	replacemodel "src/lod_hi" "src/lod_mid.smd"
	removemodel "src/lod_low"
}

$sequence "idle" "src/lod_hi.smd" fps 30
''')

    # ---- collision_single --------------------------------------------------------------------------------------
    write_file("src/phys_box.smd", smd_reference(CROOT, phys_box_mesh()))
    qc_file("collision_single", "Prop with a single convex bone-parented $collisionmodel and $mass", '''
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel "src/phys_box.smd"
{
	$mass 50
	$inertia 3
	$damping 0.1
	$rotdamping 1.5
}
''')

    # ---- collision_joints --------------------------------------------------------------------------------------
    write_file("src/ragdoll_ref.smd", smd_reference(RAGDOLL, ragdoll_mesh()))
    write_file("src/ragdoll_phys.smd", smd_reference(RAGDOLL, ragdoll_phys()))
    write_file("src/anims/ragdoll_idle.smd", ragdoll_idle(6))
    qc_file("collision_joints", "Ragdoll: $collisionjoints with a 3-bone physics SMD, $mass, $jointconstrain, $rootbone", '''
$surfaceprop "flesh"

$body "ragdoll" "src/ragdoll_ref.smd"

$sequence "idle" "src/anims/ragdoll_idle.smd" fps 30 loop

$collisionjoints "src/ragdoll_phys.smd"
{
	$mass 40
	$rootbone "pelvis"
	$jointconstrain "spine" x limit -20 20 0.1
	$jointconstrain "spine" y limit -30 30 0.1
	$jointconstrain "spine" z fixed 0 0 0
	$jointconstrain "skull" x limit -40 40 0.2
	$jointconstrain "skull" y free 0 0 0.2
	$jointconstrain "skull" z limit -45 45 0.2
	$jointmassbias "skull" 1.5
	$jointdamping "spine" 0.2
}
''')

    # every whole-model option of $collisionmodel (a single body)
    qc_file("collision_options", "$collisionmodel with every whole-model option", '''
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel "src/phys_box.smd"
{
	$mass 25
	$inertia 2.5
	$damping 0.2
	$rotdamping 0.7
	$drag 0.3
	$rollingDrag 0.5
	$maxconvexpieces 4
	$remove2d
	$weldposition 0.02
	$weldnormal 0.95
	$concave
	$convexhullcountoverride 1
	$masscenter 0 0 8
	$assumeworldspace
}
''')

    # $automass (no $mass)
    qc_file("collision_automass", "$collisionmodel with $automass", '''
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel "src/phys_box.smd"
{
	$automass
}
''')

    # empty root collision + convex sources ($addconvexsrc: plain, then with offset and concave)
    qc_file("collision_convex", "$collisionmodel blank with two $addconvexsrc (offset, concave)", '''
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel blank
{
	$mass 30
	$addconvexsrc "src/phys_box"
	$addconvexsrc "src/phys_box" "offset pos[ 0 0 20 ] angle[ 0 45 0 ] scale[ 0.5 ]" concave
}
''')

    # ragdoll with every joint command that keeps the three joints
    qc_file("collision_joints_full", "$collisionjoints with the joint properties, collide pairs, no self collisions, animated friction", '''
$surfaceprop "flesh"

$body "ragdoll" "src/ragdoll_ref.smd"

$sequence "idle" "src/anims/ragdoll_idle.smd" fps 30 loop

$collisionjoints "src/ragdoll_phys.smd"
{
	$automass
	$inertia 4
	$damping 0.1
	$rotdamping 0.2
	$drag 0.05
	$maxconvexpieces 8
	$weldposition 0.03
	$concaveperjoint
	$convexhullcountoverride 1
	$noselfcollisions
	$rootbone "pelvis"
	$animatedfriction 10 100 0.5 1.0 0.5
	$jointinertia "spine" 2
	$jointdamping "skull" 0.3
	$jointrotdamping "spine" 0.4
	$jointmassbias "pelvis" 2
	$jointconstrain "spine" x limit -20 20 0.1
	$jointconstrain "spine" y free 0 0 0.2
	$jointconstrain "spine" z fixed 0 0 0
	$jointconstrain "skull" x limit -10 10
	$jointcollide "pelvis" "skull"
	$jointcollidealltoall
	{
		"pelvis"
		"spine"
		"skull"
	}
	$jointnocollide "spine" "skull"
}
''')

    qc_file("collision_joints_merge", "$collisionjoints with $jointmerge", '''
$surfaceprop "flesh"

$body "ragdoll" "src/ragdoll_ref.smd"

$sequence "idle" "src/anims/ragdoll_idle.smd" fps 30 loop

$collisionjoints "src/ragdoll_phys.smd"
{
	$mass 40
	$jointmerge "spine" "skull"
	$jointconstrain "spine" x limit -20 20 0.1
}
''')

    qc_file("collision_joints_skip", "$collisionjoints with $jointskip", '''
$surfaceprop "flesh"

$body "ragdoll" "src/ragdoll_ref.smd"

$sequence "idle" "src/anims/ragdoll_idle.smd" fps 30 loop

$collisionjoints "src/ragdoll_phys.smd"
{
	$mass 40
	$jointskip "skull"
	$jointconstrain "spine" x limit -20 20 0.1
}
''')

    # the collision source is the body's own SMD
    qc_file("collision_mesh", "$collisionmodel of the model's own SMD", '''
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel "src/crate.smd"
{
	$mass 50
}
''')

    # ---- jiggle ------------------------------------------------------------------------------------------------
    qc_file("jiggle", "$jigglebone on a skinned chain: is_flexible (+ has_base_spring) and is_rigid", '''
$surfaceprop "flesh"

$body "tube" "src/chain_ref.smd"

$jigglebone "tip"
{
	is_flexible
	{
		length 6
		tip_mass 40
		pitch_stiffness 100
		pitch_damping 7
		yaw_stiffness 100
		yaw_damping 7
		allow_length_flex
		along_stiffness 100
		along_damping 0
		angle_constraint 30
	}
	has_base_spring
	{
		stiffness 100
		damping 5
		left_constraint -10 10
		up_constraint -10 10
		forward_constraint -10 10
		base_mass 5
	}
}

$jigglebone "tip_end"
{
	is_rigid
	{
		length 4
		tip_mass 10
		yaw_constraint -20 20
		yaw_friction 0.3
		yaw_bounce 0.1
		pitch_constraint -25 25
		pitch_friction 0.3
		pitch_bounce 0.1
	}
}

$sequence "idle" "src/chain_ref.smd" fps 30
$sequence "wiggle" "src/anims/chain_idle.smd" fps 30 loop
''')

    # ---- flex ------------------------------------------------------------------------------------------------
    face = face_mesh()
    write_file("src/face.smd", smd_reference(FACERIG, face))
    write_file("src/face.vta", vta_file(FACERIG, face, face_flexes(face)))
    eyes = eyes_mesh()
    write_file("src/eyes.smd", smd_reference(FACERIG, eyes))
    write_file("src/eyes.vta", vta_file(FACERIG, eyes, eyes_flexes(eyes)))
    write_file("src/anims/face_idle.smd", face_idle(6))
    # The model-wide lines (localvar, mouth, flexcontroller, %rules) close the last $model block: the builder applies
    # the options of every mesh first, then the model's local variables, mouths, controllers and rules, so the flex
    # numbers come out the same (api/src/apply_flex.cpp).
    qc_file("flex", "Old-style VTA flexes (flexfile/defaultflex/flex/flexpair/flexcontroller/rules/localvar), eyeballs, eyelids, mouth", '''
$surfaceprop "flesh"

$model "face" "src/face.smd" {
	flexfile "src/face.vta"
	{
		defaultflex frame 0
		flex "blink" frame 1
		flexpair "smile" 0.4 frame 2
		flex "jaw_open" frame 3
	}
}

$model "eyes" "src/eyes.smd" {
	eyeball "right_eye" "r_eye" 2.5 4 1.5 "eyeball_r" 2.4 4 "iris_r" 0.9
	eyeball "left_eye" "l_eye" -2.5 4 1.5 "eyeball_l" 2.4 4 "iris_l" 0.9
	eyelid upper_right "src/eyes.vta" lowerer 1 -0.8 neutral 0 0 raiser 2 0.5 split 0.1 eyeball "right_eye"
	eyelid upper_left "src/eyes.vta" lowerer 3 -0.8 neutral 0 0 raiser 4 0.5 split 0.1 eyeball "left_eye"

	localvar smile_amount

	mouth 0 "mouth_open" "jaw" 0 1 0

	flexcontroller default range 0 1 blink_ctl smile_ctl jaw_ctl
	flexcontroller default range -1 1 brow_ctl

	%blink = blink_ctl
	%smile_amount = smile_ctl * 0.5 + 0.25
	%smileR = smile_ctl
	%smileL = smile_ctl * 0.5 + %smile_amount * 0.5
	%jaw_open = jaw_ctl + 0.25 * %blink
}

$animation "a_idle" "src/anims/face_idle.smd" fps 30 loop
$sequence "idle" "a_idle"
''')

    # multi-position flex, split / decay, sphere normals, lower eyelid, bone flex driver, min/max/unary minus rules
    qc_file("flex_more", "Flex options not in flex.qc: position/decay/split, spherenormals, noautodmxrules, lower eyelid, $boneflexdriver, functions in rules", '''
$surfaceprop "flesh"

$model "face" "src/face.smd" {
	flexfile "src/face.vta"
	{
		flex "squeeze" frame 1 position 0.5 decay 0.5
		flex "squeeze" frame 2
		flexpair "wide" 0.2 frame 3 decay 0.5
		flex "narrow" frame 3 split 0.3 position 0.75
	}
	spherenormals "face_skin" 0 60 0
	noautodmxrules
}

$model "eyes" "src/eyes.smd" {
	eyeball "right_eye" "r_eye" 2.5 4 1.5 "eyeball_r" 2.4 4 "iris_r" 0.9
	eyeball "left_eye" "l_eye" -2.5 4 1.5 "eyeball_l" 2.4 4 "iris_l" 0.9
	eyelid lower_right "src/eyes.vta" lowerer 1 -0.6 neutral 0 0 raiser 2 0.4 eyeball "right_eye"
	eyelid lower_left "src/eyes.vta" lowerer 3 -0.6 neutral 0 0 raiser 4 0.4

	flexcontroller default range 0 1 squeeze_ctl wide_ctl
	flexcontroller eyes range -1 1 look_ctl

	%squeeze = max(squeeze_ctl, wide_ctl * 0.5)
	%wideR = min(wide_ctl, 0.8) + -look_ctl
	%wideL = (wide_ctl - 0.1) / 2
	%narrow = squeeze_ctl * (1 - wide_ctl)
}

$boneflexdriver "jaw" tz squeeze_ctl 0 1.5
$boneflexdriver "jaw" tx wide_ctl -1 1

$animation "a_idle" "src/anims/face_idle.smd" fps 30 loop
$sequence "idle" "a_idle"
''')

    # ---- DMX combination operator / vertex cache animation -------------------------------------------------------
    write_file("src/combo.dmx", dmx_combo(False))
    write_file("src/combo_rules.dmx", dmx_combo(True))
    qc_file("combo", "DMX combination operator: stereo/n-way/eyelid controls, corrective shapes, a dominator, balance", '''
$surfaceprop "flesh"
$includemodel "api/none.mdl"

$body "combo" "src/combo.dmx"
''')
    qc_file("combo_rules", "DMX combination operator with flex rules (advanced flex): controllers with ranges, expressions", '''
$surfaceprop "flesh"
$includemodel "api/none.mdl"

$body "combo" "src/combo_rules.dmx"
''')
    qc_file("vca", "Vertex cache animation: $model vcafile (the frames of a VTA played by one n-way controller)", '''
$surfaceprop "flesh"

$model "face" "src/face.smd" {
	vcafile "src/face.vta"
}

$animation "a_idle" "src/anims/face_idle.smd" fps 30 loop
$sequence "idle" "a_idle"
''')


if __name__ == "__main__":
    main()
