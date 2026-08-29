# face_keys.py — precision study of the keyboard face, alone.
#
# A 110mm face plate carrying a 3x3 grid of MacBook-style chiclet keys:
# flat tops, rounded plan corners, fine edge roll, each key seated snugly in
# its own aperture with a 0.4mm reveal. Dark backer beneath so gaps read black.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python face_keys.py

import bpy
import bmesh
import math
from mathutils import Vector

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- parameters (mm) ----
PLATE = 110.0
PT = 6.0            # plate thickness
KEY = 26.4          # key square size
KEY_H = 4.5         # key body height
KEY_PROUD = 2.2     # how far the key top sits above the deck
KEY_R = 3.2         # plan corner radius
EDGE_ROLL = 1.0     # top edge rounding
GAP = 3.2           # visible gap between neighboring keys
REVEAL = 0.45       # key-to-aperture clearance (the snug dark outline)
PITCH = KEY + GAP


def mm(v):
    return v * S


def clean_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def _bool(target, other, op):
    mod = target.modifiers.new(name="bool", type="BOOLEAN")
    mod.operation = op
    mod.object = other
    mod.solver = "EXACT"
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(other, do_unlink=True)


def boolean(target, cutter):
    _bool(target, cutter, "DIFFERENCE")


def union(target, other):
    _bool(target, other, "UNION")


def bevel(ob, width_mm, segments=6, angle_limit=60):
    mod = ob.modifiers.new(name="bevel", type="BEVEL")
    mod.width = mm(width_mm)
    mod.segments = segments
    mod.limit_method = "ANGLE"
    mod.angle_limit = math.radians(angle_limit)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)


def smooth(ob):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    try:
        bpy.ops.object.shade_auto_smooth(angle=0.7)
    except Exception:
        bpy.ops.object.shade_smooth()


def plastic(name, color, rough=0.45):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 900.0
    noise.inputs["Detail"].default_value = 3.0
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.012
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    return m


def assign(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


def rounded_rect_prism(name, cx, cy, w, h, r, z0, z1):
    """Rounded-rectangle prism: union of 2 boxes + 4 corner cylinders.
    Sharp top/bottom edges (rounded afterward by a bevel if wanted)."""
    w2, h2 = w / 2, h / 2

    def box(x0, x1, y0, y1):
        bpy.ops.mesh.primitive_cube_add(
            size=1, location=(mm(cx + (x0 + x1) / 2), mm(cy + (y0 + y1) / 2), mm((z0 + z1) / 2)))
        ob = bpy.context.object
        ob.scale = (mm(x1 - x0), mm(y1 - y0), mm(z1 - z0))
        bpy.ops.object.transform_apply(scale=True)
        return ob

    main = box(-w2 + r, w2 - r, -h2, h2)
    main.name = name
    b2 = box(-w2, w2, -h2 + r, h2 - r)
    union(main, b2)
    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        bpy.ops.mesh.primitive_cylinder_add(
            radius=mm(r), depth=mm(z1 - z0), vertices=48,
            location=(mm(cx + sx * (w2 - r)), mm(cy + sy * (h2 - r)), mm((z0 + z1) / 2)))
        union(main, bpy.context.object)
    return main


def build():
    clean_scene()

    cream = plastic("Cream", (0.885, 0.848, 0.768), rough=0.42)
    key_mat = plastic("Keys", (0.885, 0.848, 0.768), rough=0.36)
    black = plastic("Backer", (0.006, 0.006, 0.007), rough=0.6)

    # ---- deck plate with 9 snug apertures ----
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, mm(PT / 2)))
    plate = bpy.context.object
    plate.name = "KeyFace"
    plate.scale = (mm(PLATE), mm(PLATE), mm(PT))
    bpy.ops.object.transform_apply(scale=True)

    centers = [(-PITCH + c * PITCH, -PITCH + r * PITCH) for r in range(3) for c in range(3)]
    for (cx, cy) in centers:
        cut = rounded_rect_prism("cut", cx, cy, KEY + 2 * REVEAL, KEY + 2 * REVEAL,
                                 KEY_R + REVEAL, -2, PT + 2)
        boolean(plate, cut)
    bevel(plate, 1.6, segments=6)
    smooth(plate)
    assign(plate, cream)

    # ---- black gasket just below the deck surface (the MacBook membrane) ----
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, mm(PT - 0.65)))
    gasket = bpy.context.object
    gasket.name = "Gasket"
    gasket.scale = (mm(PLATE - 4), mm(PLATE - 4), mm(1.0))
    bpy.ops.object.transform_apply(scale=True)
    for (cx, cy) in centers:
        hole = rounded_rect_prism("gh", cx, cy, KEY + 0.08, KEY + 0.08, KEY_R,
                                  PT - 2, PT + 1)
        boolean(gasket, hole)
    assign(gasket, black)

    # ---- 9 chiclet keys, snug in their apertures ----
    for (cx, cy) in centers:
        key = rounded_rect_prism("Key", cx, cy, KEY, KEY, KEY_R,
                                 PT + KEY_PROUD - KEY_H, PT + KEY_PROUD)
        bevel(key, EDGE_ROLL, segments=5, angle_limit=48)
        smooth(key)
        assign(key, key_mat)

    # ---- studio ----
    bpy.ops.mesh.primitive_plane_add(size=3.0, location=(0, 0.5, -0.004))
    bg = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(bg.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=24, use_grid_fill=True)
    for v in bm.verts:
        if v.co.y > 0.35:
            v.co.z += (v.co.y - 0.35) ** 2 * 1.6
    bm.to_mesh(bg.data)
    bg.data.update()
    assign(bg, plastic("Backdrop", (0.80, 0.77, 0.73), rough=0.85))

    def light(name, loc, energy, size, rot):
        bpy.ops.object.light_add(type="AREA", location=loc, rotation=rot)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        L.data.size = size

    # raking key light to carve the key edges, soft fill, cool rim
    light("Key", (mm(-260), mm(-180), mm(160)), 55, 0.8,
          (math.radians(58), 0, math.radians(-55)))
    light("Fill", (mm(300), mm(-220), mm(200)), 11, 1.1,
          (math.radians(55), 0, math.radians(50)))
    light("Rim", (mm(60), mm(300), mm(260)), 55, 0.7, (math.radians(-45), 0, 0))


def cam(loc, rot, lens, focus_d, fstop=8.0):
    bpy.ops.object.camera_add(location=loc, rotation=rot)
    c = bpy.context.object
    c.data.lens = lens
    c.data.dof.use_dof = True
    c.data.dof.focus_distance = focus_d
    c.data.dof.aperture_fstop = fstop
    bpy.context.scene.camera = c
    return c


def render(path, samples=288):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
        sc.cycles.device = "GPU"
    except Exception:
        pass
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.render.resolution_x = 1700
    sc.render.resolution_y = 1275
    sc.render.filepath = path
    sc.view_settings.look = "AgX - Base Contrast"
    sc.view_settings.exposure = -2.35
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    build()
    # 3/4 beauty: low, raking, close
    cam((mm(150), mm(-190), mm(120)), (math.radians(62), 0, math.radians(38)),
        70, 0.28, fstop=12.0)
    render(f"{OUT_DIR}/face_keys_a.png")
    # near-top-down: the grid as graphic
    bpy.data.objects.remove(bpy.context.scene.camera, do_unlink=True)
    cam((mm(40), mm(-70), mm(260)), (math.radians(16), 0, math.radians(30)),
        58, 0.27, fstop=12.0)
    render(f"{OUT_DIR}/face_keys_b.png")
