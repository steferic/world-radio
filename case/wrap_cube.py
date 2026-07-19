# wrap_cube.py — World Radio in the Sony TR-1825 cube language, v4.
#
# A smoothly-rounded black core cube with two THIN (2mm) wrap ribbons that each
# cover 3 faces and are NARROWER than the cube, so black core shows at every
# edge and corner:
#   cream ribbon:   front -> bottom -> back   (|x| <= RIBBON_HALF)
#   mustard ribbon: left  -> top    -> right  (|y| <= RIBBON_HALF)
# The ribbons are built as (rounded outer shell − rounded inner shell) ∩ band,
# which guarantees uniform thickness, matched rounded bends, and zero overlap.
# Black does the work: thumbwheel on the bare top-front strip, grille window in
# the mustard right leg revealing perforated core, screen in the cream front.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python wrap_cube.py

import bpy
import bmesh
import math

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

# ---- master dimensions (mm, absolute; origin at core center) ----
CORE = 55.0        # core half-edge
CORE_R = 7.0       # core corner rounding
T = 2.0            # ribbon thickness (thin!)
CLEAR = 0.3        # ribbon-to-core clearance
RIBBON_HALF = 48.0 # ribbon half-width = full flat face width (black shows on the edge arcs)
CREAM_TOP = 48.0   # cream legs run to the top edge arc of their faces
MUST_BOT = -48.0   # mustard legs run to the bottom edge arc


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


def intersect(target, region):
    _bool(target, region, "INTERSECT")


def bevel(ob, width_mm, segments=8, angle_limit=60):
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


def rounded_cube(name, half, r):
    ob = boxmm(name, -half, half, -half, half, -half, half)
    bevel(ob, r, segments=10)
    return ob


def make_ribbon(name, band_axis):
    """Uniform 2mm shell over the rounded core, limited to a 3-face band.
    band_axis 'x': |x|<=RIBBON_HALF band wrapping front/bottom/back is made by
    the caller's region box; here we just produce outer-minus-inner."""
    outer = rounded_cube(name, CORE + CLEAR + T, CORE_R + CLEAR + T)
    inner = rounded_cube("inner", CORE + CLEAR, CORE_R + CLEAR)
    boolean(outer, inner)
    return outer


def build():
    clean_scene()

    black = material("Core", (0.004, 0.004, 0.005), rough=0.6)
    cream = material("Cream", (0.902, 0.868, 0.792), rough=0.5)
    mustard = material("Mustard", (0.55, 0.33, 0.02), rough=0.45)
    dark = material("Dark", (0.003, 0.003, 0.004), rough=0.45)
    brass = material("Brass", (0.85, 0.64, 0.29), rough=0.28, metallic=1.0)

    OUTER = CORE + CLEAR + T  # 57.3 outer envelope

    # ---- smoothly rounded black core ----
    core = rounded_cube("Core", CORE, CORE_R)
    smooth(core)
    assign(core, black)

    # ---- cream ribbon: front -> bottom -> back ----
    cream_rib = make_ribbon("CreamRibbon", "x")
    region = boxmm("reg", -RIBBON_HALF, RIBBON_HALF, -OUTER - 2, OUTER + 2, -OUTER - 2, CREAM_TOP)
    intersect(cream_rib, region)
    # screen aperture through the front leg
    sw2, sh2 = 38.5 / 2, 51.0 / 2
    scz = -10.0
    win = boxmm("win", -sw2, sw2, -OUTER - 2, -CORE + 1, scz - sh2, scz + sh2)
    boolean(cream_rib, win)
    # slot revealing the black core + brass thumbwheel
    slot = boxmm("slot", 2.0, 38.0, -OUTER - 2, -CORE + 1, 33.0, 47.0)
    boolean(cream_rib, slot)
    bevel(cream_rib, 0.7, segments=4)
    smooth(cream_rib)
    assign(cream_rib, cream)
    glass = boxmm("Screen", -sw2 - 1.5, sw2 + 1.5, -CORE - 0.6, -CORE + 0.5,
                  scz - sh2 - 1.5, scz + sh2 + 1.5)
    assign(glass, dark)

    # ---- mustard ribbon: left -> top -> right ----
    must_rib = make_ribbon("MustardRibbon", "y")
    region2 = boxmm("reg2", -OUTER - 2, OUTER + 2, -RIBBON_HALF, RIBBON_HALF, MUST_BOT, OUTER + 2)
    intersect(must_rib, region2)
    # speaker grille: perforate the ENTIRE right leg (holes through the shell)
    gh = []
    pitch = 6.0
    row = 0
    z = -44.0
    while z <= 44.0:
        yoff = (pitch / 2) if (row % 2) else 0.0
        y = -44.0 + yoff
        while y <= 44.0:
            if abs(y) <= 44.0 and MUST_BOT + 4 <= z <= 44.0:
                c = add_cyl("h", 1.8, 8, loc=(mm(CORE + CLEAR + T / 2), mm(y), mm(z)),
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
    boolean(must_rib, bpy.context.object)
    bevel(must_rib, 0.7, segments=4)
    smooth(must_rib)
    assign(must_rib, mustard)

    # ---- thin brass thumbwheel on the bare top-front strip ----
    kz = 40.0
    wheel = add_cyl("Wheel", 5.5, 20.0, loc=(mm(20), mm(-CORE + 1.5), mm(kz)),
                    rot=(0, math.radians(90), 0), verts=48)
    bevel(wheel, 0.7, segments=3, angle_limit=40)
    assign(wheel, brass)
    for i in range(16):
        a = i * (2 * math.pi / 16)
        g = add_cyl("g", 0.45, 26,
                    loc=(mm(20), mm(-CORE + 1.5) + math.cos(a) * mm(5.5), mm(kz) + math.sin(a) * mm(5.5)),
                    rot=(0, math.radians(90), 0), verts=8)
        boolean(wheel, g)
    # pocket so the wheel reads seated in the core
    pocket = boxmm("pocket", 4.0, 36.0, -CORE - 1, -CORE + 4, 33.5, 46.5)
    boolean(core, pocket)

    # ---- studio ----
    floor_z = -OUTER
    bpy.ops.mesh.primitive_plane_add(size=3.0, location=(0, 0.6, mm(floor_z) - 0.0005))
    bg = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(bg.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=24, use_grid_fill=True)
    for v2 in bm.verts:
        if v2.co.y > 0.3:
            v2.co.z += (v2.co.y - 0.3) ** 2 * 1.8
    bm.to_mesh(bg.data)
    bg.data.update()
    assign(bg, material("Backdrop", (0.82, 0.79, 0.75), rough=0.9))

    bpy.ops.object.camera_add(location=(mm(245), mm(-285), mm(115)),
                              rotation=(math.radians(75), 0, math.radians(40)))
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
