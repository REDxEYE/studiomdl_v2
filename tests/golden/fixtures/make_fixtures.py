"""Generates the golden-test source models with Blender Source Tools (the exporter the users of studiomdl_v2 use).

    blender -b --factory-startup --python tests/golden/fixtures/make_fixtures.py -- <output dir>

The generated files are committed (tests/golden/fixtures/src); re-run this only when a fixture has to change, then
refresh the expected hashes with `run_golden.py --update`.

Everything is built procedurally and deterministically: no randomness, fixed sizes.
"""
import math
import os
import sys

import addon_utils
import bmesh
import bpy

out_dir = os.path.abspath(sys.argv[sys.argv.index("--") + 1])
os.makedirs(out_dir, exist_ok=True)

addon_utils.enable("io_scene_valvesource", default_set=True)
from io_scene_valvesource.utils import State  # noqa: E402  (needs the add-on enabled)

# start from an empty scene (--factory-startup has the default cube, camera and light)
for ob in list(bpy.data.objects):
    bpy.data.objects.remove(ob)
scene = bpy.context.scene
scene.frame_start, scene.frame_end = 0, 9


def material(name):
    return bpy.data.materials.get(name) or bpy.data.materials.new(name)


def make_armature():
    data = bpy.data.armatures.new("Rig")
    rig = bpy.data.objects.new("Rig", data)
    scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    parent = None
    for name, z0, z1 in (("root", 0.0, 2.0), ("spine", 2.0, 6.0), ("head", 6.0, 10.0)):
        bone = data.edit_bones.new(name)
        bone.head = (0, 0, z0)
        bone.tail = (0, 0, z1)
        bone.parent = parent
        if parent:
            bone.use_connect = True
        parent = bone
    bpy.ops.object.mode_set(mode='OBJECT')
    return rig


def skin(ob, rig):
    """Weights by height between the three bones; parents the mesh to the rig."""
    groups = {name: ob.vertex_groups.new(name=name) for name in ("root", "spine", "head")}
    for v in ob.data.vertices:
        z = v.co.z
        if z < 2.0:
            groups["root"].add([v.index], 1.0, 'REPLACE')
        elif z < 6.0:
            t = (z - 2.0) / 4.0
            groups["root"].add([v.index], 1.0 - t, 'REPLACE')
            groups["spine"].add([v.index], t, 'REPLACE')
        else:
            t = min((z - 6.0) / 4.0, 1.0)
            groups["spine"].add([v.index], 1.0 - t, 'REPLACE')
            groups["head"].add([v.index], t, 'REPLACE')
    ob.parent = rig
    mod = ob.modifiers.new("Armature", 'ARMATURE')
    mod.object = rig


def cylinder_mesh(name, segments, rings, radius, height, materials, z0=0.0):
    """Open tube of quads; the faces of the upper half use the second material when two are given."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    verts = []
    for r in range(rings + 1):
        z = z0 + height * r / rings
        row = []
        for s in range(segments):
            a = 2.0 * math.pi * s / segments
            row.append(bm.verts.new((radius * math.cos(a), radius * math.sin(a), z)))
        verts.append(row)
    for r in range(rings):
        for s in range(segments):
            f = bm.faces.new((verts[r][s], verts[r][(s + 1) % segments], verts[r + 1][(s + 1) % segments],
                              verts[r + 1][s]))
            f.material_index = 1 if (len(materials) > 1 and r >= rings // 2) else 0
    bm.to_mesh(me)
    bm.free()
    for m in materials:
        me.materials.append(material(m))
    uv = me.uv_layers.new(name="UVMap")
    for loop in me.loops:
        co = me.vertices[loop.vertex_index].co
        uv.data[loop.index].uv = (math.atan2(co.y, co.x) / (2 * math.pi) + 0.5, (co.z - z0) / height)
    ob = bpy.data.objects.new(name, me)
    scene.collection.objects.link(ob)
    return ob


def grid_mesh(name, n, size, mat, z=4.0):
    """Flat n x n quad grid standing in the XZ plane (enough vertices to exercise the vertex-count split)."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    rows = [[bm.verts.new((size * (i / n - 0.5), 0.0, z + size * (j / n - 0.5))) for i in range(n + 1)]
            for j in range(n + 1)]
    for j in range(n):
        for i in range(n):
            bm.faces.new((rows[j][i], rows[j][i + 1], rows[j + 1][i + 1], rows[j + 1][i]))
    bm.to_mesh(me)
    bm.free()
    me.materials.append(material(mat))
    uv = me.uv_layers.new(name="UVMap")
    for loop in me.loops:
        co = me.vertices[loop.vertex_index].co
        uv.data[loop.index].uv = (co.x / size + 0.5, (co.z - z) / size + 0.5)
    ob = bpy.data.objects.new(name, me)
    scene.collection.objects.link(ob)
    return ob


