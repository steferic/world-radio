# wild_variations.py — three structurally different World Radio forms.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python wild_variations.py
#
# W1 "Block":  deep near-cube desk unit, top-firing speaker, front screen,
#              top dial. Maximum desk stability.
# W2 "Wedge":  console with a deep base and a slanted control face (screen +
#              knob on the slope, vent slots below). Low center of gravity.
# W3 "Globe":  a sphere on a brass stand — screen recessed in the front,
#              radial grille on the back hemisphere, north-pole knob.

import bpy
import bmesh
import math
from mathutils import Vector

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001


def mm(v):
    return v * S


# ---------------------------------------------------------------- helpers ----

def clean_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def add_box(name, w, d, h, loc=(0, 0, 0), rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    ob = bpy.context.object
    ob.name = name
    ob.scale = (mm(w), mm(d), mm(h))
    bpy.ops.object.transform_apply(scale=True)
    return ob


def add_cyl(name, r, depth, loc=(0, 0, 0), rot=(0, 0, 0), verts=64):
    bpy.ops.mesh.primitive_cylinder_add(radius=mm(r), depth=mm(depth), location=loc, rotation=rot, vertices=verts)
    ob = bpy.context.object
    ob.name = name
    return ob


def boolean(target, cutter):
    mod = target.modifiers.new(name="bool", type="BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.object = cutter
    mod.solver = "EXACT"
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)


def bevel(ob, width_mm, segments=6, angle_limit=60):
    mod = ob.modifiers.new(name="bevel", type="BEVEL")
    mod.width = mm(width_mm)
    mod.segments = segments
    mod.limit_method = "ANGLE"
    mod.angle_limit = math.radians(angle_limit)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)


def material(name, color, rough=0.45, metallic=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metallic
    return m


def material_wood(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.42
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 9.0
    noise.inputs["Detail"].default_value = 8.0
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (28.0, 2.0, 2.0)
    coord = nt.nodes.new("ShaderNodeTexCoord")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[0].color = (0.08, 0.035, 0.015, 1)
    ramp.color_ramp.elements[1].position = 0.72
    ramp.color_ramp.elements[1].color = (0.22, 0.105, 0.048, 1)
    nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])
    nt.links.new(mapping.outputs["Vector"], noise.inputs["Vector"])
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    return m


