# face_keys_v3.py — keyboard face, v3. Precision over puffiness.
#
# Fixes from v2:
#   * caps are FLAT slabs (KeyV2, authored at final 29mm size: no dish, 1.6mm
#     taper, 0.45mm edge roll) instead of tapered domes
#   * the clunky recessed tray is gone. The dark plate is a flush INLAY in the
#     cream deck, separated by a hairline part-line groove — like a trackpad
#     inlay, no stepped walls
#   * real tonal contrast: near-black plate, warm-white caps, cream deck
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python face_keys_v3.py

import bpy
import bmesh
import math
from mathutils import Vector

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- parameters (mm) ----
PLATE = 110.0
PT = 6.0
CAP = 29.0          # authored cap size (see keycap_flat.scad)
CAP_H = 5.6
GAP = 1.6           # gap between cap bases
PITCH = CAP + GAP   # 30.6
INLAY_MARGIN = 4.0  # black inlay border around the cap field
GROOVE_W = 0.5      # hairline part-line around the inlay
GROOVE_D = 0.45
CAP_SIT = 1.1       # cap base above the deck surface (keys float, no bezels)


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


def bevel(ob, width_mm, segments=6, angle_limit=60):
    mod = ob.modifiers.new(name="bevel", type="BEVEL")
    mod.width = mm(width_mm)
    mod.segments = segments
    mod.limit_method = "ANGLE"
    mod.angle_limit = math.radians(angle_limit)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)


def smooth(ob, angle=0.5):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    try:
        bpy.ops.object.shade_auto_smooth(angle=angle)
    except Exception:
        bpy.ops.object.shade_smooth()


def rrect(name, cx, cy, w, h, r, z0, z1):
    """Rounded-rect prism from boxes + corner cylinders."""
    w2, h2 = w / 2, h / 2

    def box(x0, x1, y0, y1):
        bpy.ops.mesh.primitive_cube_add(
            size=1, location=(mm(cx + (x0 + x1) / 2), mm(cy + (y0 + y1) / 2), mm((z0 + z1) / 2)))
        o = bpy.context.object
        o.scale = (mm(x1 - x0), mm(y1 - y0), mm(z1 - z0))
        bpy.ops.object.transform_apply(scale=True)
        return o

    main = box(-w2 + r, w2 - r, -h2, h2)
    main.name = name
    _bool(main, box(-w2, w2, -h2 + r, h2 - r), "UNION")
    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        bpy.ops.mesh.primitive_cylinder_add(
            radius=mm(r), depth=mm(z1 - z0), vertices=64,
            location=(mm(cx + sx * (w2 - r)), mm(cy + sy * (h2 - r)), mm((z0 + z1) / 2)))
        _bool(main, bpy.context.object, "UNION")
    return main


