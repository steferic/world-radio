# product_render.py — ad-quality render of the World Radio cube.
#
# Construction: six discrete face plates over a black inner skeleton (seams =
# real part lines). Front carries the emissive touchscreen (true 38.5x51mm
# aperture, showing the actual firmware globe UI from screen_ui.png). Right is
# the full-face perforated speaker. Top carries a row of DSA-profile keycaps
# (dimensions per the open-source KeyV2 keycap library: 18.2mm base, spherical
# dish). Left is the debossed wordmark; back has the USB-C slot; bottom, feet.
#
# Renders two shots: assembled hero + exploded construction view.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python product_render.py

import bpy
import bmesh
import math
from mathutils import Matrix, Vector

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- master dimensions (mm) ----
PLATE = 110.0      # face plate square size
PT = 6.0           # plate thickness
SKEL = 52.0        # skeleton half-edge (plates' inner faces land on it)
OUTER = SKEL + PT  # 58 outer envelope
SKEL_R = 6.0       # skeleton corner rounding
SW, SH = 38.5, 51.0  # screen aperture (true 2.4" class panel, portrait)

# DSA keycap per KeyV2 spec
KEY_BASE = 18.2
KEY_TOP = 13.0
KEY_H = 7.5
KEY_PITCH = 19.05


def mm(v):
    return v * S


def clean_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.lights,
                  bpy.data.cameras, bpy.data.curves, bpy.data.images):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def boxmm(name, x0, x1, y0, y1, z0, z1):
    cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2
    bpy.ops.mesh.primitive_cube_add(size=1, location=(mm(cx), mm(cy), mm(cz)))
    ob = bpy.context.object
    ob.name = name
    ob.scale = (mm(x1 - x0), mm(y1 - y0), mm(z1 - z0))
    bpy.ops.object.transform_apply(scale=True)
    return ob


def add_cyl(name, r, depth, loc=(0, 0, 0), rot=(0, 0, 0), verts=48):
    bpy.ops.mesh.primitive_cylinder_add(radius=mm(r), depth=mm(depth),
                                        location=loc, rotation=rot, vertices=verts)
    ob = bpy.context.object
    ob.name = name
    return ob


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


def smooth(ob):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    try:
        bpy.ops.object.shade_auto_smooth(angle=0.7)
    except Exception:
        bpy.ops.object.shade_smooth()


def plastic(name, color, rough=0.45):
    """Plastic with subtle micro-surface variation (ad-grade, not CG-flat)."""
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
    bump.inputs["Strength"].default_value = 0.015
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    return m


def emissive_screen(name, img_path):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    img = bpy.data.images.load(img_path)
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
    bsdf.inputs["Emission Strength"].default_value = 3.2
    bsdf.inputs["Roughness"].default_value = 0.32
    return m


