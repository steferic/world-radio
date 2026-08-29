# face_keys_v4.py — keyboard face, v4.
#
# Caps are lofted natively here (rounded-rect base -> slightly smaller rounded-
# rect top -> filleted rim), because KeyV2 on OpenSCAD 2021.01 silently ignores
# its own parameters: its sculpting hooks are function literals stored in $-vars,
# which that build can't call, so every export fell back to a default 18mm cap.
# For a flat slab this loft gives exact, verifiable dimensions.
#
# Face anatomy:
#   cream deck  ->  flush near-black inlay (hairline part-line groove)
#                ->  9 flat keys floating 1.1mm above the inlay
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python face_keys_v4.py

import bpy
import bmesh
import math
from mathutils import Vector

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- parameters (mm) ----
PLATE = 110.0
PT = 6.0

CAP = 29.0          # key base
CAP_TOP = 27.6      # key top (1.4mm total taper -> near-vertical sides)
CAP_H = 5.4
CAP_R = 4.0         # plan corner radius at the base
CAP_TOP_R = 4.3
RIM = 0.5           # top/bottom edge roll

GAP = 1.6
PITCH = CAP + GAP   # 30.6
INLAY_MARGIN = 4.0
GROOVE_W = 0.5
GROOVE_D = 0.45
CAP_SIT = 1.1       # base height above the deck surface

ARC = 10            # samples per rounded corner


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


def smooth(ob, angle=0.45):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    try:
        bpy.ops.object.shade_auto_smooth(angle=angle)
    except Exception:
        bpy.ops.object.shade_smooth()


def rrect_pts(w, h, r):
    """CCW point loop of a rounded rectangle centred on origin."""
    w2, h2 = w / 2 - r, h / 2 - r
    pts = []
    for cx, cy, a0 in ((w2, h2, 0.0), (-w2, h2, math.pi / 2),
                       (-w2, -h2, math.pi), (w2, -h2, 3 * math.pi / 2)):
        for i in range(ARC + 1):
            a = a0 + (math.pi / 2) * i / ARC
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def rrect_prism(name, cx, cy, w, h, r, z0, z1):
    """Straight-walled rounded-rect prism (for deck / inlay / cutters)."""
    bm = bmesh.new()
    pts = rrect_pts(w, h, r)
    bot = [bm.verts.new((mm(cx + x), mm(cy + y), mm(z0))) for (x, y) in pts]
    top = [bm.verts.new((mm(cx + x), mm(cy + y), mm(z1))) for (x, y) in pts]
    n = len(pts)
    for i in range(n):
        bm.faces.new((bot[i], bot[(i + 1) % n], top[(i + 1) % n], top[i]))
    bm.faces.new(list(reversed(bot)))
    bm.faces.new(top)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    return ob


def keycap(name, cx, cy, z0):
    """Lofted flat keycap: rounded-rect base -> slightly smaller rounded-rect
    top, rim filleted by a bevel. Exact dimensions, no library involved."""
    bm = bmesh.new()
    pb = rrect_pts(CAP, CAP, CAP_R)
    pt = rrect_pts(CAP_TOP, CAP_TOP, CAP_TOP_R)
    bot = [bm.verts.new((mm(cx + x), mm(cy + y), mm(z0))) for (x, y) in pb]
    top = [bm.verts.new((mm(cx + x), mm(cy + y), mm(z0 + CAP_H))) for (x, y) in pt]
    n = len(pb)
    for i in range(n):
        bm.faces.new((bot[i], bot[(i + 1) % n], top[(i + 1) % n], top[i]))
    bm.faces.new(list(reversed(bot)))
    bm.faces.new(top)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    bevel(ob, RIM, segments=5, angle_limit=35)  # rim only; walls already smooth
    smooth(ob, angle=0.35)
    return ob


def matte(name, color, rough=0.44, coat=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    if coat > 0 and "Coat Weight" in b.inputs:
        b.inputs["Coat Weight"].default_value = coat
        b.inputs["Coat Roughness"].default_value = 0.28
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


def build():
    clean_scene()

    deck_mat = matte("Deck", (0.862, 0.826, 0.744), rough=0.46)
    cap_mat = matte("Keycap", (0.930, 0.914, 0.872), rough=0.33, coat=0.10)
    ink_mat = matte("Inlay", (0.013, 0.014, 0.016), rough=0.42, coat=0.06)

    centers = [(-PITCH + c * PITCH, -PITCH + r * PITCH) for r in range(3) for c in range(3)]
    field = 2 * PITCH + CAP           # 90.2
    inlay = field + 2 * INLAY_MARGIN  # 98.2

    ink_t = 1.4

    # ---- cream deck, pocketed for a flush inlay ----
    deck = rrect_prism("Deck", 0, 0, PLATE, PLATE, 3.0, 0, PT)
    boolean(deck, rrect_prism("pk", 0, 0, inlay, inlay, 6.0, PT - ink_t, PT + 2))
    # hairline part-line groove hugging the inlay
    g = rrect_prism("go", 0, 0, inlay + 2 * GROOVE_W, inlay + 2 * GROOVE_W, 6.5,
                    PT - GROOVE_D, PT + 2)
    boolean(g, rrect_prism("gi", 0, 0, inlay, inlay, 6.0, PT - GROOVE_D - 1, PT + 3))
    boolean(deck, g)
    bevel(deck, 0.45, segments=4, angle_limit=50)
    smooth(deck, angle=0.5)
    assign(deck, deck_mat)

    # ---- flush near-black inlay ----
    ink = rrect_prism("Inlay", 0, 0, inlay - 0.1, inlay - 0.1, 6.0, PT - ink_t, PT)
    bevel(ink, 0.25, segments=3, angle_limit=50)
    smooth(ink, angle=0.5)
    assign(ink, ink_mat)

    # ---- nine flat keys ----
    for i, (cx, cy) in enumerate(centers):
        cap = keycap(f"Cap{i}", cx, cy, PT + CAP_SIT)
        assign(cap, cap_mat)

    # ---- studio ----
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
    assign(bg, matte("Backdrop", (0.50, 0.487, 0.470), rough=0.9))

    def light(name, loc, energy, size, rot):
        bpy.ops.object.light_add(type="AREA", location=loc, rotation=rot)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        L.data.size = size

    light("Box", (mm(-200), mm(-240), mm(390)), 78, 1.0,
          (math.radians(32), 0, math.radians(-40)))
    light("Fill", (mm(330), mm(-170), mm(210)), 9, 1.3,
          (math.radians(58), 0, math.radians(58)))
    light("Edge", (0, mm(330), mm(200)), 22, 0.5, (math.radians(-56), 0, 0))


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
    # Punchy keeps the inlay reading black; AgX Base lifts shadows to grey.
    try:
        sc.view_settings.look = "AgX - Punchy"
    except Exception:
        sc.view_settings.look = "AgX - Base Contrast"
    sc.view_settings.exposure = -1.9
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    build()
    cam((mm(190), mm(-235), mm(240)), (math.radians(44), 0, math.radians(39)), 62, 0.38)
    render(f"{OUT_DIR}/face_keys_v4_a.png")
    bpy.data.objects.remove(bpy.context.scene.camera, do_unlink=True)
    cam((0, 0, mm(350)), (0, 0, 0), 60, 0.35)
    render(f"{OUT_DIR}/face_keys_v4_b.png")
