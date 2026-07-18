# cube_design.py — World Radio as a cube: screen on the front face, speaker
# grille owning the right face, tuning knob on top.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python cube_design.py

import bpy
import bmesh
import math
from mathutils import Vector

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001


def mm(v):
    return v * S


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


def assign(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


def build():
    clean_scene()
    A = 112.0  # cube edge
    half = mm(A) / 2

    teal = material("Body", (0.012, 0.118, 0.128), rough=0.38)
    cream = material("Face", (0.913, 0.875, 0.796), rough=0.55)
    dark = material("Dark", (0.004, 0.004, 0.005), rough=0.55)
    brass = material("Brass", (0.85, 0.64, 0.29), rough=0.25, metallic=1.0)

    body = add_box("Body", A, A, A)
    bevel(body, 6.0, segments=10)
    assign(body, teal)
    try:
        bpy.ops.object.select_all(action="DESELECT")
        body.select_set(True)
        bpy.context.view_layer.objects.active = body
        bpy.ops.object.shade_auto_smooth(angle=0.61)
    except Exception:
        pass

    # ---- right face: circular speaker plate with hex grille ----
    plate_r = 44.0
    plate = add_cyl("SpeakerPlate", plate_r, 2.6, loc=(half + mm(0.6), 0, 0),
                    rot=(0, math.radians(90), 0), verts=96)
    bevel(plate, 1.2, segments=3, angle_limit=40)
    assign(plate, cream)
    gr, pitch = 38.0, 5.8
    cutters = []
    row, v = 0, -gr
    while v <= gr:
        yoff = (pitch / 2) if (row % 2) else 0.0
        u = -gr + yoff
        while u <= gr:
            if math.hypot(u, v) <= gr - 2.0:
                c = add_cyl("h", 1.7, 26, loc=(half + mm(0.6), mm(u), mm(v)),
                            rot=(0, math.radians(90), 0), verts=12)
                cutters.append(c)
            u += pitch
        v += pitch * 0.866
        row += 1
    bpy.ops.object.select_all(action="DESELECT")
    for c in cutters:
        c.select_set(True)
    bpy.context.view_layer.objects.active = cutters[0]
    bpy.ops.object.join()
    boolean(plate, bpy.context.object)
    cloth = add_cyl("Cloth", gr + 1.5, 1.2, loc=(half - mm(1.4), 0, 0),
                    rot=(0, math.radians(90), 0))
    assign(cloth, dark)
    ring = add_cyl("Ring", plate_r + 2.4, 2.0, loc=(half + mm(0.2), 0, 0),
                   rot=(0, math.radians(90), 0))
    ri = add_cyl("ri", plate_r + 0.6, 9, loc=(half + mm(0.2), 0, 0),
                 rot=(0, math.radians(90), 0))
    boolean(ring, ri)
    assign(ring, brass)

    # ---- front face: portrait screen ----
    sw, sh = 38.5, 51.0
    scz = 4.0
    face_y = -half
    win = add_box("win", sw, 8, sh, loc=(0, face_y + mm(1.5), mm(scz)))
    boolean(body, win)
    glass = add_box("Screen", sw + 3, 1.2, sh + 3, loc=(0, face_y + mm(1.2), mm(scz)))
    assign(glass, dark)
    bez = add_box("Bezel", sw + 6.5, 1.6, sh + 6.5, loc=(0, face_y - mm(0.2), mm(scz)))
    bezc = add_box("bc", sw + 0.5, 9, sh + 0.5, loc=(0, face_y - mm(0.2), mm(scz)))
    boolean(bez, bezc)
    assign(bez, brass)

    # ---- top: knurled brass knob, centered ----
    knob = add_cyl("Knob", 14.0, 13.0, loc=(0, 0, half + mm(4.5)), verts=48)
    bevel(knob, 2.2, segments=4, angle_limit=40)
    assign(knob, brass)
    for i in range(18):
        a = i * (2 * math.pi / 18)
        g = add_cyl("g", 0.9, 22, loc=(math.cos(a) * mm(14), math.sin(a) * mm(14), half + mm(4.5)), verts=10)
        boolean(knob, g)
    dot = add_cyl("Dot", 1.4, 2.0, loc=(0, -mm(14) * 0.62, half + mm(11.2)), verts=12)
    assign(dot, dark)

    # ---- feet ----
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        f = add_cyl("Foot", 6, 3.5, loc=(sx * (half - mm(20)), sy * (half - mm(20)), -half - mm(1.2)))
        assign(f, material("Foot", (0.02, 0.02, 0.02), rough=0.8))

    # ---- studio ----
    bpy.ops.mesh.primitive_plane_add(size=2.5, location=(0, 0.5, -half - mm(4)))
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

    bpy.ops.object.camera_add(location=(mm(210), mm(-250), mm(120)),
                              rotation=(math.radians(70), 0, math.radians(40)))
    cam = bpy.context.object
    cam.data.lens = 62
    bpy.context.scene.camera = cam

    def light(name, loc, energy, size, rot):
        bpy.ops.object.light_add(type="AREA", location=loc, rotation=rot)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        L.data.size = size

    light("Key", (mm(-250), mm(-260), mm(260)), 45, 0.7, (math.radians(50), 0, math.radians(-40)))
    light("Fill", (mm(300), mm(-200), mm(120)), 22, 0.9, (math.radians(65), 0, math.radians(50)))
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


if __name__ == "__main__":
    build()
    render(f"{OUT_DIR}/cube.png")