def assign(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


def hex_holes_cutter(cx_mm, cz_mm, radius, pitch, hole_r, y_mm, axis_rot, thick):
    """Hex-packed cylinder cutters (facing given rot), joined into one object."""
    cutters = []
    row, z = 0, -radius
    while z <= radius:
        xoff = (pitch / 2) if (row % 2) else 0.0
        x = -radius + xoff
        while x <= radius:
            if math.hypot(x, z) <= radius - 2.0:
                c = add_cyl("h", hole_r, thick,
                            loc=(cx_mm + mm(x), y_mm, cz_mm + mm(z)), rot=axis_rot, verts=14)
                cutters.append(c)
            x += pitch
        z += pitch * 0.866
        row += 1
    bpy.ops.object.select_all(action="DESELECT")
    for c in cutters:
        c.select_set(True)
    bpy.context.view_layer.objects.active = cutters[0]
    bpy.ops.object.join()
    return bpy.context.object


def studio(cam_loc, cam_rot, lens=60):
    bpy.ops.mesh.primitive_plane_add(size=2.5, location=(0, 0.5, -0.001))
    bg = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(bg.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=24, use_grid_fill=True)
    for v in bm.verts:
        if v.co.y > 0.25:
            v.co.z += (v.co.y - 0.25) ** 2 * 2.2
    bm.to_mesh(bg.data)
    bg.data.update()
    assign(bg, material("Backdrop", (0.82, 0.79, 0.75), rough=0.9))

    bpy.ops.object.camera_add(location=cam_loc, rotation=cam_rot)
    cam = bpy.context.object
    cam.data.lens = lens
    bpy.context.scene.camera = cam

    def light(name, loc, energy, size, rot):
        bpy.ops.object.light_add(type="AREA", location=loc, rotation=rot)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        L.data.size = size

    light("Key", (mm(-250), mm(-260), mm(260)), 45, 0.7, (math.radians(50), 0, math.radians(-40)))
    light("Fill", (mm(300), mm(-200), mm(120)), 18, 0.9, (math.radians(65), 0, math.radians(50)))
    light("Rim", (mm(60), mm(260), mm(240)), 60, 0.6, (math.radians(-45), 0, 0))


def render(path):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 96
    sc.cycles.use_denoising = True
    sc.render.resolution_x = 1280
    sc.render.resolution_y = 960
    sc.render.filepath = path
    sc.view_settings.look = "AgX - Base Contrast"
    sc.view_settings.exposure = -2.2
    bpy.ops.render.render(write_still=True)


# ------------------------------------------------------------- W1: Block -----

def build_block():
    clean_scene()
    W, H, D = 112.0, 118.0, 94.0  # deep, slightly upright: very stable
    face_y = -mm(D) / 2
    ground = -mm(H) / 2

    teal = material("Body", (0.012, 0.118, 0.128), rough=0.38)
    cream = material("Face", (0.913, 0.875, 0.796), rough=0.55)
    dark = material("Dark", (0.004, 0.004, 0.005), rough=0.55)
    brass = material("Brass", (0.85, 0.64, 0.29), rough=0.25, metallic=1.0)

    body = add_box("Body", W, D, H)
    bevel(body, 8.0, segments=10)
    assign(body, teal)

    # top plate (cream) with top-firing speaker grille
    top = add_box("TopPlate", W - 16, D - 16, 3.0, loc=(0, 0, mm(H) / 2 - mm(0.6)))
    bevel(top, 2.0, segments=4)
    assign(top, cream)
    cut = hex_holes_cutter(0, 0, 34.0, 5.6, 1.6, 0, (0, 0, 0), 30)
    # hex_holes_cutter builds in XZ; for a top grille we need XY — rebuild manually:
    bpy.data.objects.remove(cut, do_unlink=True)
    cutters = []
    row, yy = 0, -34.0
    while yy <= 34.0:
        xoff = (5.6 / 2) if (row % 2) else 0.0
        x = -34.0 + xoff
        while x <= 34.0:
            if math.hypot(x, yy) <= 32.0:
                c = add_cyl("h", 1.6, 30, loc=(mm(x), mm(yy) + mm(6), mm(H) / 2 - mm(0.6)), verts=14)
                cutters.append(c)
            x += 5.6
        yy += 5.6 * 0.866
        row += 1
    bpy.ops.object.select_all(action="DESELECT")
    for c in cutters:
        c.select_set(True)
    bpy.context.view_layer.objects.active = cutters[0]
    bpy.ops.object.join()
    boolean(top, bpy.context.object)
    cloth = add_cyl("Cloth", 32.5, 1.2, loc=(0, mm(6), mm(H) / 2 - mm(2.2)))
    assign(cloth, dark)

    # front screen, centered
    sw, sh = 38.5, 51.0
    scz = 16.0
    win = add_box("win", sw, 8, sh, loc=(0, face_y + mm(1.5), mm(scz)))
    boolean(body, win)
    glass = add_box("Screen", sw + 3, 1.2, sh + 3, loc=(0, face_y + mm(1.2), mm(scz)))
    assign(glass, dark)
    bez = add_box("Bezel", sw + 6.5, 1.6, sh + 6.5, loc=(0, face_y - mm(0.2), mm(scz)))
    bezc = add_box("bc", sw + 0.5, 9, sh + 0.5, loc=(0, face_y - mm(0.2), mm(scz)))
    boolean(bez, bezc)
    assign(bez, brass)
    try:
        bpy.ops.object.select_all(action="DESELECT")
        body.select_set(True)
        bpy.context.view_layer.objects.active = body
        bpy.ops.object.shade_auto_smooth(angle=0.61)
    except Exception:
        pass

    # top dial knob, front-right corner of the top
    kx, ky = mm(W) / 2 - mm(24), -mm(D) / 2 + mm(22)
    knob = add_cyl("Knob", 13.0, 12.0, loc=(kx, ky, mm(H) / 2 + mm(4)), verts=48)
    bevel(knob, 2.0, segments=4, angle_limit=40)
    assign(knob, brass)
    for i in range(16):
        a = i * (2 * math.pi / 16)
        g = add_cyl("g", 0.9, 20, loc=(kx + math.cos(a) * mm(13), ky + math.sin(a) * mm(13), mm(H) / 2 + mm(4)), verts=10)
        boolean(knob, g)

    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        f = add_cyl("Foot", 6, 3.5, loc=(sx * (mm(W) / 2 - mm(18)), sy * (mm(D) / 2 - mm(18)), ground - mm(1.2)))
        assign(f, material("Foot", (0.02, 0.02, 0.02), rough=0.8))

    studio((mm(170), mm(-250), mm(130)), (math.radians(70), 0, math.radians(33)))


# ------------------------------------------------------------- W2: Wedge -----

def build_wedge():
    clean_scene()
    W, H, D = 170.0, 100.0, 115.0
    t = math.radians(30)
    z1 = -20.0   # where the slope starts on the front face
    yf, yb = -D / 2, D / 2
    zb, zt = -H / 2, H / 2
    y2 = yf + (zt - z1) * math.tan(t)  # slope top lands here on the top face

    wood = material_wood("Body")
    cream = material("Face", (0.913, 0.875, 0.796), rough=0.55)
    dark = material("Dark", (0.004, 0.004, 0.005), rough=0.55)
    brass = material("Brass", (0.85, 0.64, 0.29), rough=0.25, metallic=1.0)

    # explicit prism: front face -> slope -> flat top -> back (no boolean slicing)
    cross = [(yf, zb), (yf, z1), (y2, zt), (yb, zt), (yb, zb)]
    bm = bmesh.new()
    rings = []
    for x in (-W / 2, W / 2):
        rings.append([bm.verts.new((mm(x), mm(yy), mm(zz))) for (yy, zz) in cross])
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[1])))
    nvi = len(cross)
    for i in range(nvi):
        a, b = rings[0][i], rings[0][(i + 1) % nvi]
        c, d = rings[1][(i + 1) % nvi], rings[1][i]
        bm.faces.new((a, b, c, d))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new("Body")
    bm.to_mesh(me)
    ob = bpy.data.objects.new("Body", me)
    bpy.context.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bevel(ob, 4.0, segments=8)
    assign(ob, wood)

    # slope frame
    n = Vector((0, -math.cos(t), math.sin(t)))
    up = Vector((0, math.sin(t), math.cos(t)))
    B = Vector((0, mm(yf), mm(z1)))
    L = (zt - z1) / math.cos(t)  # slope length in mm

    def on_slope(x_mm, d_along, offset_out, obj):
        p = B + up * mm(d_along) + n * mm(offset_out)
        obj.location = (mm(x_mm), p.y, p.z)

    # cream control panel proud on the slope
    panel = add_box("FacePanel", W - 24, 2.4, L - 14, rot=(-t, 0, 0))
    on_slope(0, L / 2, 0.6, panel)
    bevel(panel, 1.6, segments=4)
    assign(panel, cream)

    # hex grille (left) cut through the panel, cloth BEHIND it
    gr, pitch = 26.0, 5.8
    cutters = []
    row, v = 0, -gr
    while v <= gr:
        xoff = (pitch / 2) if (row % 2) else 0.0
        u = -gr + xoff
        while u <= gr:
            if math.hypot(u, v) <= gr - 2.0:
                c = add_cyl("h", 1.7, 30, rot=(math.radians(90) - t, 0, 0), verts=12)
                on_slope(-45.0 + u, L / 2 + v, 0.6, c)
                cutters.append(c)
            u += pitch
        v += pitch * 0.866
        row += 1
    bpy.ops.object.select_all(action="DESELECT")
    for c in cutters:
        c.select_set(True)
    bpy.context.view_layer.objects.active = cutters[0]
    bpy.ops.object.join()
    boolean(panel, bpy.context.object)
    cloth = add_cyl("Cloth", gr + 1, 1.0, rot=(math.radians(90) - t, 0, 0))
    on_slope(-45.0, L / 2, -1.6, cloth)
    assign(cloth, dark)

    # screen (right) — glass recessed BEHIND the panel window
    sw, sh = 38.5, 51.0
    scx = 28.0
    win = add_box("win", sw, 30, sh, rot=(-t, 0, 0))
    on_slope(scx, L / 2, 0.6, win)
    boolean(panel, win)
    glass = add_box("Screen", sw + 5, 1.2, sh + 5, rot=(-t, 0, 0))
    on_slope(scx, L / 2, -1.2, glass)
    assign(glass, dark)
    bez = add_box("Bezel", sw + 6, 1.4, sh + 6, rot=(-t, 0, 0))
    on_slope(scx, L / 2, 1.6, bez)
    bezc = add_box("bc", sw + 0.5, 10, sh + 0.5, rot=(-t, 0, 0))
    on_slope(scx, L / 2, 1.6, bezc)
    boolean(bez, bezc)
    assign(bez, brass)

    # brass knob, right of the screen on the slope
    knob = add_cyl("Knob", 12.0, 11.0, rot=(math.radians(90) - t, 0, 0), verts=48)
    on_slope(64.0, L / 2 - 6, 5.5, knob)
    bevel(knob, 2.0, segments=4, angle_limit=40)
    assign(knob, brass)
    dot = add_cyl("Dot", 1.3, 2.0, rot=(math.radians(90) - t, 0, 0), verts=10)
    on_slope(64.0, L / 2 - 6 + 12 * 0.55, 11.2, dot)
    assign(dot, dark)

    # vent slots on the front vertical band
    for vz in (-32.0, -41.0):
        s = add_box("s", W - 60, 24, 4.2, loc=(0, mm(yf) + mm(2), mm(vz)))
        bevel(s, 1.5, segments=3, angle_limit=30)
        boolean(ob, s)

    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        f = add_cyl("Foot", 6, 3.5, loc=(sx * (mm(W) / 2 - mm(24)), sy * (mm(D) / 2 - mm(24)), mm(zb) - mm(1.2)))
        assign(f, material("Foot", (0.02, 0.02, 0.02), rough=0.8))

    studio((mm(200), mm(-310), mm(180)), (math.radians(65), 0, math.radians(32)))