def matte(name, color, rough=0.44, coat=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    if coat > 0 and "Coat Weight" in b.inputs:
        b.inputs["Coat Weight"].default_value = coat
        b.inputs["Coat Roughness"].default_value = 0.3
    n = nt.nodes.new("ShaderNodeTexNoise")
    n.inputs["Scale"].default_value = 1600.0
    n.inputs["Detail"].default_value = 3.0
    bp = nt.nodes.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value = 0.008
    nt.links.new(n.outputs["Fac"], bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


def assign(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


def import_cap():
    path = f"{OUT_DIR}/keycap_flat.stl"
    before = set(bpy.data.objects)
    try:
        bpy.ops.wm.stl_import(filepath=path)
    except Exception:
        bpy.ops.import_mesh.stl(filepath=path)
    ob = list(set(bpy.data.objects) - before)[0]
    ob.name = "CapSrc"
    ob.scale = (S, S, S)
    bpy.ops.object.transform_apply(scale=True)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")
    bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    ob.location = (0, 0, -min(v.z for v in bb))
    bpy.ops.object.transform_apply(location=True)
    return ob


def build():
    clean_scene()

    deck_mat = matte("Deck", (0.862, 0.826, 0.744), rough=0.46)
    cap_mat = matte("Keycap", (0.935, 0.918, 0.876), rough=0.34, coat=0.12)
    ink_mat = matte("Inlay", (0.016, 0.017, 0.019), rough=0.52)

    centers = [(-PITCH + c * PITCH, -PITCH + r * PITCH) for r in range(3) for c in range(3)]
    field = 2 * PITCH + CAP           # 90.2
    inlay = field + 2 * INLAY_MARGIN  # 98.2  (on a 110 face -> 5.9mm cream border)

    # ---- cream deck ----
    deck = rrect("Deck", 0, 0, PLATE, PLATE, 3.0, 0, PT)
    # pocket for the inlay (flush): shallow, exactly inlay thickness
    ink_t = 1.4
    pocket = rrect("pk", 0, 0, inlay, inlay, 6.0, PT - ink_t, PT + 2)
    boolean(deck, pocket)
    # hairline part-line groove just outside the inlay
    g_out = rrect("go", 0, 0, inlay + 2 * GROOVE_W, inlay + 2 * GROOVE_W, 6.5,
                  PT - GROOVE_D, PT + 2)
    g_in = rrect("gi", 0, 0, inlay, inlay, 6.0, PT - GROOVE_D - 1, PT + 3)
    boolean(g_out, g_in)
    boolean(deck, g_out)
    bevel(deck, 0.5, segments=4, angle_limit=50)
    smooth(deck)
    assign(deck, deck_mat)

    # ---- flush black inlay ----
    ink = rrect("Inlay", 0, 0, inlay - 0.1, inlay - 0.1, 6.0, PT - ink_t, PT)
    bevel(ink, 0.3, segments=3, angle_limit=50)
    smooth(ink)
    assign(ink, ink_mat)

    # ---- flat KeyV2 caps floating above the inlay ----
    src = import_cap()
    for i, (cx, cy) in enumerate(centers):
        cap = src.copy()
        cap.data = src.data.copy()
        cap.name = f"Cap{i}"
        bpy.context.collection.objects.link(cap)
        cap.location = (mm(cx), mm(cy), mm(PT + CAP_SIT))
        smooth(cap, angle=0.45)
        assign(cap, cap_mat)
    bpy.data.objects.remove(src, do_unlink=True)

    # ---- studio: one big soft box + gentle fill, graduated backdrop ----
    bpy.ops.mesh.primitive_plane_add(size=3.2, location=(0, 0.55, -0.0004))
    bg = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(bg.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=28, use_grid_fill=True)
    for v in bm.verts:
        if v.co.y > 0.35:
            v.co.z += (v.co.y - 0.35) ** 2 * 1.5
    bm.to_mesh(bg.data)
    bg.data.update()
    assign(bg, matte("Backdrop", (0.55, 0.535, 0.515), rough=0.9))

    def light(name, loc, energy, size, rot):
        bpy.ops.object.light_add(type="AREA", location=loc, rotation=rot)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        L.data.size = size

    light("Box", (mm(-210), mm(-250), mm(400)), 95, 1.1,
          (math.radians(32), 0, math.radians(-40)))
    light("Fill", (mm(330), mm(-180), mm(220)), 12, 1.3,
          (math.radians(58), 0, math.radians(58)))
    light("Edge", (mm(0), mm(330), mm(210)), 30, 0.5, (math.radians(-56), 0, 0))


def cam(loc, rot, lens, focus_d, fstop=14.0):
    bpy.ops.object.camera_add(location=loc, rotation=rot)
    c = bpy.context.object
    c.data.lens = lens
    c.data.dof.use_dof = True
    c.data.dof.focus_distance = focus_d
    c.data.dof.aperture_fstop = fstop
    bpy.context.scene.camera = c
    return c


def render(path, samples=320):
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
    sc.cycles.max_bounces = 6
    sc.cycles.diffuse_bounces = 2
    sc.render.resolution_x = 1700
    sc.render.resolution_y = 1275
    sc.render.filepath = path
    sc.view_settings.look = "AgX - Base Contrast"
    sc.view_settings.exposure = -2.2
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    build()
    # hero: whole face in frame, steep enough to show cap thickness
    cam((mm(205), mm(-250), mm(255)), (math.radians(44), 0, math.radians(39)),
        62, 0.40)
    render(f"{OUT_DIR}/face_keys_v3_a.png")
    # graphic: straight down
    bpy.data.objects.remove(bpy.context.scene.camera, do_unlink=True)
    cam((0, 0, mm(360)), (0, 0, math.radians(0)), 60, 0.36)
    render(f"{OUT_DIR}/face_keys_v3_b.png")
