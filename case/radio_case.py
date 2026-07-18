# radio_case.py — parametric World Radio enclosure, built + rendered in Blender.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python radio_case.py -- render
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python radio_case.py -- stl
#
# Concept: a retro transistor radio. Rounded two-tone shell; circular speaker
# grille on the left of the face, portrait 2.4" screen on the right, tuning
# knob on the right side panel, small feet.
#
# All key dimensions live in P (mm). The model is sized around the real parts:
# Waveshare 2.4" LCD module (70.5 x 43.3 mm PCB, ~37 x 49 mm visible area),
# 3" speaker driver (~78 mm), EC11 encoder, classic ESP32 dev board.

import bpy
import bmesh
import math
import sys
from mathutils import Vector

# ----------------------------------------------------------------------------
# Parameters (mm)
# ----------------------------------------------------------------------------
P = {
    "body_w": 172.0,   # width
    "body_h": 100.0,   # height
    "body_d": 50.0,    # depth
    "bevel": 7.0,      # edge roundness
    "wall": 3.0,

    # speaker (left of face)
    "grille_cx": -42.0,     # from body center
    "grille_cy": 0.0,
    "grille_r": 38.0,       # grille footprint radius (3" driver ~78mm)
    "grille_hole_r": 1.6,
    "grille_pitch": 5.6,

    # screen (right of face) — portrait 2.4"
    "screen_cx": 47.0,
    "screen_cy": 4.0,
    "screen_w": 38.5,   # visible-area window + small margin
    "screen_h": 51.0,
    "screen_recess": 1.6,

    # knob on the right side panel
    "knob_r": 15.0,
    "knob_len": 16.0,
    "knob_z": 8.0,      # above body center

    "foot_r": 5.0,
    "foot_h": 4.0,
    "foot_inset": 22.0,
}

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001  # mm -> blender meters


def mm(v):
    return v * S


# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------

