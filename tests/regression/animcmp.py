"""Decodes every animation of two compiled MDLs (frame by frame, bone by bone, like bone_decode.cpp) and compares them.
usage: animcmp.py a.mdl b.mdl"""
import math
import os
import struct
import sys

RAWPOS, RAWROT, ANIMPOS, ANIMROT, DELTA, RAWROT2 = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20


def half(h):
    return struct.unpack("<e", struct.pack("<H", h))[0]


def cstr(d, o):
    return d[o:d.index(b"\0", o)].decode("latin-1")


def euler_to_quat(a):  # AngleQuaternion(RadianEuler)
    sr, cr = math.sin(a[0] * 0.5), math.cos(a[0] * 0.5)
    sp, cp = math.sin(a[1] * 0.5), math.cos(a[1] * 0.5)
    sy, cy = math.sin(a[2] * 0.5), math.cos(a[2] * 0.5)
    srXcp, crXsp = sr * cp, cr * sp
    crXcp, srXsp = cr * cp, sr * sp
    return (srXcp * cy - crXsp * sy, crXsp * cy + srXcp * sy, crXcp * sy - srXsp * cy, crXcp * cy + srXsp * sy)


class Model:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        ani = os.path.splitext(path)[0] + ".ani"
        self.ani = open(ani, "rb").read() if os.path.exists(ani) else b""
        i = lambda o: struct.unpack_from("<i", self.d, o)[0]
        self.bones = []
        for b in range(i(156)):
            o = i(160) + b * 216
            self.bones.append(dict(
                name=cstr(self.d, o + i(o)),
                pos=struct.unpack_from("<3f", self.d, o + 32), quat=struct.unpack_from("<4f", self.d, o + 44),
                rot=struct.unpack_from("<3f", self.d, o + 60), posscale=struct.unpack_from("<3f", self.d, o + 72),
                rotscale=struct.unpack_from("<3f", self.d, o + 84)))
        self.blocks = [struct.unpack_from("<2i", self.d, i(356) + k * 8) for k in range(i(352))]
        self.anims = {}
        for a in range(i(180)):
            o = i(184) + a * 100
            f = struct.unpack_from("<iifiiii", self.d, o)
            desc = dict(off=o, name=cstr(self.d, o + f[1]), fps=f[2], flags=f[3], numframes=f[4], nummovements=f[5],
                        animblock=i(o + 52), animindex=i(o + 56), numikrules=i(o + 60), sectionindex=i(o + 80),
                        sectionframes=i(o + 84))
            self.anims[desc["name"]] = desc

    def block_data(self, desc, block, index):
        if block == 0:
            return self.d, desc["off"] + index
        return self.ani, self.blocks[block][0] + index

    def anim_at(self, desc, frame):  # mstudioanimdesc_t::pAnim
        block, index = desc["animblock"], desc["animindex"]
        sf = desc["sectionframes"]
        if sf:
            if desc["numframes"] > sf and frame == desc["numframes"] - 1:
                frame, section = 0, desc["numframes"] // sf + 1
            else:
                section = frame // sf
                frame -= section * sf
            block, index = struct.unpack_from("<2i", self.d, desc["off"] + desc["sectionindex"] + section * 8)
        data, o = self.block_data(desc, block, index)
        return data, o, frame

    @staticmethod
    def value(data, o, frame, scale):  # ExtractAnimValue
        k = frame
        while True:
            valid, total = data[o], data[o + 1]
            if total > k:
                break
            k -= total
            o += (valid + 1) * 2
            if total == 0:
                return 0.0
        idx = k + 1 if valid > k else valid
        return struct.unpack_from("<h", data, o + idx * 2)[0] * scale

    def pose(self, name, frame):
        desc = self.anims[name]
        data, o, local = self.anim_at(desc, frame)
        out = {b: (bone["pos"], bone["quat"]) for b, bone in enumerate(self.bones)}
        if not data:
            return out
        if desc["flags"] & 0x40:  # STUDIO_FRAMEANIM: mstudio_frame_anim_t, frame x bone
            return self.frame_pose(desc, data, o, local, out)
        while True:
            bone, flags, nxt = struct.unpack_from("<BBh", data, o)
            if bone == 255:  # no (more) animated bones
                break
            bd = self.bones[bone]
            p = o + 4
            q = bd["quat"]
            if flags & RAWROT:
                x, y, zw = struct.unpack_from("<HHH", data, p)
                qx, qy, qz = (x - 32768) / 32768.0, (y - 32768) / 32768.0, ((zw & 0x7FFF) - 16384) / 16384.0
                qw = math.sqrt(max(0.0, 1 - qx * qx - qy * qy - qz * qz)) * (-1 if zw & 0x8000 else 1)
                q = (qx, qy, qz, qw)
                p += 6
            elif flags & RAWROT2:
                v = struct.unpack_from("<Q", data, p)[0]
                qx = ((v & 0x1FFFFF) - 1048576) / 1048576.5
                qy = (((v >> 21) & 0x1FFFFF) - 1048576) / 1048576.5
                qz = (((v >> 42) & 0x1FFFFF) - 1048576) / 1048576.5
                qw = math.sqrt(max(0.0, 1 - qx * qx - qy * qy - qz * qz)) * (-1 if v >> 63 else 1)
                q = (qx, qy, qz, qw)
                p += 8
            elif flags & ANIMROT:
                ptrs = struct.unpack_from("<3h", data, p)
                ang = [Model.value(data, p + ptrs[j], local, bd["rotscale"][j]) if ptrs[j] else 0.0 for j in range(3)]
                if not flags & DELTA:
                    ang = [ang[j] + bd["rot"][j] for j in range(3)]
                q = euler_to_quat(ang)
                p += 6
            pos = bd["pos"]
            if flags & RAWPOS:
                pos = tuple(half(h) for h in struct.unpack_from("<3H", data, p))
            elif flags & ANIMPOS:
                ptrs = struct.unpack_from("<3h", data, p)
                v = [Model.value(data, p + ptrs[j], local, bd["posscale"][j]) if ptrs[j] else 0.0 for j in range(3)]
                pos = tuple(v[j] + (0.0 if flags & DELTA else bd["pos"][j]) for j in range(3))
            out[bone] = (pos, q)
            if nxt == 0:
                break
            o += nxt
        return out


