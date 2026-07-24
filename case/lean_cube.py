# lean_cube.py — the wrap cube, reclined 15° like a picture frame.
#
# Pure shell study: rounded black core + the two thin wrap ribbons (no screen,
# knobs, or grille yet). The whole cube leans back on a wedge foot molded into
# the cream bottom ribbon, so a desk user looking down sees the front face
# head-on. The cube stays a cube — it just sits back.
#
#   /Applications/Blender.app/Contents/MacOS/Blender --background --python lean_cube.py

import bpy
import bmesh
import math
from mathutils import Matrix, Vector

OUT_DIR = "/Users/stefanlenoach/Code/world-radio/case"
S = 0.001

CORE = 55.0
CORE_R = 7.0
T = 2.0
CLEAR = 0.3
RIBBON_HALF = 48.0
CREAM_TOP = 48.0
MUST_BOT = -48.0
LEAN = math.radians(15)  # recline angle


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


def make_ribbon(name):
    outer = rounded_cube(name, CORE + CLEAR + T, CORE_R + CLEAR + T)
    inner = rounded_cube("inner", CORE + CLEAR, CORE_R + CLEAR)
    boolean(outer, inner)
    return outer


def build():
    clean_scene()

    black = material("Core", (0.004, 0.004, 0.005), rough=0.6)
    cream = material("Cream", (0.902, 0.868, 0.792), rough=0.5)
    mustard = material("Mustard", (0.55, 0.33, 0.02), rough=0.45)

    OUTER = CORE + CLEAR + T  # 57.3

    core = rounded_cube("Core", CORE, CORE_R)
    smooth(core)
    assign(core, black)

    cream_rib = make_ribbon("CreamRibbon")
    region = boxmm("reg", -RIBBON_HALF, RIBBON_HALF, -OUTER - 2, OUTER + 2, -OUTER - 2, CREAM_TOP)
    intersect(cream_rib, region)
    bevel(cream_rib, 0.7, segments=4)
    smooth(cream_rib)
    assign(cream_rib, cream)

    must_rib = make_ribbon("MustardRibbon")
    region2 = boxmm("reg2", -OUTER - 2, OUTER + 2, -RIBBON_HALF, RIBBON_HALF, MUST_BOT, OUTER + 2)
    intersect(must_rib, region2)
    bevel(must_rib, 0.7, segments=4)
    smooth(must_rib)
    assign(must_rib, mustard)

    # ---- recline the whole cube about its bottom-front edge ----
    pivot = Vector((0, mm(-OUTER), mm(-OUTER)))
    R = (Matrix.Translation(pivot)
         @ Matrix.Rotation(-LEAN, 4, "X")
         @ Matrix.Translation(-pivot))
    for ob in list(bpy.data.objects):
        if ob.type == "MESH":
            ob.matrix_world = R @ ob.matrix_world

    # ---- wedge foot molded into the cream bottom ribbon ----
    # fills between the floor and the tilted underside, same width as the ribbon
    yb = -OUTER + 2 * OUTER * math.cos(LEAN)  # back bottom corner y after tilt
    bm = bmesh.new()
    cross = [
        (-OUTER + 8.0, -OUTER),                                   # near the front edge, on the floor
        (yb - 6.0, -OUTER),                                       # back, on the floor
        (yb - 6.0, -OUTER + (yb - 6.0 + OUTER) * math.tan(LEAN) + 1.0),  # up to the tilted underside (+1 embed)
    ]
    rings = []
    for x in (-RIBBON_HALF + 2, RIBBON_HALF - 2):
        rings.append([bm.verts.new((mm(x), mm(y), mm(z))) for (y, z) in cross])
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[1])))
    for i in range(3):
        a, b = rings[0][i], rings[0][(i + 1) % 3]
        c, d = rings[1][(i + 1) % 3], rings[1][i]
        bm.faces.new((a, b, c, d))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new("Foot")
    bm.to_mesh(me)
    foot = bpy.data.objects.new("Foot", me)
    bpy.context.collection.objects.link(foot)
    bpy.context.view_layer.objects.active = foot
    foot.select_set(True)
    bevel(foot, 1.5, segments=4)
    smooth(foot)
    assign(foot, cream)

    # ---- studio ----
    floor_z = -OUTER
    bpy.ops.mesh.primitive_plane_add(size=3.0, location=(0, 0.6, mm(floor_z) - 0.0005))
    bg = bpy.context.object
    bm2 = bmesh.new()
    bm2.from_mesh(bg.data)
    bmesh.ops.subdivide_edges(bm2, edges=bm2.edges[:], cuts=24, use_grid_fill=True)
    for v2 in bm2.verts:
        if v2.co.y > 0.3:
            v2.co.z += (v2.co.y - 0.3) ** 2 * 1.8
    bm2.to_mesh(bg.data)
    bg.data.update()
    assign(bg, material("Backdrop", (0.82, 0.79, 0.75), rough=0.9))

    # slightly high camera — the desk user's point of view, where the lean pays off
    bpy.ops.object.camera_add(location=(mm(300), mm(-330), mm(105)),
                              rotation=(math.radians(78), 0, math.radians(42)))
    cam = bpy.context.object
    cam.data.lens = 55
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
    render(f"{OUT_DIR}/lean_cube.png")
