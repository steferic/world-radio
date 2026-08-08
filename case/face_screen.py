# face_screen.py — the screen face, alone.
#
# Same construction language as the key face (face_keys_v4.py): a 110mm cream
# deck, a flush inlay pocketed 1.4mm deep, a 0.5mm hairline part-line groove
# hugging it. Here the inlay is the touchscreen: black glass, glossy, dead flush
# with the deck, at EXACTLY the same size as the key face's grey plate (98.2mm
# square, 6mm plan corner radius) so the two faces read as one system.
#
# The UI is projected in object space onto the glass, so no UV work is needed
# and the top face maps 1:1 to the image.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python face_screen.py

import bpy
import bmesh
import math
from mathutils import Vector

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- shared with the key face ----
PLATE = 110.0
PT = 6.0
DECK_R = 3.0
INLAY = 98.2        # == the grey plate the keys sit on
INLAY_R = 6.0
GLASS_T = 1.4       # inlay/glass thickness -> flush top at z = PT
GROOVE_W = 0.5
GROOVE_D = 0.45

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


def rrect_pts(w, h, r):
    w2, h2 = w / 2 - r, h / 2 - r
    pts = []
    for cx, cy, a0 in ((w2, h2, 0.0), (-w2, h2, math.pi / 2),
                       (-w2, -h2, math.pi), (w2, -h2, 3 * math.pi / 2)):
        for i in range(ARC + 1):
            a = a0 + (math.pi / 2) * i / ARC
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def rrect_prism(name, cx, cy, w, h, r, z0, z1):
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


def glass_screen(name, img_path=None, emit=11.0):
    """Black cover glass. With img_path, the UI glows through it."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.004, 0.004, 0.005, 1.0)
    b.inputs["Roughness"].default_value = 0.075      # glossy cover glass
    b.inputs["IOR"].default_value = 1.52
    if "Coat Weight" in b.inputs:
        b.inputs["Coat Weight"].default_value = 0.28
        b.inputs["Coat Roughness"].default_value = 0.03
    if img_path:
        img = bpy.data.images.load(img_path)
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        tex.extension = "EXTEND"
        # object-space projection: maps the top face 1:1, no UVs required
        co = nt.nodes.new("ShaderNodeTexCoord")
        mp = nt.nodes.new("ShaderNodeMapping")
        k = 1.0 / mm(INLAY)
        mp.inputs["Scale"].default_value = (k, k, 1.0)
        mp.inputs["Location"].default_value = (0.5, 0.5, 0.0)
        nt.links.new(co.outputs["Object"], mp.inputs["Vector"])
        nt.links.new(mp.outputs["Vector"], tex.inputs["Vector"])
        nt.links.new(tex.outputs["Color"], b.inputs["Emission Color"])
        b.inputs["Emission Strength"].default_value = emit
    return m


def assign(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


def build(screen_on=True):
    clean_scene()
    deck_mat = matte("Deck", (0.862, 0.826, 0.744), rough=0.46)

    # ---- cream deck, pocketed for the flush screen ----
    deck = rrect_prism("Deck", 0, 0, PLATE, PLATE, DECK_R, 0, PT)
    boolean(deck, rrect_prism("pk", 0, 0, INLAY, INLAY, INLAY_R, PT - GLASS_T, PT + 2))
    g = rrect_prism("go", 0, 0, INLAY + 2 * GROOVE_W, INLAY + 2 * GROOVE_W,
                    INLAY_R + GROOVE_W, PT - GROOVE_D, PT + 2)
    boolean(g, rrect_prism("gi", 0, 0, INLAY, INLAY, INLAY_R, PT - GROOVE_D - 1, PT + 3))
    boolean(deck, g)
    bevel(deck, 0.45, segments=4, angle_limit=50)
    smooth(deck)
    assign(deck, deck_mat)

    # ---- the touchscreen: flush black glass, exactly INLAY square ----
    glass = rrect_prism("Screen", 0, 0, INLAY - 0.1, INLAY - 0.1, INLAY_R, PT - GLASS_T, PT)
    bevel(glass, 0.22, segments=3, angle_limit=50)
    smooth(glass)
    assign(glass, glass_screen("ScreenGlass",
                               f"{OUT_DIR}/screen_ui_sq.png" if screen_on else None))

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
        # Flag the light off the glass: it lights the deck diffusely but never
        # appears in the screen's mirror reflection (which would otherwise show
        # the light panel's edge as a hard seam across the display).
        L.visible_glossy = False
        L.visible_transmission = False
        return L

    # Black gobo overhead: the glass reflects THIS (so it reads black) while the
    # cream deck is lit by grazing lights below the card. Camera sits under it.
    bpy.ops.mesh.primitive_plane_add(size=9.0, location=(0, 0, mm(560)))
    gobo = bpy.context.object
    gobo.name = "Gobo"
    gm = bpy.data.materials.new("Gobo")
    gm.use_nodes = True
    gb = gm.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (0.008, 0.008, 0.009, 1.0)
    gb.inputs["Roughness"].default_value = 0.9
    gobo.data.materials.append(gm)

    # grazing lights, all beneath the gobo so nothing bounces off it
    # soft reflector the glass MAY see: gives a gentle sheen, no hard edges
    bpy.ops.mesh.primitive_plane_add(size=0.6, location=(mm(-260), mm(-120), mm(430)),
                                     rotation=(math.radians(38), 0, math.radians(-30)))
    refl = bpy.context.object
    refl.name = "Reflector"
    rm = bpy.data.materials.new("Reflector")
    rm.use_nodes = True
    rn = rm.node_tree
    em = rn.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (1, 1, 1, 1)
    em.inputs["Strength"].default_value = 1.6
    rn.links.new(em.outputs["Emission"], rn.nodes["Material Output"].inputs["Surface"])
    refl.data.materials.append(rm)

    light("L1", (mm(-300), mm(-230), mm(150)), 60, 0.55,
          (math.radians(66), 0, math.radians(-46)))
    light("L2", (mm(300), mm(-180), mm(140)), 26, 0.6,
          (math.radians(68), 0, math.radians(52)))
    light("L3", (0, mm(320), mm(150)), 34, 0.5, (math.radians(-64), 0, 0))
    light("L4", (mm(-40), mm(-330), mm(120)), 18, 0.5, (math.radians(74), 0, 0))


def cam(loc, lens, fstop=14.0):
    """Camera that always points at the face centre, so framing is exact."""
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


def render(path, samples=340):
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
    sc.cycles.max_bounces = 8
    sc.cycles.diffuse_bounces = 2
    sc.render.resolution_x = 1700
    sc.render.resolution_y = 1275
    sc.render.filepath = path
    try:
        sc.view_settings.look = "AgX - Punchy"
    except Exception:
        pass
    sc.view_settings.exposure = -1.9
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    # 1. screen on, straight down: the UI as graphic
    build(screen_on=True)
    cam((0, mm(-0.01), mm(330)), 62)
    render(f"{OUT_DIR}/face_screen_on_top.png")
    # 2. screen on, 3/4: shows flushness + glass reflection
    bpy.data.objects.remove(bpy.context.scene.camera, do_unlink=True)
    cam((mm(215), mm(-255), mm(215)), 58)
    render(f"{OUT_DIR}/face_screen_on_hero.png")
    # 3. screen off: pure black glass, industrial-design read
    build(screen_on=False)
    cam((mm(215), mm(-255), mm(215)), 58)
    render(f"{OUT_DIR}/face_screen_off.png")