def quat48(data, p):
    x, y, zw = struct.unpack_from("<HHH", data, p)
    qx, qy, qz = (x - 32768) / 32768.0, (y - 32768) / 32768.0, ((zw & 0x7FFF) - 16384) / 16384.0
    return (qx, qy, qz, math.sqrt(max(0.0, 1 - qx * qx - qy * qy - qz * qz)) * (-1 if zw & 0x8000 else 1))


def quat48s(data, p):
    w0, w1, w2 = struct.unpack_from("<HHH", data, p)
    q = [0.0] * 4
    ia = ((w1 >> 15) & 1) + ((w0 >> 15) & 1) * 2
    ib, ic, id_ = (ia + 1) % 4, (ia + 2) % 4, (ia + 3) % 4
    q[ia], q[ib], q[ic] = [((w & 0x7FFF) - 16384) / 23168.0 for w in (w0, w1, w2)]
    q[id_] = math.sqrt(max(0.0, 1 - q[ia] ** 2 - q[ib] ** 2 - q[ic] ** 2)) * (-1 if w2 >> 15 else 1)
    return tuple(q)


def vec48(data, p):
    return tuple(half(h) for h in struct.unpack_from("<3H", data, p))


def frame_pose_impl(self, desc, data, o, local, out):
    constants, frameoffset, framelength = struct.unpack_from("<3i", data, o)
    cp = o + constants
    fp = o + frameoffset + local * framelength
    delta = bool(desc["flags"] & 0x0004)
    for bone in range(len(self.bones)):
        flags = data[o + 24 + bone]
        pos, q = (0.0, 0.0, 0.0) if delta else self.bones[bone]["pos"], (0.0, 0.0, 0.0, 1.0) if delta else self.bones[bone]["quat"]
        if flags & 0x08:
            q = quat48(data, fp); fp += 6
        elif flags & 0x80:
            q = quat48s(data, fp); fp += 6
        elif flags & 0x02:
            q = quat48(data, cp); cp += 6
        elif flags & 0x40:
            q = quat48s(data, cp); cp += 6
        if flags & 0x04:
            pos = vec48(data, fp); fp += 6
        elif flags & 0x01:
            pos = vec48(data, cp); cp += 6
        elif flags & 0x10:
            pos = struct.unpack_from("<3f", data, fp); fp += 12
        elif flags & 0x20:
            pos = struct.unpack_from("<3f", data, cp); cp += 12
        out[bone] = (pos, q)
    return out