# ------------------------------------------------------------- W3: Globe -----

def build_globe():
    clean_scene()
    R = 58.0

    navy = material("Body", (0.005, 0.02, 0.06), rough=0.35)
    dark = material("Dark", (0.004, 0.004, 0.005), rough=0.5)
    brass = material("Brass", (0.85, 0.64, 0.29), rough=0.25, metallic=1.0)

    bpy.ops.mesh.primitive_uv_sphere_add(radius=mm(R), segments=96, ring_count=64, location=(0, 0, 0))
    sphere = bpy.context.object
    sphere.name = "Body"
    bpy.ops.object.shade_smooth()
    assign(sphere, navy)

    # radial grille holes over the back hemisphere
    cutters = []
    for lat in range(-40, 41, 13):
        lat_r = math.radians(lat)
        lon_step = max(10, int(12 / max(math.cos(lat_r), 0.3)))
        for lon in range(95, 266, lon_step):
            lon_r = math.radians(lon)
            d = Vector((math.sin(lon_r) * math.cos(lat_r), -math.cos(lon_r) * math.cos(lat_r), math.sin(lat_r)))
            c = add_cyl("h", 2.0, 16, loc=(d * mm(R)).to_tuple(), verts=12)
            c.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
            cutters.append(c)
    bpy.ops.object.select_all(action="DESELECT")
    for c in cutters:
        c.select_set(True)
    bpy.context.view_layer.objects.active = cutters[0]
    bpy.ops.object.join()
    boolean(sphere, bpy.context.object)

    # flat screen recess on the front
    rec = add_cyl("rec", 33.0, 26, loc=(0, -mm(R) - mm(1), 0), rot=(math.radians(90), 0, 0))
    boolean(sphere, rec)
    face_flat_y = -mm(R) + mm(12)
    glass = add_cyl("ScreenDisc", 32.0, 1.4, loc=(0, face_flat_y + mm(0.4), 0), rot=(math.radians(90), 0, 0))
    assign(glass, dark)
    ring = add_cyl("Rim", 34.6, 2.4, loc=(0, face_flat_y - mm(0.2), 0), rot=(math.radians(90), 0, 0))
    ri = add_cyl("ri", 32.4, 9, loc=(0, face_flat_y - mm(0.2), 0), rot=(math.radians(90), 0, 0))
    boolean(ring, ri)
    assign(ring, brass)

    # brass stand
    stand = add_cyl("Stand", 34.0, 16.0, loc=(0, 0, -mm(R) - mm(2)), verts=64)
    assign(stand, brass)
    base = add_cyl("Base", 44.0, 5.0, loc=(0, 0, -mm(R) - mm(9)), verts=64)
    bevel(base, 1.6, segments=4, angle_limit=40)
    assign(base, brass)

    # north-pole tuning knob
    knob = add_cyl("Knob", 12.0, 15.0, loc=(0, 0, mm(R) + mm(3.5)), verts=48)
    bevel(knob, 2.2, segments=4, angle_limit=40)
    assign(knob, brass)
    for i in range(16):
        a = i * (2 * math.pi / 16)
        g = add_cyl("g", 0.8, 24, loc=(math.cos(a) * mm(12), math.sin(a) * mm(12), mm(R) + mm(3.5)), verts=10)
        boolean(knob, g)

    # raise everything so the base sits on the ground plane
    lift = mm(R) + mm(11.5)
    for ob in bpy.data.objects:
        if ob.type == "MESH":
            ob.location.z += lift

    studio((mm(280), mm(-430), mm(230)), (math.radians(72), 0, math.radians(33)), lens=58)


if __name__ == "__main__":
    build_block()
    render(f"{OUT_DIR}/wild_1_block.png")
    build_wedge()
    render(f"{OUT_DIR}/wild_2_wedge.png")
    build_globe()
    render(f"{OUT_DIR}/wild_3_globe.png")
