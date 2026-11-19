"""Structural summary of a compiled model (MDL v49 + VVD + dx90 VTX), independent of bone order and byte layout.

Used by sfm_diff.py to compare studiomdl_v2 output with Valve's studiomdl. Bone references are resolved to bone names,
vertices are compared as sorted, rounded tuples, so two compilers that produce the same model in a different order
compare equal.

    python3 mdl_struct.py <path/to/model.mdl>     # prints the summary as JSON
"""
import json
import os
import struct
import sys


def _cstr(data, offset):
    end = data.index(b"\0", offset)
    return data[offset:end].decode("latin-1")


class Mdl:
    HDR = 156  # offset of numbones in studiohdr_t v49

    def __init__(self, path):
        self.d = open(path, "rb").read()
        if self.d[:4] != b"IDST":
            raise ValueError("%s is not an MDL" % path)
        self.version, self.checksum = struct.unpack_from("<2i", self.d, 4)

    def field(self, index, count=1):
        return struct.unpack_from("<%di" % count, self.d, self.HDR + 4 * index)

    def bones(self):
        n, idx = self.field(0, 2)
        out = []
        for i in range(n):
            b = idx + i * 216
            name_ofs, parent = struct.unpack_from("<2i", self.d, b)
            out.append((_cstr(self.d, b + name_ofs), parent))
        return out

    def textures(self):
        n, idx = self.field(12, 2)
        return [_cstr(self.d, idx + i * 64 + struct.unpack_from("<i", self.d, idx + i * 64)[0]) for i in range(n)]

    def skins(self):
        nref, nfam, idx = self.field(16, 3)
        return [list(struct.unpack_from("<%dh" % nref, self.d, idx + f * nref * 2)) for f in range(nfam)]

    def attachments(self):
        """(name, local bone index, flags, 3x4 local matrix rounded to 3 decimals) per mstudioattachment_t (92 bytes)"""
        n, idx = self.field(21, 2)
        out = []
        for i in range(n):
            o = idx + i * 92
            name_ofs, flags, bone = struct.unpack_from("<iIi", self.d, o)
            matrix = struct.unpack_from("<12f", self.d, o + 12)
            out.append((_cstr(self.d, o + name_ofs), bone, flags,
                        [[round(matrix[r * 4 + c], 3) + 0.0 for c in range(4)] for r in range(3)]))
        return out

    def flexdescs(self):
        n, idx = self.field(26, 2)
        return [_cstr(self.d, idx + i * 4 + struct.unpack_from("<i", self.d, idx + i * 4)[0]) for i in range(n)]

    def flexcontrollers(self):
        n, idx = self.field(28, 2)
        out = []
        for i in range(n):
            o = idx + i * 20
            type_ofs, name_ofs, _, lo, hi = struct.unpack_from("<iiiff", self.d, o)
            out.append((_cstr(self.d, o + name_ofs), _cstr(self.d, o + type_ofs), round(lo, 4), round(hi, 4)))
        return out

    def flexrules(self, descs, controllers):
        n, idx = self.field(30, 2)
        out = []
        for i in range(n):
            o = idx + i * 12
            flex, nops, opi = struct.unpack_from("<3i", self.d, o)
            ops = []
            for k in range(nops):
                op, value = struct.unpack_from("<ii", self.d, o + opi + k * 8)
                if op == 1:  # STUDIO_CONST
                    ops.append((op, round(struct.unpack_from("<f", self.d, o + opi + k * 8 + 4)[0], 4)))
                elif op in (2, 15, 16, 17, 20, 21):  # FETCH1, 2WAY_0/1, NWAY, DME eyelids: controller index
                    ops.append((op, controllers[value][0] if 0 <= value < len(controllers) else value))
                elif op == 3:  # FETCH2: flexdesc index
                    ops.append((op, descs[value] if 0 <= value < len(descs) else value))
                elif op in (18, 19):  # COMBO, DOMINATE: operand count
                    ops.append((op, value))
                else:  # arithmetic operators: no operand
                    ops.append((op, None))
            out.append((descs[flex], ops))
        return sorted(out)

    def sequences(self):
        n, idx = self.field(8, 2)
        out = []
        for i in range(n):
            o = idx + i * 212
            label_ofs = struct.unpack_from("<i", self.d, o + 4)[0]
            out.append(_cstr(self.d, o + label_ofs))
        return out

    def bodyparts(self):
        """[(bodypart name, [(model name, [mesh dict])])] with the model's vertex index into the VVD."""
        n, idx = self.field(19, 2)
        descs = self.flexdescs()
        out = []
        for i in range(n):
            b = idx + i * 16
            name_ofs, nmodels, _, model_idx = struct.unpack_from("<4i", self.d, b)
            models = []
            for j in range(nmodels):
                m = b + model_idx + j * 148
                mname = self.d[m:m + 64].split(b"\0")[0].decode("latin-1")
                nmesh, mesh_idx, nverts, vertindex = struct.unpack_from("<4i", self.d, m + 72)
                meshes = []
                for k in range(nmesh):
                    me = m + mesh_idx + k * 116
                    material, _, mverts, voff, nflex, flex_idx = struct.unpack_from("<6i", self.d, me)
                    flexes = []
                    for q in range(nflex):
                        f = me + flex_idx + q * 60
                        fdesc, t0, t1, t2, t3, fnv = struct.unpack_from("<i4fi", self.d, f)
                        flexes.append((descs[fdesc], fnv))
                    meshes.append({"material": material, "verts": mverts, "vertoffset": voff,
                                   "flexes": sorted(flexes)})
                models.append({"name": mname, "verts": nverts, "vertindex": vertindex, "meshes": meshes})
            out.append((_cstr(self.d, b + name_ofs), models))
        return out


