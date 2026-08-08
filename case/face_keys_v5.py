# face_keys_v5.py — control face, v5: 4x4 grid, Codex-Micro style.
#
# 16 grid positions on the same 98.2mm flush black plate the screen face uses:
#   * right column, top three cells  -> the three knobs
#       Volume  (large)  : absolute position matters, reads at a glance
#       Tune    (medium) : detented encoder feel, station scroll
#       Zoom    (small)  : smooth encoder, globe zoom
#   * the other 13 cells -> flat keys
# Knob sizes deliberately differ so they're identifiable by touch alone.
#
# Keys are lofted rounded-rect slabs; knobs are lofted fluted cylinders (the
# flutes come from radius modulation in the profile, not booleans, so the mesh
# stays clean and fast).
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python face_keys_v5.py

import bpy
import bmesh
import math

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- face (shared with the screen face) ----
PLATE = 110.0
PT = 6.0
DECK_R = 3.0
INLAY = 98.2
INLAY_R = 6.0
INK_T = 1.4
GROOVE_W = 0.5
GROOVE_D = 0.45

# ---- 4x4 grid ----
N = 4
PITCH = 22.6
CAP = 21.0          # key base
CAP_TOP = 20.0      # 1.0mm total taper: sides read vertical
CAP_H = 4.8
CAP_R = 2.6         # tighter than v4's 4.0 -> hardware, not app icon
CAP_TOP_R = 2.8
RIM = 0.72
CAP_SIT = 1.0       # base above the deck surface

# ---- knobs: (grid row from top, base diameter, height, flutes) ----
# ordered top -> bottom: (name, diameter, height, flutes)
KNOBS = [
    ("Volume", 20.4, 16.5, 26),
    ("Tune",   18.8, 15.0, 24),
    ("Zoom",   17.2, 13.6, 22),
]
KNOB_COL = 3        # rightmost column
FLUTE_DEPTH = 0.38
KNOB_TAPER = 0.955  # top radius / base radius

ARC = 12


def mm(v):
    return v * S


def clean_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.lights,
                  bpy.data.cameras, bpy.data.images):
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


def boolean(t, c):
    _bool(t, c, "DIFFERENCE")


def bevel(ob, w, segments=6, angle_limit=60):
    m = ob.modifiers.new(name="bevel", type="BEVEL")
    m.width = mm(w)
    m.segments = segments
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(angle_limit)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=m.name)


def smooth(ob, angle=0.5):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    try:
        bpy.ops.object.shade_auto_smooth(angle=angle)
    except Exception:
        bpy.ops.object.shade_smooth()


def rrect_pts(w, h, r):
    w2, h2 = w / 2 - r, h / 2 - r
    pts = []
    for cx, cy, a0 in ((w2, h2, 0.0), (-w2, h2, math.pi / 2),
                       (-w2, -h2, math.pi), (w2, -h2, 3 * math.pi / 2)):
        for i in range(ARC + 1):
            a = a0 + (math.pi / 2) * i / ARC
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def loft(name, bot_pts, top_pts, z0, z1, cx=0.0, cy=0.0):
    """Bridge two equal-length point loops into a closed solid."""
    bm = bmesh.new()
    bot = [bm.verts.new((mm(cx + x), mm(cy + y), mm(z0))) for (x, y) in bot_pts]
    top = [bm.verts.new((mm(cx + x), mm(cy + y), mm(z1))) for (x, y) in top_pts]
    n = len(bot_pts)
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


def prism(name, cx, cy, w, h, r, z0, z1):
    p = rrect_pts(w, h, r)
    return loft(name, p, p, z0, z1, cx, cy)


def keycap(name, cx, cy, z0):
    ob = loft(name, rrect_pts(CAP, CAP, CAP_R), rrect_pts(CAP_TOP, CAP_TOP, CAP_TOP_R),
              z0, z0 + CAP_H, cx, cy)
    bevel(ob, RIM, segments=5, angle_limit=35)
    smooth(ob, angle=0.35)
    return ob


def fluted_pts(radius, flutes, depth, samples_per_flute=7):
    """Circle with sinusoidal radius modulation -> knurled/fluted skirt."""
    pts = []
    n = flutes * samples_per_flute
    for i in range(n):
        a = 2 * math.pi * i / n
        r = radius - depth * 0.5 * (1 - math.cos(flutes * a))
        pts.append((r * math.cos(a), r * math.sin(a)))
    return pts


def knob(name, cx, cy, z0, dia, height, flutes, dark_mat):
    rb = dia / 2
    rt = rb * KNOB_TAPER
    ob = loft(name,
              fluted_pts(rb, flutes, FLUTE_DEPTH),
              fluted_pts(rt, flutes, FLUTE_DEPTH * 0.8),
              z0, z0 + height, cx, cy)
    bevel(ob, 0.7, segments=4, angle_limit=32)   # rounds top/bottom rims only
    smooth(ob, angle=0.6)
    # indicator line inset into the top face
    ind = prism(f"{name}Ind", cx, cy + (rt * 0.55), 1.3, rt * 0.78, 0.55,
                z0 + height - 0.05, z0 + height + 0.35)
    ind.data.materials.append(dark_mat)
    return ob, ind