def clean_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def add_box(name, w, h, d, loc=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    ob = bpy.context.object
    ob.name = name
    ob.scale = (mm(w) / 2 * 2 / 1, mm(h) / 2 * 2 / 1, mm(d) / 2 * 2 / 1)
    ob.scale = (mm(w), mm(h), mm(d))
    bpy.ops.object.transform_apply(scale=True)
    return ob


def add_cyl(name, r, depth, loc=(0, 0, 0), rot=(0, 0, 0), verts=64):
    bpy.ops.mesh.primitive_cylinder_add(radius=mm(r), depth=mm(depth), location=loc, rotation=rot, vertices=verts)
    ob = bpy.context.object
    ob.name = name
    return ob


def boolean(target, cutter, op="DIFFERENCE"):
    mod = target.modifiers.new(name="bool", type="BOOLEAN")
    mod.operation = op
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


def assign(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


# ----------------------------------------------------------------------------
# Build
# ----------------------------------------------------------------------------

def build_scene():
    clean_scene()

    body_w, body_h, body_d = P["body_w"], P["body_h"], P["body_d"]
    face_y = -mm(body_d) / 2

    mat_body = material("Body", (0.012, 0.118, 0.128), rough=0.38)      # deep teal
    mat_face = material("Face", (0.913, 0.875, 0.796), rough=0.55)      # warm cream
    mat_dark = material("Dark", (0.004, 0.004, 0.005), rough=0.55)
    mat_brass = material("Brass", (0.85, 0.64, 0.29), rough=0.25, metallic=1.0)
    mat_foot = material("Foot", (0.02, 0.02, 0.02), rough=0.8)

    # --- body ---
    body = add_box("Body", body_w, body_d, body_h)
    bevel(body, P["bevel"], segments=8)
    assign(body, mat_body)

    # --- face plate (a slightly proud cream panel on the front) ---
    fp_w, fp_h, fp_t = body_w - 14, body_h - 14, 3.0
    face = add_box("FacePlate", fp_w, fp_t, fp_h, loc=(0, face_y + mm(fp_t) / 2 - mm(0.8), 0))
    bevel(face, 2.2, segments=5)
    assign(face, mat_face)

    # --- speaker grille: drilled holes through the face plate, hex pattern ---
    gr = P["grille_r"]
    gx, gz = P["grille_cx"], P["grille_cy"]
    pitch = P["grille_pitch"]
    holes = []
    row = 0
    z = -gr
    while z <= gr:
        xoff = (pitch / 2) if (row % 2) else 0.0
        x = -gr + xoff
        while x <= gr:
            if math.hypot(x, z) <= gr - 2.0:
                holes.append((x, z))
            x += pitch
        z += pitch * 0.866
        row += 1
    # merge hole cutters into one mesh for speed
    cutters = []
    for (hx, hz) in holes:
        c = add_cyl("h", P["grille_hole_r"], fp_t * 6,
                    loc=(mm(gx + hx), face_y + mm(fp_t) / 2, mm(gz + hz)),
                    rot=(math.radians(90), 0, 0), verts=16)
        cutters.append(c)
    if cutters:
        bpy.ops.object.select_all(action="DESELECT")
        for c in cutters:
            c.select_set(True)
        bpy.context.view_layer.objects.active = cutters[0]
        bpy.ops.object.join()
        boolean(face, bpy.context.object)
    # dark cloth behind the grille
    cloth = add_cyl("GrilleCloth", gr, 1.2, loc=(mm(gx), face_y + mm(1.2), mm(gz)),
                    rot=(math.radians(90), 0, 0))
    assign(cloth, mat_dark)
    # brass trim ring
    ring = add_cyl("Ring", gr + 2.2, 2.0, loc=(mm(gx), face_y + mm(0.4), mm(gz)),
                   rot=(math.radians(90), 0, 0))
    inner = add_cyl("ri", gr + 0.6, 8.0, loc=(mm(gx), face_y + mm(0.4), mm(gz)),
                    rot=(math.radians(90), 0, 0))
    boolean(ring, inner)
    assign(ring, mat_brass)

    # --- screen window (portrait) on the right ---
    sw, sh = P["screen_w"], P["screen_h"]
    scx, scz = P["screen_cx"], P["screen_cy"]
    win = add_box("win", sw, fp_t * 6, sh, loc=(mm(scx), face_y + mm(fp_t) / 2, mm(scz)))
    boolean(face, win)
    # dark screen glass, slightly recessed
    glass = add_box("Screen", sw + 4, 1.2, sh + 4, loc=(mm(scx), face_y + mm(2.2), mm(scz)))
    assign(glass, mat_dark)
    # brass screen bezel
    bez = add_box("Bezel", sw + 5.5, 1.4, sh + 5.5, loc=(mm(scx), face_y + mm(0.6), mm(scz)))
    bezc = add_box("bc", sw + 1.5, 8.0, sh + 1.5, loc=(mm(scx), face_y + mm(0.6), mm(scz)))
    boolean(bez, bezc)
    assign(bez, mat_brass)

    # --- tuning knob on the right side panel ---
    kx = mm(body_w) / 2
    knob = add_cyl("Knob", P["knob_r"], P["knob_len"],
                   loc=(kx + mm(P["knob_len"]) / 2 - mm(4), 0, mm(P["knob_z"])),
                   rot=(0, math.radians(90), 0), verts=48)
    bevel(knob, 2.0, segments=4, angle_limit=40)
    assign(knob, mat_brass)
    # knurl hint: shallow grooves
    for i in range(18):
        a = i * (2 * math.pi / 18)
        gy = math.cos(a) * mm(P["knob_r"])
        gz2 = math.sin(a) * mm(P["knob_r"]) + mm(P["knob_z"])
        g = add_cyl("g", 0.8, P["knob_len"] + 4,
                    loc=(kx + mm(P["knob_len"]) / 2 - mm(4), gy, gz2),
                    rot=(0, math.radians(90), 0), verts=10)
        boolean(knob, g)

    # --- feet ---
    for sx in (-1, 1):
        f = add_cyl("Foot", P["foot_r"], P["foot_h"],
                    loc=(sx * (mm(body_w) / 2 - mm(P["foot_inset"])), 0,
                         -mm(body_h) / 2 - mm(P["foot_h"]) / 2 + mm(0.5)))
        assign(f, mat_foot)

    # --- backdrop, camera, lights ---
    bpy.ops.mesh.primitive_plane_add(size=2.5, location=(0, mm(body_d) * 6, -mm(body_h) / 2 - mm(P["foot_h"])))
    bg = bpy.context.object
    bg.name = "Backdrop"
    # curve the backdrop up behind
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

    def light(name, kind, loc, energy, size=1.0, rot=(0, 0, 0)):
        bpy.ops.object.light_add(type=kind, location=loc, rotation=rot)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        if kind == "AREA":
            L.data.size = size
        return L

    light("Key", "AREA", (mm(-250), mm(-260), mm(260)), 45, size=0.7,
          rot=(math.radians(50), 0, math.radians(-40)))
    light("Fill", "AREA", (mm(300), mm(-200), mm(120)), 18, size=0.9,
          rot=(math.radians(65), 0, math.radians(50)))
    light("Rim", "AREA", (mm(60), mm(260), mm(240)), 60, size=0.6,
          rot=(math.radians(-45), 0, 0))

    return body, face


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


def export_stl():
    # Export shells for printing: body and face plate as separate STLs.
    for name in ("Body", "FacePlate", "Knob"):
        ob = bpy.data.objects.get(name)
        if not ob:
            continue
        bpy.ops.object.select_all(action="DESELECT")
        ob.select_set(True)
        bpy.ops.wm.stl_export(filepath=f"{OUT_DIR}/{name.lower()}.stl",
                              export_selected_objects=True, global_scale=1000.0)


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    build_scene()
    if "stl" in args:
        export_stl()
    if "render" in args or not args:
        render(f"{OUT_DIR}/concept.png")