def vvd_vertices(path):
    """List of (bone indices with weights, position, normal, uv) per vertex of LOD 0."""
    d = open(path, "rb").read()
    num = struct.unpack_from("<i", d, 16)[0]
    start = struct.unpack_from("<i", d, 56)[0]
    out = []
    for i in range(num):
        o = start + i * 48
        w = struct.unpack_from("<3f", d, o)
        bones = struct.unpack_from("<3B", d, o + 12)
        nb = d[o + 15]
        pos = struct.unpack_from("<3f", d, o + 16)
        nrm = struct.unpack_from("<3f", d, o + 28)
        uv = struct.unpack_from("<2f", d, o + 40)
        out.append(([(bones[k], w[k]) for k in range(nb)], pos, nrm, uv))
    return out


def vtx_triangles(path):
    """Triangle count per (bodypart, model, mesh) of LOD 0."""
    d = open(path, "rb").read()
    nbp, bpo = struct.unpack_from("<ii", d, 28)  # FileHeader_t: numBodyParts, bodyPartOffset
    out = {}
    for i in range(nbp):
        b = bpo + i * 8
        nm, mo = struct.unpack_from("<ii", d, b)
        for j in range(nm):
            m = b + mo + j * 8
            nlod, lo = struct.unpack_from("<ii", d, m)
            if not nlod:
                continue
            ll = m + lo
            nmesh, meo = struct.unpack_from("<ii", d, ll)
            for k in range(nmesh):
                me = ll + meo + k * 9
                nsg, sgo = struct.unpack_from("<ii", d, me)
                tris = 0
                for s in range(nsg):
                    sg = me + sgo + s * 33
                    nidx = struct.unpack_from("<i", d, sg + 8)[0]
                    tris += nidx // 3
                out["%d/%d/%d" % (i, j, k)] = tris
    return out


def cull_weights(weights, threshold):
    """Valve's SortAndBalanceBones: drop the smallest influences below threshold (keeping at least one), renormalize."""
    w = sorted(weights, key=lambda bw: -bw[1])
    while len(w) > 1 and w[-1][1] < threshold:
        w.pop()
    total = sum(x for _, x in w)
    return [(b, x / total) for b, x in w] if total > 0 else w


def summarize(mdl_path, ignore_bones=(), weight_cull=0.0):
    base = os.path.splitext(mdl_path)[0]
    mdl = Mdl(mdl_path)
    bones = mdl.bones()
    bone_names = [name for name, _ in bones]
    controllers = mdl.flexcontrollers()
    descs = mdl.flexdescs()
    textures = mdl.textures()
    summary = {
        "bones": sorted((name, bone_names[parent] if parent >= 0 else None) for name, parent in bones
                        if name not in ignore_bones),
        "textures": textures,
        "skins": [[textures[t] if 0 <= t < len(textures) else t for t in family] for family in mdl.skins()],
        "sequences": sorted(mdl.sequences()),
        "flexdescs": sorted(descs),
        "flexcontrollers": sorted(controllers),
        "flexrules": mdl.flexrules(descs, controllers),
        "attachments": sorted((name, bone_names[bone] if 0 <= bone < len(bone_names) else bone, flags, matrix)
                              for name, bone, flags, matrix in mdl.attachments()),
        "bodyparts": [],
    }

    verts = vvd_vertices(base + ".vvd") if os.path.exists(base + ".vvd") else []
    tris = vtx_triangles(base + ".dx90.vtx") if os.path.exists(base + ".dx90.vtx") else {}
    for i, (bp_name, models) in enumerate(mdl.bodyparts()):
        bp = {"name": bp_name, "models": []}
        for j, model in enumerate(models):
            first = model["vertindex"] // 48
            meshes = []
            for k, mesh in enumerate(model["meshes"]):
                mv = []
                for v in verts[first + mesh["vertoffset"]:first + mesh["vertoffset"] + mesh["verts"]]:
                    vw = [(bone_names[b], w) for b, w in v[0]]
                    if weight_cull:
                        vw = cull_weights(vw, weight_cull)
                    weights = sorted((name, round(w, 3)) for name, w in vw if name not in ignore_bones or w > 0)
                    mv.append((tuple(round(x, 3) for x in v[1]), tuple(round(x, 2) for x in v[2]),
                               tuple(round(x, 3) for x in v[3]), tuple(weights)))
                meshes.append({"material": textures[mesh["material"]] if mesh["material"] < len(textures)
                               else mesh["material"],
                               "verts": mesh["verts"], "flexes": mesh["flexes"],
                               "triangles": tris.get("%d/%d/%d" % (i, j, k)),
                               "vertex_data": sorted(mv)})
            bp["models"].append({"name": model["name"], "meshes": meshes})
        summary["bodyparts"].append(bp)
    return summary


if __name__ == "__main__":
    s = summarize(sys.argv[1])
    for bp in s["bodyparts"]:
        for m in bp["models"]:
            for mesh in m["meshes"]:
                mesh["vertex_data"] = len(mesh["vertex_data"])
    json.dump(s, sys.stdout, indent=1, default=list)
    print()