def assign(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


MATS = {}


def make_materials():
    MATS["black"] = plastic("Skeleton", (0.006, 0.006, 0.007), rough=0.55)
    MATS["cream"] = plastic("Cream", (0.885, 0.848, 0.768), rough=0.42)
    MATS["mustard"] = plastic("Mustard", (0.46, 0.235, 0.012), rough=0.4)
    MATS["dark"] = plastic("Dark", (0.006, 0.006, 0.008), rough=0.3)
    MATS["screen"] = emissive_screen("ScreenUI", f"{OUT_DIR}/screen_ui.png")


def keycap(cx, cy, top_z, mat, explode_v):
    """DSA-profile keycap (KeyV2 dims): tapered square, beveled, spherical dish."""
    bm = bmesh.new()
    b2, t2 = KEY_BASE / 2, KEY_TOP / 2
    v = []
    for (sx, sy) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        v.append(bm.verts.new((mm(sx * b2), mm(sy * b2), 0)))
    for (sx, sy) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        v.append(bm.verts.new((mm(sx * t2), mm(sy * t2), mm(KEY_H))))
    bm.faces.new((v[0], v[3], v[2], v[1]))
    bm.faces.new((v[4], v[5], v[6], v[7]))
    for i in range(4):
        a, b = v[i], v[(i + 1) % 4]
        c, d = v[4 + (i + 1) % 4], v[4 + i]
        bm.faces.new((a, b, c, d))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new("Key")
    bm.to_mesh(me)
    ob = bpy.data.objects.new("Key", me)
    bpy.context.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bevel(ob, 1.1, segments=5, angle_limit=48)
    # spherical top dish
    bpy.ops.mesh.primitive_uv_sphere_add(radius=mm(40.0), segments=64, ring_count=32,
                                         location=(0, 0, mm(KEY_H + 40.0 - 1.1)))
    dish = bpy.context.object
    boolean(ob, dish)
    smooth(ob)
    assign(ob, mat)
    ob.location = Vector((mm(cx), mm(cy), mm(top_z))) + explode_v
    return ob


def build(explode=0.0):
    clean_scene()
    make_materials()

    ex = {
        "front": Vector((0, -mm(explode), 0)),
        "back": Vector((0, mm(explode), 0)),
        "left": Vector((-mm(explode), 0, 0)),
        "right": Vector((mm(explode), 0, 0)),
        "top": Vector((0, 0, mm(explode))),
        "bottom": Vector((0, 0, -mm(explode))),
    }

    # ---- black inner skeleton ----
    skel = boxmm("Skeleton", -SKEL, SKEL, -SKEL, SKEL, -SKEL, SKEL)
    bevel(skel, SKEL_R, segments=10)
    smooth(skel)
    assign(skel, MATS["black"])

    P2 = PLATE / 2

    # ---- FRONT: cream plate + emissive screen ----
    front = boxmm("FaceScreen", -P2, P2, -OUTER, -SKEL, -P2, P2)
    scz = 8.0
    step = boxmm("st", -SW / 2 - 2, SW / 2 + 2, -OUTER - 1, -OUTER + 1.6,
                 scz - SH / 2 - 2, scz + SH / 2 + 2)
    boolean(front, step)
    win = boxmm("wn", -SW / 2, SW / 2, -OUTER - 2, -SKEL - 1, scz - SH / 2, scz + SH / 2)
    boolean(front, win)
    bevel(front, 1.6, segments=6)
    smooth(front)
    assign(front, MATS["cream"])
    front.location += ex["front"]
    # emissive UI plane just behind the aperture
    bpy.ops.mesh.primitive_plane_add(size=1, location=(0, mm(-OUTER + 2.6), mm(scz)),
                                     rotation=(math.radians(90), 0, 0))
    scr = bpy.context.object
    scr.name = "ScreenPlane"
    scr.scale = (mm(SW + 2), mm(SH + 2), 1)
    bpy.ops.object.transform_apply(scale=True)
    assign(scr, MATS["screen"])
    scr.location += ex["front"]

    # ---- RIGHT: mustard speaker plate, full-face perforation ----
    right = boxmm("FaceSpeaker", SKEL, OUTER, -P2, P2, -P2, P2)
    gh = []
    pitch = 6.2
    row = 0
    z = -46.0
    while z <= 46.0:
        yoff = (pitch / 2) if (row % 2) else 0.0
        y = -46.0 + yoff
        while y <= 46.0:
            if abs(y) <= 46.0:
                c = add_cyl("h", 1.9, PT * 3, loc=(mm(SKEL + PT / 2), mm(y), mm(z)),
                            rot=(0, math.radians(90), 0), verts=12)
                gh.append(c)
            y += pitch
        z += pitch * 0.866
        row += 1
    bpy.ops.object.select_all(action="DESELECT")
    for c in gh:
        c.select_set(True)
    bpy.context.view_layer.objects.active = gh[0]
    bpy.ops.object.join()
    boolean(right, bpy.context.object)
    bevel(right, 1.6, segments=6)
    smooth(right)
    assign(right, MATS["mustard"])
    right.location += ex["right"]

    # ---- TOP: cream plate + keywell + DSA keycap row ----
    top = boxmm("FaceKeys", -P2, P2, -P2, P2, SKEL, OUTER)
    well_w, well_d = 4 * KEY_PITCH + 8, KEY_BASE + 8
    well_cy = -18.0
    well = boxmm("wl", -well_w / 2, well_w / 2, well_cy - well_d / 2, well_cy + well_d / 2,
                 OUTER - 1.2, OUTER + 2)
    bevel(well, 2.5, segments=4)
    boolean(top, well)
    bevel(top, 1.6, segments=6)
    smooth(top)
    assign(top, MATS["cream"])
    top.location += ex["top"]
    xs = [(-1.5 + i) * KEY_PITCH for i in range(4)]
    for i, kx in enumerate(xs):
        mat = MATS["mustard"] if i == 3 else MATS["cream"]
        keycap(kx, well_cy, OUTER - 1.0, mat, ex["top"])

    # ---- LEFT: mustard plate with debossed wordmark ----
    left = boxmm("FaceLogo", -OUTER, -SKEL, -P2, P2, -P2, P2)
    bpy.ops.object.text_add(location=(mm(-OUTER - 0.4), 0, 0),
                            rotation=(math.radians(90), 0, math.radians(-90)))
    txt = bpy.context.object
    txt.data.body = "WORLD RADIO"
    txt.data.size = mm(9.5)
    txt.data.extrude = mm(1.4)
    txt.data.align_x = "CENTER"
    txt.data.align_y = "CENTER"
    bpy.ops.object.convert(target="MESH")
    txt = bpy.context.object
    boolean(left, txt)
    bevel(left, 1.6, segments=6)
    smooth(left)
    assign(left, MATS["mustard"])
    left.location += ex["left"]

    # ---- BACK: cream plate + USB-C slot ----
    back = boxmm("FaceBack", -P2, P2, SKEL, OUTER, -P2, P2)
    usb = boxmm("usb", -5.0, 5.0, SKEL - 2, OUTER + 2, -40.0, -36.5)
    bevel(usb, 1.6, segments=4)
    boolean(back, usb)
    bevel(back, 1.6, segments=6)
    smooth(back)
    assign(back, MATS["cream"])
    back.location += ex["back"]

    # ---- BOTTOM: cream plate + feet ----
    bottom = boxmm("FaceBottom", -P2, P2, -P2, P2, -OUTER, -SKEL)
    bevel(bottom, 1.6, segments=6)
    smooth(bottom)
    assign(bottom, MATS["cream"])
    bottom.location += ex["bottom"]
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        f = add_cyl("Foot", 7.0, 3.0, loc=(sx * mm(P2 - 20), sy * mm(P2 - 20), mm(-OUTER - 1.4)))
        bevel(f, 0.8, segments=3, angle_limit=40)
        smooth(f)
        assign(f, MATS["black"])
        f.location += ex["bottom"]

    # ---- studio ----
    ground = -OUTER - 3.0 - explode
    bpy.ops.mesh.primitive_plane_add(size=4.0, location=(0, 0.7, mm(ground) - 0.0005))
    bg = bpy.context.object
    bm2 = bmesh.new()
    bm2.from_mesh(bg.data)
    bmesh.ops.subdivide_edges(bm2, edges=bm2.edges[:], cuts=32, use_grid_fill=True)
    for v2 in bm2.verts:
        if v2.co.y > 0.4:
            v2.co.z += (v2.co.y - 0.4) ** 2 * 1.6
    bm2.to_mesh(bg.data)
    bg.data.update()
    bgmat = plastic("Backdrop", (0.80, 0.77, 0.73), rough=0.85)
    assign(bg, bgmat)

    dist = 1.0 + explode * 0.012
    bpy.ops.object.camera_add(
        location=(mm(265 * dist), mm(-310 * dist), mm(150 * dist)),
        rotation=(math.radians(70), 0, math.radians(40)))
    cam = bpy.context.object
    cam.data.lens = 68
    cam.data.dof.use_dof = True
    cam.data.dof.focus_distance = 0.40 * dist
    cam.data.dof.aperture_fstop = 9.0
    bpy.context.scene.camera = cam

    def light(name, loc, energy, size, rot):
        bpy.ops.object.light_add(type="AREA", location=loc, rotation=rot)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        L.data.size = size

    light("Key", (mm(-300), mm(-280), mm(320)), 65, 0.9,
          (math.radians(48), 0, math.radians(-42)))
    light("Fill", (mm(360), mm(-240), mm(140)), 26, 1.1,
          (math.radians(66), 0, math.radians(52)))
    light("Rim", (mm(80), mm(320), mm(300)), 85, 0.7, (math.radians(-46), 0, 0))
    light("Top", (0, 0, mm(500)), 18, 1.4, (0, 0, 0))


def setup_gpu():
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
        bpy.context.scene.cycles.device = "GPU"
        print("METAL GPU enabled")
    except Exception as e:
        print("GPU setup failed, CPU fallback:", e)


def render(path, samples=320):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    setup_gpu()
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.render.resolution_x = 1800
    sc.render.resolution_y = 1350
    sc.render.filepath = path
    sc.view_settings.look = "AgX - Base Contrast"
    sc.view_settings.exposure = -2.0
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    build(explode=0.0)
    render(f"{OUT_DIR}/ad_hero.png")
    build(explode=26.0)
    render(f"{OUT_DIR}/ad_exploded.png")
