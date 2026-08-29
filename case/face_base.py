# face_base.py — the base of the cube: bare cream plate, four rubber pads.
#
# Deliberately the quietest face. Same 110mm cream deck and 3mm plan corners as
# the others, but no inlay: just four soft-black rubber pads, one per corner,
# each seated in a shallow 0.5mm recess so it can't peel and reads as fitted
# rather than stuck on. Rendered face-up, as you'd inspect the underside.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python face_base.py

import bpy
import bmesh
import math

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

PLATE = 110.0
PT = 6.0
DECK_R = 3.0

PAD_DIA = 14.0
PAD_H = 1.9          # total pad thickness
PAD_RECESS = 0.5     # seated depth -> 1.4mm proud
PAD_INSET = 13.0     # pad centre inset from each edge
PAD_R = PLATE / 2 - PAD_INSET   # 42mm

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


def add_solid(bm, pts, z0, z1, cx=0.0, cy=0.0):
    bot = [bm.verts.new((mm(cx + x), mm(cy + y), mm(z0))) for (x, y) in pts]
    top = [bm.verts.new((mm(cx + x), mm(cy + y), mm(z1))) for (x, y) in pts]
    n = len(pts)
    for i in range(n):
        bm.faces.new((bot[i], bot[(i + 1) % n], top[(i + 1) % n], top[i]))
    bm.faces.new(list(reversed(bot)))
    bm.faces.new(top)


def finish(bm, name):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    return ob


def prism(name, cx, cy, w, h, r, z0, z1):
    bm = bmesh.new()
    add_solid(bm, rrect_pts(w, h, r), z0, z1, cx, cy)
    return finish(bm, name)


def disc(name, cx, cy, dia, z0, z1, verts=80):
    bm = bmesh.new()
    r = dia / 2
    pts = [(r * math.cos(2 * math.pi * i / verts), r * math.sin(2 * math.pi * i / verts))
           for i in range(verts)]
    add_solid(bm, pts, z0, z1, cx, cy)
    return finish(bm, name)


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


def rubber(name):
    """Soft matte rubber: near-black, very diffuse, fine surface tooth."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.019, 0.019, 0.021, 1.0)
    b.inputs["Roughness"].default_value = 0.86
    n = nt.nodes.new("ShaderNodeTexNoise")
    n.inputs["Scale"].default_value = 2600.0
    n.inputs["Detail"].default_value = 4.0
    bp = nt.nodes.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value = 0.05
    nt.links.new(n.outputs["Fac"], bp.inputs["Height"])
    nt.links.new(bp.outputs["Normal"], b.inputs["Normal"])
    return m


def assign(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


def build():
    clean_scene()
    deck_mat = matte("Deck", (0.862, 0.826, 0.744), rough=0.46)
    pad_mat = rubber("Rubber")

    # ---- bare cream base plate with four shallow pad recesses ----
    deck = prism("Base", 0, 0, PLATE, PLATE, DECK_R, 0, PT)
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        boolean(deck, disc("rec", sx * PAD_R, sy * PAD_R, PAD_DIA + 0.6,
                           PT - PAD_RECESS, PT + 2))
    bevel(deck, 0.45, segments=4, angle_limit=50)
    smooth(deck)
    assign(deck, deck_mat)

    # ---- four rubber pads ----
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        pad = disc("Pad", sx * PAD_R, sy * PAD_R, PAD_DIA,
                   PT - PAD_RECESS, PT - PAD_RECESS + PAD_H)
        bevel(pad, 0.55, segments=4, angle_limit=40)
        smooth(pad, angle=0.6)
        assign(pad, pad_mat)

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


def render(path, w=1800, h=1350, samples=300):
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
    render(f"{OUT_DIR}/face_base_top.png")
    bpy.data.objects.remove(bpy.context.scene.camera, do_unlink=True)
    cam((mm(200), mm(-250), mm(195)), 56)
    render(f"{OUT_DIR}/face_base_hero.png")
