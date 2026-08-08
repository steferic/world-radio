# faces_pair.py — the key face and the screen face, side by side.
#
# Both faces are built with identical construction (110mm cream deck, 98.2mm
# flush inlay at 6mm radius, 0.5mm hairline part-line groove) so the comparison
# is honest: same lighting, same camera, same scale, one render.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python faces_pair.py

import bpy
import bmesh
import math

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- shared face geometry ----
PLATE = 110.0
PT = 6.0
DECK_R = 3.0
INLAY = 98.2
INLAY_R = 6.0
INK_T = 1.4
GROOVE_W = 0.5
GROOVE_D = 0.45

# ---- keys ----
CAP = 29.0
CAP_TOP = 27.6
CAP_H = 5.4
CAP_R = 4.0
CAP_TOP_R = 4.3
RIM = 0.5
GAP = 1.6
PITCH = CAP + GAP
CAP_SIT = 1.1

SPREAD = 60.0   # face centre offset: 10mm gap between the two plates
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


def prism(name, cx, cy, w, h, r, z0, z1):
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
    bevel(ob, RIM, segments=5, angle_limit=35)
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


def glass_mat(name, img_path, emit=11.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.004, 0.004, 0.005, 1.0)
    b.inputs["Roughness"].default_value = 0.075
    b.inputs["IOR"].default_value = 1.52
    if "Coat Weight" in b.inputs:
        b.inputs["Coat Weight"].default_value = 0.28
        b.inputs["Coat Roughness"].default_value = 0.03
    img = bpy.data.images.load(img_path)
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.extension = "EXTEND"
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


def deck_with_pocket(name, deck_mat):
    """Cream deck, pocketed flush for a 98.2mm inlay, with part-line groove."""
    d = prism(name, 0, 0, PLATE, PLATE, DECK_R, 0, PT)
    boolean(d, prism("pk", 0, 0, INLAY, INLAY, INLAY_R, PT - INK_T, PT + 2))
    g = prism("go", 0, 0, INLAY + 2 * GROOVE_W, INLAY + 2 * GROOVE_W,
              INLAY_R + GROOVE_W, PT - GROOVE_D, PT + 2)
    boolean(g, prism("gi", 0, 0, INLAY, INLAY, INLAY_R, PT - GROOVE_D - 1, PT + 3))
    boolean(d, g)
    bevel(d, 0.45, segments=4, angle_limit=50)
    smooth(d)
    assign(d, deck_mat)
    return d


def offset(objs, dx):
    for o in objs:
        o.location.x += mm(dx)


def build():
    clean_scene()
    deck_mat = matte("Deck", (0.862, 0.826, 0.744), rough=0.46)
    cap_mat = matte("Keycap", (0.930, 0.914, 0.872), rough=0.33, coat=0.10)
    ink_mat = matte("Inlay", (0.013, 0.014, 0.016), rough=0.42, coat=0.06)

    # ---------- LEFT: key face ----------
    parts = [deck_with_pocket("KeyDeck", deck_mat)]
    ink = prism("KeyInlay", 0, 0, INLAY - 0.1, INLAY - 0.1, INLAY_R, PT - INK_T, PT)
    bevel(ink, 0.25, segments=3, angle_limit=50)
    smooth(ink)
    assign(ink, ink_mat)
    parts.append(ink)
    for r in range(3):
        for c in range(3):
            cap = keycap(f"Cap{r}{c}", -PITCH + c * PITCH, -PITCH + r * PITCH,
                         PT + CAP_SIT)
            assign(cap, cap_mat)
            parts.append(cap)
    offset(parts, -SPREAD)

    # ---------- RIGHT: screen face ----------
    parts2 = [deck_with_pocket("ScreenDeck", deck_mat)]
    glass = prism("Screen", 0, 0, INLAY - 0.1, INLAY - 0.1, INLAY_R, PT - INK_T, PT)
    bevel(glass, 0.22, segments=3, angle_limit=50)
    smooth(glass)
    assign(glass, glass_mat("ScreenGlass", f"{OUT_DIR}/screen_ui_sq.png"))
    parts2.append(glass)
    offset(parts2, SPREAD)

    # ---------- studio ----------
    bpy.ops.mesh.primitive_plane_add(size=4.0, location=(0, 0.55, -0.0004))
    bg = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(bg.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=28, use_grid_fill=True)
    for v in bm.verts:
        if v.co.y > 0.4:
            v.co.z += (v.co.y - 0.4) ** 2 * 1.4
    bm.to_mesh(bg.data)
    bg.data.update()
    assign(bg, matte("Backdrop", (0.50, 0.487, 0.470), rough=0.9))

    # black gobo so the glass reflects dark, camera sits beneath it
    bpy.ops.mesh.primitive_plane_add(size=9.0, location=(0, 0, mm(560)))
    gobo = bpy.context.object
    gm = bpy.data.materials.new("Gobo")
    gm.use_nodes = True
    gb = gm.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (0.008, 0.008, 0.009, 1.0)
    gb.inputs["Roughness"].default_value = 0.9
    gobo.data.materials.append(gm)

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

    light("L1", (mm(-330), mm(-240), mm(160)), 90, 0.6,
          (math.radians(66), 0, math.radians(-48)))
    light("L2", (mm(330), mm(-190), mm(150)), 38, 0.6,
          (math.radians(68), 0, math.radians(50)))
    light("L3", (0, mm(340), mm(160)), 46, 0.55, (math.radians(-64), 0, 0))
    light("L4", (0, mm(-350), mm(130)), 24, 0.5, (math.radians(74), 0, 0))


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


def render(path, w, h, samples=340):
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
    # straight down: judge geometry, proportions, tone
    cam((0, mm(-0.01), mm(430)), 55)
    render(f"{OUT_DIR}/faces_pair_top.png", 2000, 1150)
    # 3/4: judge flushness, materials, how they read as a pair
    bpy.data.objects.remove(bpy.context.scene.camera, do_unlink=True)
    cam((mm(120), mm(-330), mm(255)), 55)
    render(f"{OUT_DIR}/faces_pair_hero.png", 2000, 1250)
