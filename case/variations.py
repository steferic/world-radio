# variations.py — three World Radio enclosure design variations, rendered headless.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python variations.py
#
# V1 "Walnut Hi-Fi": mid-century wood cabinet, linen face, vertical slat grille,
#     brass front knob under a raised screen.
# V2 "Monolith": matte charcoal, crisp edges, concentric-ring grille, aluminum
#     side knob. Dieter-Rams-quiet.
# V3 "Cream Pop": chubby rounded cream body, burnt-orange face, big cream front
#     knob. Friendly kitchen-radio energy.

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


def add_box(name, w, d, h, loc=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
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
    mapping.inputs["Scale"].default_value = (28.0, 2.0, 2.0)  # stretched grain
    coord = nt.nodes.new("ShaderNodeTexCoord")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[0].color = (0.135, 0.062, 0.028, 1)  # dark walnut
    ramp.color_ramp.elements[1].position = 0.72
    ramp.color_ramp.elements[1].color = (0.32, 0.16, 0.075, 1)   # lighter grain
    nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])
    nt.links.new(mapping.outputs["Vector"], noise.inputs["Vector"])
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    return m


def assign(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


def knurl(knob, kx_center, r, z, axis="x", n=18, length=24.0):
    for i in range(n):
        a = i * (2 * math.pi / n)
        gy = math.cos(a) * mm(r)
        gz = math.sin(a) * mm(r) + mm(z)
        rot = (0, math.radians(90), 0) if axis == "x" else (math.radians(90), 0, 0)
        g = add_cyl("g", 0.8, length, loc=(kx_center, gy, gz) if axis == "x" else (kx_center + gy - kx_center + gy, 0, 0), rot=rot, verts=10)
        if axis == "x":
            g.location = (kx_center, gy, gz)
        boolean(knob, g)


# ------------------------------------------------------------------ build ----

def build_variation(V):
    clean_scene()
    body_w, body_h, body_d = V["dims"]
    face_y = -mm(body_d) / 2

    mat_body = material_wood("Body") if V.get("wood") else material("Body", V["body_color"], rough=V.get("body_rough", 0.4))
    mat_face = material("Face", V["face_color"], rough=0.55)
    mat_dark = material("Dark", (0.004, 0.004, 0.005), rough=0.55)
    mat_metal = material("Metal", V["accent_color"], rough=V.get("accent_rough", 0.25), metallic=V.get("accent_metallic", 1.0))
    mat_foot = material("Foot", (0.02, 0.02, 0.02), rough=0.8)

    # body
    body = add_box("Body", body_w, body_d, body_h)
    bevel(body, V["bevel"], segments=10)
    assign(body, mat_body)

    # face plate
    fp_w, fp_h, fp_t = body_w - V["face_margin"], body_h - V["face_margin"], 3.0
    face = add_box("FacePlate", fp_w, fp_t, fp_h, loc=(0, face_y + mm(fp_t) / 2 - mm(0.8), 0))
    bevel(face, 2.2, segments=5)
    assign(face, mat_face)

    # ---- grille ----
    gr = V["grille_r"]
    gx, gz = V["grille_c"]
    if V["grille"] == "hex":
        pitch = V.get("grille_pitch", 5.6)
        cutters = []
        row, z = 0, -gr
        while z <= gr:
            xoff = (pitch / 2) if (row % 2) else 0.0
            x = -gr + xoff
            while x <= gr:
                if math.hypot(x, z) <= gr - 2.0:
                    c = add_cyl("h", V.get("grille_hole_r", 1.6), fp_t * 6,
                                loc=(mm(gx + x), face_y + mm(fp_t) / 2, mm(gz + z)),
                                rot=(math.radians(90), 0, 0), verts=16)
                    cutters.append(c)
                x += pitch
            z += pitch * 0.866
            row += 1
        bpy.ops.object.select_all(action="DESELECT")
        for c in cutters:
            c.select_set(True)
        bpy.context.view_layer.objects.active = cutters[0]
        bpy.ops.object.join()
        boolean(face, bpy.context.object)
    elif V["grille"] == "slats":
        slot_w, gap = 3.2, 6.4
        x = -gr + slot_w
        while x <= gr - slot_w:
            half_h = math.sqrt(max(gr * gr - x * x, 0.0)) - 3.0
            if half_h > 4:
                s = add_box("s", slot_w, fp_t * 6, half_h * 2,
                            loc=(mm(gx + x), face_y + mm(fp_t) / 2, mm(gz)))
                bevel(s, 1.4, segments=3, angle_limit=30)
                boolean(face, s)
            x += gap
    elif V["grille"] == "rings":
        r = 6.0
        while r < gr:
            ring = add_cyl("rc", r + 1.6, fp_t * 6, loc=(mm(gx), face_y + mm(fp_t) / 2, mm(gz)),
                           rot=(math.radians(90), 0, 0))
            hole = add_cyl("rh", r, fp_t * 8, loc=(mm(gx), face_y + mm(fp_t) / 2, mm(gz)),
                           rot=(math.radians(90), 0, 0))
            boolean(ring, hole)
            boolean(face, ring)
            r += 4.6
        # center dot
        c = add_cyl("cd", 1.8, fp_t * 6, loc=(mm(gx), face_y + mm(fp_t) / 2, mm(gz)),
                    rot=(math.radians(90), 0, 0))
        boolean(face, c)

    # cloth behind grille
    cloth = add_cyl("GrilleCloth", gr, 1.2, loc=(mm(gx), face_y + mm(1.4), mm(gz)),
                    rot=(math.radians(90), 0, 0))
    assign(cloth, mat_dark)
    if V.get("grille_ring"):
        ring = add_cyl("Ring", gr + 2.2, 2.0, loc=(mm(gx), face_y + mm(0.4), mm(gz)),
                       rot=(math.radians(90), 0, 0))
        inner = add_cyl("ri", gr + 0.6, 8.0, loc=(mm(gx), face_y + mm(0.4), mm(gz)),
                        rot=(math.radians(90), 0, 0))
        boolean(ring, inner)
        assign(ring, mat_metal)

    # ---- screen ----
    sw, sh = V["screen_wh"]
    scx, scz = V["screen_c"]
    win = add_box("win", sw, fp_t * 6, sh, loc=(mm(scx), face_y + mm(fp_t) / 2, mm(scz)))
    boolean(face, win)
    glass = add_box("Screen", sw + 4, 1.2, sh + 4, loc=(mm(scx), face_y + mm(2.4), mm(scz)))
    assign(glass, mat_dark)
    bez = add_box("Bezel", sw + 5.5, 1.6, sh + 5.5, loc=(mm(scx), face_y + mm(0.4), mm(scz)))
    bezc = add_box("bc", sw + 1.5, 8.0, sh + 1.5, loc=(mm(scx), face_y + mm(0.4), mm(scz)))
    boolean(bez, bezc)
    assign(bez, mat_metal)

    # ---- knob ----
    kr, klen = V["knob_r"], V["knob_len"]
    if V["knob"] == "side":
        kx = mm(body_w) / 2
        knob = add_cyl("Knob", kr, klen, loc=(kx + mm(klen) / 2 - mm(4), 0, mm(V["knob_z"])),
                       rot=(0, math.radians(90), 0), verts=48)
        bevel(knob, 2.0, segments=4, angle_limit=40)
        assign(knob, mat_metal)
        for i in range(18):
            a = i * (2 * math.pi / 18)
            g = add_cyl("g", 0.8, klen + 6,
                        loc=(kx + mm(klen) / 2 - mm(4), math.cos(a) * mm(kr), math.sin(a) * mm(kr) + mm(V["knob_z"])),
                        rot=(0, math.radians(90), 0), verts=10)
            boolean(knob, g)
    else:  # front
        fx, fz = V["knob_c"]
        knob = add_cyl("Knob", kr, klen, loc=(mm(fx), face_y - mm(klen) / 2 + mm(2), mm(fz)),
                       rot=(math.radians(90), 0, 0), verts=48)
        bevel(knob, 2.4, segments=5, angle_limit=40)
        assign(knob, mat_metal if V.get("knob_metal", True) else mat_face)
        for i in range(16):
            a = i * (2 * math.pi / 16)
            g = add_cyl("g", 0.9, klen + 6,
                        loc=(mm(fx) + math.cos(a) * mm(kr), face_y - mm(klen) / 2 + mm(2), math.sin(a) * mm(kr) + mm(fz)),
                        rot=(math.radians(90), 0, 0), verts=10)
            boolean(knob, g)
        # indicator dot
        dot = add_cyl("Dot", 1.4, 2.0, loc=(mm(fx), face_y - mm(klen) + mm(2.6), mm(fz) + mm(kr * 0.55)),
                      rot=(math.radians(90), 0, 0), verts=12)
        assign(dot, mat_dark)

    # feet
    for sx in (-1, 1):
        f = add_cyl("Foot", 5.0, 4.0,
                    loc=(sx * (mm(body_w) / 2 - mm(22)), 0, -mm(body_h) / 2 - mm(2.0) + mm(0.5)))
        assign(f, mat_foot)

    # backdrop / camera / lights
    bpy.ops.mesh.primitive_plane_add(size=2.5, location=(0, mm(body_d) * 6, -mm(body_h) / 2 - mm(4)))
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

    bpy.ops.object.camera_add(location=(mm(200), mm(-310), mm(110)),
                              rotation=(math.radians(72), 0, math.radians(33)))
    cam = bpy.context.object
    cam.data.lens = 60
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
    sc.cycles.samples = 128
    sc.cycles.use_denoising = True
    sc.render.resolution_x = 1280
    sc.render.resolution_y = 960
    sc.render.filepath = path
    sc.view_settings.look = "AgX - Base Contrast"
    sc.view_settings.exposure = -2.2
    bpy.ops.render.render(write_still=True)


VARIATIONS = [
    {   # V1 — Walnut Hi-Fi
        "name": "variation_1_walnut",
        "dims": (180, 95, 55), "bevel": 4.5, "face_margin": 16, "wood": True,
        "body_color": None, "face_color": (0.895, 0.862, 0.788),
        "accent_color": (0.85, 0.64, 0.29), "accent_metallic": 1.0,
        "grille": "slats", "grille_r": 36.0, "grille_c": (-48.0, 0.0), "grille_ring": False,
        "screen_wh": (38.5, 51.0), "screen_c": (44.0, 11.0),
        "knob": "front", "knob_c": (44.0, -27.0), "knob_r": 11.0, "knob_len": 10.0,
    },
    {   # V2 — Monolith
        "name": "variation_2_monolith",
        "dims": (172, 100, 46), "bevel": 2.6, "face_margin": 10,
        "body_color": (0.022, 0.022, 0.024), "body_rough": 0.5,
        "face_color": (0.038, 0.038, 0.042),
        "accent_color": (0.75, 0.75, 0.77), "accent_metallic": 1.0, "accent_rough": 0.35,
        "grille": "rings", "grille_r": 37.0, "grille_c": (-42.0, 0.0), "grille_ring": False,
        "screen_wh": (38.5, 51.0), "screen_c": (47.0, 4.0),
        "knob": "side", "knob_z": 8.0, "knob_r": 14.0, "knob_len": 14.0,
    },
    {   # V3 — Cream Pop
        "name": "variation_3_creampop",
        "dims": (162, 106, 60), "bevel": 12.0, "face_margin": 20,
        "body_color": (0.88, 0.84, 0.76), "body_rough": 0.5,
        "face_color": (0.72, 0.235, 0.08),
        "accent_color": (0.88, 0.84, 0.76), "accent_metallic": 0.0, "accent_rough": 0.5,
        "grille": "hex", "grille_r": 34.0, "grille_c": (-40.0, 6.0), "grille_ring": False,
        "grille_pitch": 6.4, "grille_hole_r": 2.1,
        "screen_wh": (38.5, 51.0), "screen_c": (44.0, 10.0),
        "knob": "front", "knob_c": (44.0, -28.0), "knob_r": 13.0, "knob_len": 12.0,
        "knob_metal": False,
    },
]

if __name__ == "__main__":
    for V in VARIATIONS:
        build_variation(V)
        render(f"{OUT_DIR}/{V['name']}.png")
