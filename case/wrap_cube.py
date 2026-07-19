# wrap_cube.py — World Radio in the Sony TR-1825 cube language:
# a black core cube with two interlocking wrap shells (mustard: top+back,
# cream: front+bottom). The black core shows through as recessed channels —
# the top-front channel carries a brass thumbwheel tuner, the side faces are
# framed black panels, the right one perforated as the speaker grille.
#
# All geometry is specified in ABSOLUTE mm coordinates via boxmm(x0,x1,y0,y1,z0,z1)
# — no center/offset arithmetic.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python wrap_cube.py

import bpy
import bmesh
import math

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- master dimensions (mm, absolute; origin at core center) ----
CORE = 55.0      # core half-edge: core spans [-55, 55] in x/y/z
T = 4.0          # shell thickness (thin, like bent plastic)
OUT = CORE + T   # outer plane of a shell leg (59)
CREAM_W = 57.0   # cream U half-width in x (slightly proud of core sides)
LEG_TOP = 34.0   # top edge of the cream front/back legs
MLEG_BOT = -40.0 # bottom edge of the mustard side legs (black reveal below)


def mm(v):
    return v * S


def clean_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def boxmm(name, x0, x1, y0, y1, z0, z1):
    """Box from absolute mm bounds."""
    cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2
    bpy.ops.mesh.primitive_cube_add(size=1, location=(mm(cx), mm(cy), mm(cz)))
    ob = bpy.context.object
    ob.name = name
    ob.scale = (mm(x1 - x0), mm(y1 - y0), mm(z1 - z0))
    bpy.ops.object.transform_apply(scale=True)
    return ob


def add_cyl(name, r, depth, loc=(0, 0, 0), rot=(0, 0, 0), verts=64):
    bpy.ops.mesh.primitive_cylinder_add(radius=mm(r), depth=mm(depth),
                                        location=loc, rotation=rot, vertices=verts)
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


def union(target, other):
    mod = target.modifiers.new(name="bool", type="BOOLEAN")
    mod.operation = "UNION"
    mod.object = other
    mod.solver = "EXACT"
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(other, do_unlink=True)


def bevel(ob, width_mm, segments=4, angle_limit=60):
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

    black = material("Core", (0.004, 0.004, 0.005), rough=0.62)
    cream = material("Cream", (0.902, 0.868, 0.792), rough=0.5)
    mustard = material("Mustard", (0.55, 0.33, 0.02), rough=0.45)
    dark = material("Dark", (0.004, 0.004, 0.005), rough=0.55)
    brass = material("Brass", (0.85, 0.64, 0.29), rough=0.28, metallic=1.0)

    # ---- black core cube ----
    core = boxmm("Core", -CORE, CORE, -CORE, CORE, -CORE, CORE)
    bevel(core, 1.0)
    assign(core, black)

    # ---- cream U: front -> bottom -> back (3 faces, rounded bends) ----
    frontleg = boxmm("CreamShell", -CREAM_W, CREAM_W, -OUT, -CORE, -OUT, LEG_TOP)
    bottomleg = boxmm("cb", -CREAM_W, CREAM_W, -OUT, OUT, -OUT, -CORE)
    backleg = boxmm("cb2", -CREAM_W, CREAM_W, CORE, OUT, -OUT, LEG_TOP)
    union(frontleg, bottomleg)
    union(frontleg, backleg)
    bevel(frontleg, 2.0, segments=6)
    assign(frontleg, cream)
    front = frontleg

    # screen window through the cream front leg; dark glass against the core
    sw2, sh2 = 38.5 / 2, 51.0 / 2
    scz = -12.0
    win = boxmm("win", -sw2, sw2, -OUT - 2, -CORE + 1, scz - sh2, scz + sh2)
    boolean(front, win)
    glass = boxmm("Screen", -sw2 - 1.5, sw2 + 1.5, -CORE - 0.9, -CORE + 0.5,
                  scz - sh2 - 1.5, scz + sh2 + 1.5)
    assign(glass, dark)

    # ---- mustard U: left -> top -> right (3 faces, rounded bends) ----
    topleg = boxmm("MustardShell", -OUT, OUT, -CREAM_W, CREAM_W, CORE, OUT)
    leftleg = boxmm("ml", -OUT, -CORE, -CREAM_W, CREAM_W, MLEG_BOT, CORE)
    rightleg = boxmm("mr", CORE, OUT, -CREAM_W, CREAM_W, MLEG_BOT, CORE)
    union(topleg, leftleg)
    union(topleg, rightleg)

    # speaker grille: perforate the mustard right leg (black core shows through)
    gr, pitch = 24.0, 6.2
    cutters = []
    row, v = 0, -gr
    while v <= gr:
        yoff = (pitch / 2) if (row % 2) else 0.0
        u = -gr + yoff
        while u <= gr:
            if math.hypot(u, v) <= gr - 1.5:
                c = add_cyl("h", 1.9, 14, loc=(mm(CORE + T / 2), mm(u), mm(v + 2)),
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
    boolean(topleg, bpy.context.object)

    bevel(topleg, 2.0, segments=6)
    assign(topleg, mustard)

    # ---- brass thumbwheel in the top-front black channel [FRONT_TOP, TOP_UNDER] ----
    kz = (LEG_TOP + CORE) / 2
    wheel = add_cyl("Wheel", 9.0, 28.0, loc=(mm(24), mm(-CORE + 3), mm(kz)),
                    rot=(0, math.radians(90), 0), verts=48)
    bevel(wheel, 1.3, segments=3, angle_limit=40)
    assign(wheel, brass)
    for i in range(20):
        a = i * (2 * math.pi / 20)
        g = add_cyl("g", 0.7, 34,
                    loc=(mm(24), mm(-CORE + 3) + math.cos(a) * mm(9), mm(kz) + math.sin(a) * mm(9)),
                    rot=(0, math.radians(90), 0), verts=8)
        boolean(wheel, g)

    # ---- studio ----
    floor_z = -OUT
    bpy.ops.mesh.primitive_plane_add(size=2.5, location=(0, 0.5, mm(floor_z) - 0.0005))
    bg = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(bg.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=24, use_grid_fill=True)
    for v2 in bm.verts:
        if v2.co.y > 0.25:
            v2.co.z += (v2.co.y - 0.25) ** 2 * 2.2
    bm.to_mesh(bg.data)
    bg.data.update()
    assign(bg, material("Backdrop", (0.82, 0.79, 0.75), rough=0.9))

    bpy.ops.object.camera_add(location=(mm(265), mm(-300), mm(175)),
                              rotation=(math.radians(68), 0, math.radians(41)))
    cam = bpy.context.object
    cam.data.lens = 58
    bpy.context.scene.camera = cam

    def light(name, loc, energy, size, rot):
        bpy.ops.object.light_add(type="AREA", location=loc, rotation=rot)
        L = bpy.context.object
        L.name = name
        L.data.energy = energy
        L.data.size = size

    light("Key", (mm(-250), mm(-260), mm(260)), 45, 0.7, (math.radians(50), 0, math.radians(-40)))
    light("Fill", (mm(300), mm(-200), mm(120)), 24, 0.9, (math.radians(65), 0, math.radians(50)))
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
    render(f"{OUT_DIR}/wrap_cube.png")