Model.frame_pose = frame_pose_impl


def qdist(a, b):  # angle between unit quaternions, degrees (atan2 form: stable near 0)
    na, nb = math.sqrt(sum(x * x for x in a)), math.sqrt(sum(x * x for x in b))
    a, b = [x / na for x in a], [x / nb for x in b]
    if sum(x * y for x, y in zip(a, b)) < 0:
        b = [-x for x in b]
    d = math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))
    t = math.sqrt(sum((x + y) ** 2 for x, y in zip(a, b)))
    return math.degrees(4 * math.atan2(d, t))


def main():
    # --pos-tol / --rot-tol (degrees): exit 1 if any animation differs by more, or bones/animations/metadata differ
    args = sys.argv[1:]
    tol = {}
    for opt in ("--pos-tol", "--rot-tol"):
        if opt in args:
            i = args.index(opt)
            tol[opt] = float(args[i + 1])
            del args[i:i + 2]
    a, b = Model(args[0]), Model(args[1])
    names_a, names_b = [x["name"] for x in a.bones], [x["name"] for x in b.bones]
    mismatch = False
    if names_a != names_b:
        print("bone lists differ")
        mismatch = True
    if set(a.anims) != set(b.anims):
        print("animation sets differ:", sorted(set(a.anims) ^ set(b.anims)))
        mismatch = True
    worst_pos = worst_rot = 0.0
    bad = 0
    for name in sorted(set(a.anims) & set(b.anims)):
        da, db = a.anims[name], b.anims[name]
        meta = [k for k in ("fps", "flags", "numframes", "nummovements", "numikrules") if da[k] != db[k]]
        mismatch |= bool(meta)
        anim_pos = anim_rot = 0.0
        for frame in range(min(da["numframes"], db["numframes"])):
            pa, pb = a.pose(name, frame), b.pose(name, frame)
            for bone in pa:
                anim_pos = max(anim_pos, max(abs(x - y) for x, y in zip(pa[bone][0], pb[bone][0])))
                anim_rot = max(anim_rot, qdist(pa[bone][1], pb[bone][1]))
        worst_pos, worst_rot = max(worst_pos, anim_pos), max(worst_rot, anim_rot)
        flag = "" if not meta and anim_pos < 1e-3 and anim_rot < 0.05 else "  <-- DIFF"
        bad += bool(flag)
        print("  %-28s frames %4d  fps %5.1f  sections %3d/%3d  max pos diff %.6f  max rot diff %.4f deg %s%s" % (
            name, da["numframes"], da["fps"], da["sectionframes"], db["sectionframes"], anim_pos, anim_rot,
            ("meta differs: " + ", ".join("%s %r/%r" % (k, da[k], db[k]) for k in meta)) if meta else "", flag))
    print("  => %d animations, %d differ; worst pos %.6f, worst rot %.4f deg" % (len(a.anims), bad, worst_pos, worst_rot))
    if tol:
        over = mismatch or worst_pos > tol.get("--pos-tol", float("inf")) or worst_rot > tol.get("--rot-tol", float("inf"))
        sys.exit(1 if over else 0)


main()
