# wrap_cube.py — World Radio in the Sony TR-1825 cube language, v3.
#
# Two thin U-shells on a SHARED outer envelope (every face flush, parting lines
# are 0.8mm shadow seams), wrapping a black core:
#   cream U:   front -> bottom -> back
#   mustard U: left  -> top    -> right, with a fold-down front/back lip
# Black does the work: a slim shaded channel under the front lip carries a
# mostly-buried brass thumbwheel; a rounded window in the mustard right leg
# reveals a black perforated grille zone on the core.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python wrap_cube.py

import bpy
import bmesh
import math

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- master dimensions (mm, absolute; origin at core center) ----
CORE = 55.0      # core half-edge
T = 4.0          # shell thickness
OUT = CORE + T   # shared outer envelope: everything's outer face lands on ±59
SEAM = 0.8       # shadow gap where shells butt
LEG_TOP = 36.0   # cream front/back legs rise to here
LIP_BOT = 48.0   # mustard fold-down lip reaches down to here (channel 36..48)


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


def hex_holes(region_y0, region_y1, region_z0, region_z1, hole_r, pitch, x_center, margin=4.0):
    """Joined hex-packed cylinder cutters (axis X) filling a rectangular region."""
    cutters = []
    row = 0
    z = region_z0 + margin
    while z <= region_z1 - margin:
        yoff = (pitch / 2) if (row % 2) else 0.0
        y = region_y0 + margin + yoff
        while y <= region_y1 - margin:
            c = add_cyl("h", hole_r, 10, loc=(mm(x_center), mm(y), mm(z)),
                        rot=(0, math.radians(90), 0), verts=12)
            cutters.append(c)
            y += pitch
        z += pitch * 0.866
        row += 1
    bpy.ops.object.select_all(action="DESELECT")
    for c in cutters:
        c.select_set(True)
    bpy.context.view_layer.objects.active = cutters[0]
    bpy.ops.object.join()
    return bpy.context.object


def build():
    clean_scene()

    black = material("Core", (0.004, 0.004, 0.005), rough=0.62)
    cream = material("Cream", (0.902, 0.868, 0.792), rough=0.5)
    mustard = material("Mustard", (0.55, 0.33, 0.02), rough=0.45)
    dark = material("Dark", (0.003, 0.003, 0.004), rough=0.45)
    brass = material("Brass", (0.85, 0.64, 0.29), rough=0.28, metallic=1.0)

    # ---- black core ----
    core = boxmm("Core", -CORE, CORE, -CORE, CORE, -CORE, CORE)

    # grille perforation on the core's right-front zone (revealed by the shell window)
    holes = hex_holes(-40.0, -8.0, -36.0, 24.0, 1.8, 6.0, CORE - 2.0)
    boolean(core, holes)
    # wheel pocket in the core's top-front edge
    pocket = boxmm("pocket", 8.0, 40.0, -CORE - 1, -CORE + 5, LEG_TOP + 0.5, LIP_BOT - 0.5)
    boolean(core, pocket)
    bevel(core, 1.0, segments=3)
    assign(core, black)

    # ---- cream U: front / bottom / back — outer faces all on the envelope ----
    cfront = boxmm("CreamShell", -OUT, OUT, -OUT, -CORE, -OUT, LEG_TOP)
    cbot = boxmm("cb", -OUT, OUT, -OUT, OUT, -OUT, -CORE)
    cback = boxmm("cb2", -OUT, OUT, CORE, OUT, -OUT, LEG_TOP)
    union(cfront, cbot)
    union(cfront, cback)

    # screen aperture: shallow step recess + through window, glass on the core
    sw2, sh2 = 38.5 / 2, 51.0 / 2
    scz = -12.0
    step = boxmm("step", -sw2 - 2, sw2 + 2, -OUT - 1, -OUT + 1.6, scz - sh2 - 2, scz + sh2 + 2)
    boolean(cfront, step)
    win = boxmm("win", -sw2, sw2, -OUT - 2, -CORE + 1, scz - sh2, scz + sh2)
    boolean(cfront, win)
    bevel(cfront, 2.5)
    assign(cfront, cream)
    glass = boxmm("Screen", -sw2 - 1.5, sw2 + 1.5, -CORE - 0.7, -CORE + 0.5,
                  scz - sh2 - 1.5, scz + sh2 + 1.5)
    assign(glass, dark)

    # ---- mustard U: left / top / right + fold-down lips, flush on the envelope ----
    mtop = boxmm("MustardShell", -OUT, OUT, -OUT, OUT, CORE, OUT)
    lip_f = boxmm("lipf", -OUT, OUT, -OUT, -CORE, LIP_BOT, CORE)
    lip_b = boxmm("lipb", -OUT, OUT, CORE, OUT, LIP_BOT, CORE)
    mleft = boxmm("ml", -OUT, -CORE, -CORE + SEAM, CORE - SEAM, -CORE + SEAM, CORE)
    mright = boxmm("mr", CORE, OUT, -CORE + SEAM, CORE - SEAM, -CORE + SEAM, CORE)
    union(mtop, lip_f)
    union(mtop, lip_b)
    union(mtop, mleft)
    union(mtop, mright)

    # rounded window in the right leg exposing the black grille zone
    gwin = boxmm("gwin", CORE - 1, OUT + 2, -42.0, -6.0, -38.0, 26.0)
    bevel(gwin, 5.0, segments=5)
    boolean(mtop, gwin)

    bevel(mtop, 2.5)
    assign(mtop, mustard)

    # ---- thin brass thumbwheel, mostly buried in the channel ----
    kz = (LEG_TOP + LIP_BOT) / 2  # 42
    wheel = add_cyl("Wheel", 6.0, 24.0, loc=(mm(24), mm(-CORE + 1.0), mm(kz)),
                    rot=(0, math.radians(90), 0), verts=48)
    bevel(wheel, 0.8, segments=3, angle_limit=40)
    assign(wheel, brass)
    for i in range(18):
        a = i * (2 * math.pi / 18)
        g = add_cyl("g", 0.5, 30,
                    loc=(mm(24), mm(-CORE + 1.0) + math.cos(a) * mm(6), mm(kz) + math.sin(a) * mm(6)),
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

    # lower, more eye-level camera than before
    bpy.ops.object.camera_add(location=(mm(250), mm(-290), mm(120)),
                              rotation=(math.radians(74), 0, math.radians(40)))
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