def shape_key(ob, name, region, offset):
    """Adds a shape key that moves the vertices for which region(co) is true by offset."""
    if not ob.data.shape_keys:
        ob.shape_key_add(name="Basis")
    key = ob.shape_key_add(name=name)
    for v, kv in zip(ob.data.vertices, key.data):
        if region(v.co):
            kv.co = (v.co.x + offset[0], v.co.y + offset[1], v.co.z + offset[2])
    return key


rig = make_armature()

# Body: two materials, three flexes (one corrective A_B over two real controls), flexes touch only part of the mesh
body = cylinder_mesh("Body", 32, 24, 2.0, 10.0, ["body", "body2"])
skin(body, rig)
shape_key(body, "Bulge", lambda co: 3.0 < co.z < 5.0 and co.x > 0, (0.4, 0.0, 0.0))
shape_key(body, "Pinch", lambda co: 7.0 < co.z < 8.0, (0.0, 0.0, 0.3))
shape_key(body, "Bulge_Pinch", lambda co: 4.5 < co.z < 7.5 and co.x > 0, (0.1, 0.1, 0.0))

# Body_lod1: coarser stand-in for $lod
body_lod1 = cylinder_mesh("Body_lod1", 16, 10, 2.0, 10.0, ["body", "body2"])
skin(body_lod1, rig)

# Hat: bodygroup piece with its own flex
hat = cylinder_mesh("Hat", 24, 4, 2.4, 1.5, ["hat"], z0=10.0)
skin(hat, rig)
shape_key(hat, "Squash", lambda co: co.z > 11.0, (0.0, 0.0, -0.5))
shape_key(hat, "Tilt", lambda co: co.z > 11.0 and co.x > 0, (0.0, 0.0, 0.3))

# Big: 3721 verts, split with $maxverts; its flex moves only a corner (flex optimization)
big = grid_mesh("Big", 60, 8.0, "big")
skin(big, rig)
shape_key(big, "Corner", lambda co: co.x > 2.0 and co.z > 6.0, (0.0, 0.5, 0.0))
shape_key(big, "Edge", lambda co: co.x < -3.0, (0.0, -0.3, 0.0))

# Prop: unskinned static prop
prop = cylinder_mesh("Prop", 12, 3, 1.0, 2.0, ["prop"])

# Animation: spine bends over 10 frames
rig.animation_data_create()
action = bpy.data.actions.new("Wave")
rig.animation_data.action = action
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode='POSE')
for frame in range(0, 10):
    pb = rig.pose.bones["spine"]
    pb.rotation_mode = 'XYZ'
    pb.rotation_euler = (0.0, math.sin(frame / 9.0 * math.pi) * 0.6, 0.0)
    pb.keyframe_insert("rotation_euler", frame=frame)
bpy.ops.object.mode_set(mode='OBJECT')
rig.data.vs.action_selection = 'CURRENT'

# The exporter pushes an undo step, which invalidates Python references to scene data: always look things up again
def export(fmt, export_path, names, anim_names):
    scn = bpy.context.scene
    scn.vs.export_path = export_path + os.sep
    # binary like the users export (Blender Source Tools writes an invalid `"name" string` line for unnamed elements
    # in keyvalues2 mode, which the Valve keyvalues2 reader rejects)
    scn.vs.use_kv2 = False
    scn.vs.export_format = fmt
    os.makedirs(export_path, exist_ok=True)
    for ob in scn.objects:
        ob.vs.export = ob.name in names or ob.name in anim_names
        ob.vs.subdir = "anims" if ob.name in anim_names else ""
    State.update_scene(scn)
    result = bpy.ops.export_scene.smd(export_scene=True)
    if 'FINISHED' not in result:
        raise SystemExit("export failed: %s" % result)


export('DMX', out_dir, {"Body", "Body_lod1", "Hat", "Big", "Prop"}, {"Rig"})
# The same body as SMD + VTA (old-style flexes) into smd/
export('SMD', os.path.join(out_dir, "smd"), {"Body"}, {"Rig"})
print("FIXTURES WRITTEN TO", out_dir)