def tri_prism(name, cx, cy, size, z0, z1):
    """Equilateral-ish play triangle, pointing +x, as a shallow cutter."""
    h = size * 0.5
    pts = [(-h * 0.62, -h), (-h * 0.62, h), (h * 0.86, 0.0)]
    return loft(name, pts, pts, z0, z1, cx, cy)


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
    knob_mat = matte("Knob", (0.930, 0.914, 0.872), rough=0.29, coat=0.16)
    ink_mat = matte("Inlay", (0.013, 0.014, 0.016), rough=0.42, coat=0.06)
    dark_mat = matte("Marker", (0.02, 0.02, 0.023), rough=0.5)
    accent_mat = matte("Accent", (0.545, 0.108, 0.070), rough=0.34, coat=0.12)

    # ---- deck, pocketed flush for the plate ----
    deck = prism("Deck", 0, 0, PLATE, PLATE, DECK_R, 0, PT)
    boolean(deck, prism("pk", 0, 0, INLAY, INLAY, INLAY_R, PT - INK_T, PT + 2))
    g = prism("go", 0, 0, INLAY + 2 * GROOVE_W, INLAY + 2 * GROOVE_W,
              INLAY_R + GROOVE_W, PT - GROOVE_D, PT + 2)
    boolean(g, prism("gi", 0, 0, INLAY, INLAY, INLAY_R, PT - GROOVE_D - 1, PT + 3))
    boolean(deck, g)
    bevel(deck, 0.45, segments=4, angle_limit=50)
    smooth(deck)
    assign(deck, deck_mat)

    # ---- flush black plate ----
    ink = prism("Plate", 0, 0, INLAY - 0.1, INLAY - 0.1, INLAY_R, PT - INK_T, PT)
    bevel(ink, 0.25, segments=3, angle_limit=50)
    smooth(ink)
    assign(ink, ink_mat)

    # ---- keys ----
    for row in range(N):
        for col in range(N):
            if col == KNOB_COL and row < len(KNOBS):
                continue                            # knob cells handled below
            cx = (col - (N - 1) / 2) * PITCH
            cy = ((N - 1) / 2 - row) * PITCH        # row 0 = top
            cap = keycap(f"Key{row}{col}", cx, cy, PT + CAP_SIT)
            if col == KNOB_COL and row == N - 1:
                # the 16th cell: accent play/stop key with a debossed glyph
                assign(cap, accent_mat)
                boolean(cap, tri_prism("glyph", cx, cy, 8.2,
                                       PT + CAP_SIT + CAP_H - 0.45,
                                       PT + CAP_SIT + CAP_H + 1))
            else:
                assign(cap, cap_mat)

    # ---- knobs: centres nudged off-grid so the EDGE gaps are equal ----
    kx = (KNOB_COL - (N - 1) / 2) * PITCH
    y_top = ((N - 1) / 2 - 0) * PITCH
    y_bot = ((N - 1) / 2 - (len(KNOBS) - 1)) * PITCH
    r_top, r_bot = KNOBS[0][1] / 2, KNOBS[-1][1] / 2
    # equal-gap solution for the middle knob (see notes): y2 = (y1+y3+r3-r1)/2
    y_mid = (y_top + y_bot + r_bot - r_top) / 2
    ys = [y_top, y_mid, y_bot]
    for (nm, dia, h, fl), cy in zip(KNOBS, ys):
        kb, ind = knob(nm, kx, cy, PT + 0.6, dia, h, fl, dark_mat)
        assign(kb, knob_mat)

    # ---- studio ----
    bpy.ops.mesh.primitive_plane_add(size=3.4, location=(0, 0.55, -0.0004))
    bg = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(bg.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=28, use_grid_fill=True)
    for v in bm.verts:
        if v.co.y > 0.38:
            v.co.z += (v.co.y - 0.38) ** 2 * 1.4
    bm.to_mesh(bg.data)
    bg.data.update()
    assign(bg, matte("Backdrop", (0.50, 0.487, 0.470), rough=0.9))

    bpy.ops.mesh.primitive_plane_add(size=9.0, location=(0, 0, mm(560)))
    gobo = bpy.context.object
    gm = bpy.data.materials.new("Gobo")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.008, 0.008, 0.009, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.9
    gobo.data.materials.append(gm)

    def light(name, loc, energy, size, rot):
        bpy.ops.object.light_add(type="AREA", location=loc, rotation=rot)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        L.data.size = size
        L.visible_glossy = False
        L.visible_transmission = False
        return L

    light("L1", (mm(-310), mm(-235), mm(165)), 80, 0.6,
          (math.radians(65), 0, math.radians(-47)))
    light("L2", (mm(320), mm(-185), mm(150)), 34, 0.6,
          (math.radians(68), 0, math.radians(51)))
    light("L3", (0, mm(330), mm(160)), 42, 0.55, (math.radians(-64), 0, 0))
    light("L4", (0, mm(-340), mm(130)), 20, 0.5, (math.radians(74), 0, 0))


def cam(loc, lens, fstop=13.0):
    tgt = bpy.data.objects.get("AimTarget")
    if tgt is None:
        tgt = bpy.data.objects.new("AimTarget", None)
        bpy.context.collection.objects.link(tgt)
        tgt.location = (0, 0, mm(PT / 2))
    bpy.ops.object.camera_add(location=loc)
    c = bpy.context.object
    c.data.lens = lens
    con = c.constraints.new(type="TRACK_TO")
    con.target = tgt
    con.track_axis = "TRACK_NEGATIVE_Z"
    con.up_axis = "UP_Y"
    c.data.dof.use_dof = True
    c.data.dof.focus_object = tgt
    c.data.dof.aperture_fstop = fstop
    bpy.context.scene.camera = c
    return c


def render(path, w=1800, h=1350, samples=330):
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
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    sc.render.filepath = path
    try:
        sc.view_settings.look = "AgX - Punchy"
    except Exception:
        pass
    sc.view_settings.exposure = -1.9
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    build()
    cam((0, mm(-0.01), mm(345)), 58)
    render(f"{OUT_DIR}/face_keys_v5_top.png")
    bpy.data.objects.remove(bpy.context.scene.camera, do_unlink=True)
    cam((mm(200), mm(-250), mm(210)), 56)
    render(f"{OUT_DIR}/face_keys_v5_hero.png")
