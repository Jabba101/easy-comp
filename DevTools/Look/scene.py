"""
A photoreal test render of one control from the "field case" design:
olive-drab crinkle-finish panel with worn paint, printed scale, a black
bakelite chicken-head knob, a green readout window and two slotted screws.

Run: blender -b -P scene.py -- --res 640 480 --samples 32 --angle 24.75 --out /path/frame.png
"""
import math
import sys

import bpy

args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []


def arg(name, count=1, default=None, cast=float):
    if name in args:
        i = args.index(name)
        vals = [cast(v) for v in args[i + 1:i + 1 + count]]
        return vals if count > 1 else vals[0]
    return default


RES = arg('--res', 2, [640, 480], int)
SAMPLES = arg('--samples', 1, 32, int)
ANGLE = arg('--angle', 1, 24.75)          # knob pointer, degrees clockwise from straight up
OUT = arg('--out', 1, '/home/claude/render/out.png', str)
SEED = arg('--seed', 1, 7, int)

FONT_LABEL = '/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf'
FONT_DIGITS = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'

# ---------------------------------------------------------------- reset
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'


def srgb(hex_colour):
    h = hex_colour.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple(((v + 0.055) / 1.055) ** 2.4 if v > 0.04045 else v / 12.92 for v in c) + (1.0,)


def new_material(name):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    return mat, mat.node_tree.nodes, mat.node_tree.links


def node(nodes, kind, **inputs):
    n = nodes.new(kind)
    for key, value in inputs.items():
        n.inputs[key].default_value = value
    return n


def principled(nodes):
    return nodes['Principled BSDF']


def smooth(obj, angle=40):
    if obj.type == 'MESH':
        for p in obj.data.polygons:
            p.use_smooth = True
        if hasattr(obj.data, 'use_auto_smooth'):
            obj.data.use_auto_smooth = True
            obj.data.auto_smooth_angle = math.radians(angle)


def bevel(obj, width, segments=3):
    m = obj.modifiers.new('Bevel', 'BEVEL')
    m.width = width
    m.segments = segments
    m.limit_method = 'ANGLE'
    return m


# ---------------------------------------------------------------- materials
def panel_material():
    mat, nodes, links = new_material('OliveDrabCrinkle')
    bsdf = principled(nodes)
    coord = nodes.new('ShaderNodeTexCoord')

    # wrinkle-finish relief: fine ridged noise, slightly warped
    warp = node(nodes, 'ShaderNodeTexNoise', Scale=600.0, Detail=2.0)
    links.new(coord.outputs['Object'], warp.inputs['Vector'])
    warped = nodes.new('ShaderNodeMixRGB')
    warped.inputs['Fac'].default_value = 0.0012
    warped.blend_type = 'ADD'
    links.new(coord.outputs['Object'], warped.inputs['Color1'])
    links.new(warp.outputs['Color'], warped.inputs['Color2'])
    crinkle = nodes.new('ShaderNodeTexMusgrave')
    crinkle.musgrave_type = 'RIDGED_MULTIFRACTAL'
    crinkle.inputs['Scale'].default_value = 1800.0
    crinkle.inputs['Detail'].default_value = 3.0
    crinkle.inputs['Dimension'].default_value = 1.1
    crinkle.inputs['Offset'].default_value = 0.9
    crinkle.inputs['Gain'].default_value = 1.6
    links.new(warped.outputs['Color'], crinkle.inputs['Vector'])
    height = node(nodes, 'ShaderNodeMath')
    height.operation = 'MULTIPLY'
    links.new(crinkle.outputs['Fac'], height.inputs[0])
    height.inputs[1].default_value = 0.5

    # wear: chips through to dark primer, and the odd spot of bare metal
    wear = node(nodes, 'ShaderNodeTexNoise', Scale=55.0, Detail=12.0, Roughness=0.68, Distortion=0.2)
    links.new(coord.outputs['Object'], wear.inputs['Vector'])
    chip = node(nodes, 'ShaderNodeMapRange', **{'From Min': 0.645, 'From Max': 0.668, 'To Min': 0.0, 'To Max': 1.0})
    links.new(wear.outputs['Fac'], chip.inputs['Value'])
    metal = node(nodes, 'ShaderNodeMapRange', **{'From Min': 0.715, 'From Max': 0.73, 'To Min': 0.0, 'To Max': 1.0})
    links.new(wear.outputs['Fac'], metal.inputs['Value'])

    # fine scratches: very stretched noise
    scratch_map = nodes.new('ShaderNodeMapping')
    scratch_map.inputs['Scale'].default_value = (14.0, 900.0, 1.0)
    scratch_map.inputs['Rotation'].default_value = (0.0, 0.0, math.radians(18))
    links.new(coord.outputs['Object'], scratch_map.inputs['Vector'])
    scratch = node(nodes, 'ShaderNodeTexNoise', Scale=1.0, Detail=2.0, Roughness=0.5)
    links.new(scratch_map.outputs['Vector'], scratch.inputs['Vector'])
    scratch_mask = node(nodes, 'ShaderNodeMapRange', **{'From Min': 0.745, 'From Max': 0.775, 'To Min': 0.0, 'To Max': 0.32})
    links.new(scratch.outputs['Fac'], scratch_mask.inputs['Value'])

    # colour: olive drab with a little blotchiness
    blotch = node(nodes, 'ShaderNodeTexNoise', Scale=18.0, Detail=4.0, Roughness=0.5)
    links.new(coord.outputs['Object'], blotch.inputs['Vector'])
    paint = nodes.new('ShaderNodeMixRGB')
    paint.inputs['Color1'].default_value = srgb('#3f4824')
    paint.inputs['Color2'].default_value = srgb('#525c30')
    links.new(blotch.outputs['Fac'], paint.inputs['Fac'])
    primer = nodes.new('ShaderNodeMixRGB')
    links.new(chip.outputs['Result'], primer.inputs['Fac'])
    links.new(paint.outputs['Color'], primer.inputs['Color1'])
    primer.inputs['Color2'].default_value = srgb('#2c3022')
    bare = nodes.new('ShaderNodeMixRGB')
    links.new(metal.outputs['Result'], bare.inputs['Fac'])
    links.new(primer.outputs['Color'], bare.inputs['Color1'])
    bare.inputs['Color2'].default_value = srgb('#9c9d97')
    scratched = nodes.new('ShaderNodeMixRGB')
    links.new(scratch_mask.outputs['Result'], scratched.inputs['Fac'])
    links.new(bare.outputs['Color'], scratched.inputs['Color1'])
    scratched.inputs['Color2'].default_value = srgb('#7f8460')

    # grime in the corners (around the knob, screws and window)
    ao = nodes.new('ShaderNodeAmbientOcclusion')
    ao.samples = 12
    ao.inputs['Distance'].default_value = 0.004
    grime = node(nodes, 'ShaderNodeMapRange', **{'From Min': 0.0, 'From Max': 1.0, 'To Min': 0.45, 'To Max': 1.0})
    links.new(ao.outputs['AO'], grime.inputs['Value'])
    dirty = nodes.new('ShaderNodeMixRGB')
    dirty.blend_type = 'MULTIPLY'
    dirty.inputs['Fac'].default_value = 1.0
    links.new(scratched.outputs['Color'], dirty.inputs['Color1'])
    links.new(grime.outputs['Result'], dirty.inputs['Color2'])
    cavity = node(nodes, 'ShaderNodeMapRange', **{'From Min': 0.0, 'From Max': 0.9, 'To Min': 0.62, 'To Max': 1.08})
    links.new(crinkle.outputs['Fac'], cavity.inputs['Value'])
    valleys = nodes.new('ShaderNodeMixRGB')
    valleys.blend_type = 'MULTIPLY'
    valleys.inputs['Fac'].default_value = 1.0
    links.new(dirty.outputs['Color'], valleys.inputs['Color1'])
    links.new(cavity.outputs['Result'], valleys.inputs['Color2'])
    links.new(valleys.outputs['Color'], bsdf.inputs['Base Color'])

    metallic = node(nodes, 'ShaderNodeMath')
    metallic.operation = 'MULTIPLY'
    links.new(metal.outputs['Result'], metallic.inputs[0])
    metallic.inputs[1].default_value = 0.9
    links.new(metallic.outputs['Value'], bsdf.inputs['Metallic'])
    rough = node(nodes, 'ShaderNodeMapRange', **{'From Min': 0.0, 'From Max': 1.0, 'To Min': 0.72, 'To Max': 0.38})
    links.new(metal.outputs['Result'], rough.inputs['Value'])
    links.new(rough.outputs['Result'], bsdf.inputs['Roughness'])
    bsdf.inputs['Specular IOR Level'].default_value = 0.45

    bump = node(nodes, 'ShaderNodeBump', Strength=0.62, Distance=0.00014)
    links.new(height.outputs['Value'], bump.inputs['Height'])
    chip_edge = node(nodes, 'ShaderNodeBump', Strength=0.35, Distance=0.0002)
    links.new(chip.outputs['Result'], chip_edge.inputs['Height'])
    links.new(bump.outputs['Normal'], chip_edge.inputs['Normal'])
    links.new(chip_edge.outputs['Normal'], bsdf.inputs['Normal'])
    return mat


def ink_material():
    """Cream screen-printed paint, a little worn."""
    mat, nodes, links = new_material('CreamInk')
    bsdf = principled(nodes)
    coord = nodes.new('ShaderNodeTexCoord')
    wear = node(nodes, 'ShaderNodeTexNoise', Scale=700.0, Detail=6.0, Roughness=0.6)
    links.new(coord.outputs['Object'], wear.inputs['Vector'])
    patch = node(nodes, 'ShaderNodeMapRange', **{'From Min': 0.58, 'From Max': 0.66, 'To Min': 0.0, 'To Max': 0.65})
    links.new(wear.outputs['Fac'], patch.inputs['Value'])
    col = nodes.new('ShaderNodeMixRGB')
    links.new(patch.outputs['Result'], col.inputs['Fac'])
    col.inputs['Color1'].default_value = srgb('#ece1bf')
    col.inputs['Color2'].default_value = srgb('#59633a')
    links.new(col.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = 0.62
    return mat


def bakelite_material():
    mat, nodes, links = new_material('Bakelite')
    bsdf = principled(nodes)
    coord = nodes.new('ShaderNodeTexCoord')
    smudge = node(nodes, 'ShaderNodeTexNoise', Scale=260.0, Detail=5.0, Roughness=0.6)
    links.new(coord.outputs['Object'], smudge.inputs['Vector'])
    rough = node(nodes, 'ShaderNodeMapRange', **{'From Min': 0.3, 'From Max': 0.7, 'To Min': 0.12, 'To Max': 0.28})
    links.new(smudge.outputs['Fac'], rough.inputs['Value'])
    links.new(rough.outputs['Result'], bsdf.inputs['Roughness'])
    bsdf.inputs['Base Color'].default_value = srgb('#1d1712')
    bsdf.inputs['Specular IOR Level'].default_value = 0.7
    bsdf.inputs['Coat Weight'].default_value = 0.55
    bsdf.inputs['Coat Roughness'].default_value = 0.06
    micro = node(nodes, 'ShaderNodeTexNoise', Scale=5000.0, Detail=3.0)
    links.new(coord.outputs['Object'], micro.inputs['Vector'])
    bump = node(nodes, 'ShaderNodeBump', Strength=0.05, Distance=0.0001)
    links.new(micro.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])
    return mat


def simple_material(name, colour, roughness, metallic=0.0, spec=0.5):
    mat, nodes, links = new_material(name)
    bsdf = principled(nodes)
    bsdf.inputs['Base Color'].default_value = srgb(colour)
    bsdf.inputs['Roughness'].default_value = roughness
    bsdf.inputs['Metallic'].default_value = metallic
    bsdf.inputs['Specular IOR Level'].default_value = spec
    return mat


def steel_material():
    mat, nodes, links = new_material('Steel')
    bsdf = principled(nodes)
    coord = nodes.new('ShaderNodeTexCoord')
    n = node(nodes, 'ShaderNodeTexNoise', Scale=900.0, Detail=6.0)
    links.new(coord.outputs['Object'], n.inputs['Vector'])
    rough = node(nodes, 'ShaderNodeMapRange', **{'From Min': 0.3, 'From Max': 0.7, 'To Min': 0.22, 'To Max': 0.45})
    links.new(n.outputs['Fac'], rough.inputs['Value'])
    links.new(rough.outputs['Result'], bsdf.inputs['Roughness'])
    ao = nodes.new('ShaderNodeAmbientOcclusion')
    ao.inputs['Distance'].default_value = 0.0015
    tint = nodes.new('ShaderNodeMixRGB')
    links.new(ao.outputs['AO'], tint.inputs['Fac'])
    tint.inputs['Color1'].default_value = srgb('#3a2f22')
    tint.inputs['Color2'].default_value = srgb('#b9b8b2')
    links.new(tint.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Metallic'].default_value = 1.0
    return mat


def glow_material(colour, strength):
    mat, nodes, links = new_material('Glow')
    out = nodes['Material Output']
    nodes.remove(nodes['Principled BSDF'])
    em = node(nodes, 'ShaderNodeEmission', Color=srgb(colour), Strength=strength)
    links.new(em.outputs['Emission'], out.inputs['Surface'])
    return mat


PANEL, INK, BAKELITE = panel_material(), ink_material(), bakelite_material()
STEEL = steel_material()
BEZEL = simple_material('BlackAnodised', '#141513', 0.42, metallic=0.6)
GLASS = simple_material('DarkGlass', '#06100a', 0.04, spec=0.9)
INLAY = simple_material('KnobInlay', '#f1e8cd', 0.35)
PHOSPHOR = glow_material('#7dffa0', 5.0)


# ---------------------------------------------------------------- helpers
def assign(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    return obj


def box(name, size, location, rotation_z=0.0, mat=None, bevel_width=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = size
    obj.rotation_euler.z = rotation_z
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel_width:
        bevel(obj, bevel_width)
    if mat:
        assign(obj, mat)
    return obj


def text(body, font_path, size, location, mat, align='CENTER', spacing=1.0, rotation_z=0.0, extrude=0.00003):
    curve = bpy.data.curves.new(name='txt', type='FONT')
    curve.body = body
    curve.font = bpy.data.fonts.load(font_path, check_existing=True)
    curve.size = size
    curve.align_x = align
    curve.align_y = 'CENTER'
    curve.space_character = spacing
    curve.extrude = extrude
    obj = bpy.data.objects.new('txt_' + body, curve)
    obj.location = location
    obj.rotation_euler.z = rotation_z
    bpy.context.collection.objects.link(obj)
    curve.materials.append(mat)
    return obj


def polar(radius, degrees_clockwise):
    a = math.radians(degrees_clockwise)
    return radius * math.sin(a), radius * math.cos(a)


# ---------------------------------------------------------------- the panel
bpy.ops.mesh.primitive_plane_add(size=1.0, location=(0, 0, 0))
panel = bpy.context.active_object
panel.scale = (0.2, 0.16, 1)
bpy.ops.object.transform_apply(scale=True)
assign(panel, PANEL)

KX, KY = 0.0, 0.003            # knob centre

# printed scale around the knob: 11 ticks, every other one labelled
for i in range(11):
    deg = -135 + 27 * i
    major = i % 2 == 0
    r0, r1 = (0.0172, 0.0218) if major else (0.0183, 0.0207)
    x, y = polar((r0 + r1) / 2, deg)
    box('tick', (0.00072 if major else 0.00048, r1 - r0, 0.00006), (KX + x, KY + y, 0.00003),
        rotation_z=-math.radians(deg), mat=INK)
labels = ['-60', '-48', '-36', '-24', '-12', '0']
for i, lab in enumerate(labels):
    deg = -135 + 54 * i
    x, y = polar(0.0262, deg)
    text(lab, FONT_LABEL, 0.0036, (KX + x, KY + y, 0.00001), INK)
text('THRESHOLD', FONT_LABEL, 0.0052, (KX, KY + 0.0338, 0.00001), INK, spacing=1.18)

# ---------------------------------------------------------------- the knob
knob_parts = []

bpy.ops.mesh.primitive_cylinder_add(vertices=96, radius=0.0118, depth=0.0040, location=(0, 0, 0.0020))
skirt = bpy.context.active_object
bevel(skirt, 0.0009, 4)
smooth(skirt, 50)
assign(skirt, BAKELITE)
knob_parts.append(skirt)

bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, radius=0.0080, location=(0, 0, 0.0058))
dome = bpy.context.active_object
dome.scale = (1, 1, 0.82)
smooth(dome)
assign(dome, BAKELITE)
knob_parts.append(dome)

bar = bpy.data.curves.new('pointer', type='CURVE')
bar.dimensions = '2D'
bar.fill_mode = 'BOTH'
bar.extrude = 0.0034
bar.bevel_depth = 0.0013
bar.bevel_resolution = 5
outline = [(0.0, 0.0206), (0.0023, 0.0176), (0.0044, 0.0035), (0.0039, -0.0094),
           (0.0, -0.0138), (-0.0039, -0.0094), (-0.0044, 0.0035), (-0.0023, 0.0176)]
spline = bar.splines.new('BEZIER')
spline.bezier_points.add(len(outline) - 1)
for p, (x, y) in zip(spline.bezier_points, outline):
    p.co = (x, y, 0.0)
    p.handle_left_type = p.handle_right_type = 'AUTO'
spline.use_cyclic_u = True
bar_obj = bpy.data.objects.new('pointer', bar)
bar_obj.location = (0, 0, 0.0040 + 0.0034 + 0.0005)
bpy.context.collection.objects.link(bar_obj)
bar.materials.append(BAKELITE)
knob_parts.append(bar_obj)

inlay = box('inlay', (0.0012, 0.0118, 0.0004), (0, 0.0118, 0.0040 + 0.0034 * 2 + 0.0005 + 0.0013 - 0.0001), mat=INLAY,
            bevel_width=0.0002)
knob_parts.append(inlay)

knob = bpy.data.objects.new('knob', None)
bpy.context.collection.objects.link(knob)
for part in knob_parts:
    part.parent = knob
knob.location = (KX, KY, 0)
knob.rotation_euler.z = -math.radians(ANGLE)

# ---------------------------------------------------------------- readout window
WY = KY - 0.0375
box('bezel', (0.038, 0.0128, 0.0014), (KX, WY, 0.0007), mat=BEZEL, bevel_width=0.0005)
box('glass', (0.0336, 0.0088, 0.0002), (KX, WY, 0.00145), mat=GLASS)
digits = text('-24.5 dB', FONT_DIGITS, 0.0062, (KX, WY - 0.0001, 0.00162), PHOSPHOR, spacing=1.05, extrude=0.0)

# ---------------------------------------------------------------- screws
for (sx, sy, rot) in [(-0.054, 0.039, 25), (0.054, -0.039, -50)]:
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=0.0031, depth=0.0016, location=(sx, sy, 0.0008))
    head = bpy.context.active_object
    bevel(head, 0.0007, 4)
    cutter = box('slot', (0.0074, 0.00075, 0.003), (sx, sy, 0.0018), rotation_z=math.radians(rot))
    cutter.hide_render = True
    cutter.hide_viewport = True
    b = head.modifiers.new('Slot', 'BOOLEAN')
    b.operation = 'DIFFERENCE'
    b.object = cutter
    b.solver = 'EXACT'
    smooth(head, 45)
    assign(head, STEEL)

# ---------------------------------------------------------------- lights, camera
world = bpy.data.worlds.new('World')
scene.world = world
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = srgb('#9aa3ad')
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.035


def area_light(name, location, power, size, colour='#ffffff'):
    data = bpy.data.lights.new(name, type='AREA')
    data.energy = power
    data.size = size
    data.shape = 'DISK'
    data.color = srgb(colour)[:3]
    obj = bpy.data.objects.new(name, data)
    obj.location = location
    bpy.context.collection.objects.link(obj)
    direction = -obj.location.normalized()
    obj.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
    return obj


area_light('key', (-0.42, 0.32, 0.40), 7.0, 0.26, '#fff2e0')
area_light('fill', (0.45, -0.1, 0.35), 0.7, 0.4, '#dfe8ff')
area_light('kicker', (0.25, 0.35, 0.12), 0.6, 0.12, '#ffffff')

cam_data = bpy.data.cameras.new('cam')
cam_data.lens = 100
cam_data.sensor_width = 36
cam = bpy.data.objects.new('cam', cam_data)
cam.location = (0, 0.0, 0.36)
bpy.context.collection.objects.link(cam)
scene.camera = cam
cam_data.dof.use_dof = True
cam_data.dof.focus_distance = 0.352
cam_data.dof.aperture_fstop = 11

# ---------------------------------------------------------------- render settings
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = SAMPLES
scene.cycles.use_adaptive_sampling = True
scene.cycles.adaptive_threshold = 0.015
scene.cycles.use_denoising = False
scene.cycles.seed = SEED
scene.cycles.max_bounces = 6
scene.cycles.caustics_reflective = False
scene.cycles.caustics_refractive = False
scene.render.resolution_x, scene.render.resolution_y = RES
scene.render.resolution_percentage = 100
scene.render.film_transparent = False
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_depth = '16'
scene.render.filepath = OUT
scene.render.threads_mode = 'AUTO'
try:
    scene.view_settings.view_transform = 'AgX'
    scene.view_settings.look = 'AgX - Medium High Contrast'
except Exception:
    scene.view_settings.view_transform = 'Filmic'
scene.view_settings.exposure = 0.0

# a little bloom on the glowing digits
scene.use_nodes = True
tree = scene.node_tree
for n in list(tree.nodes):
    tree.nodes.remove(n)
rl = tree.nodes.new('CompositorNodeRLayers')
glare = tree.nodes.new('CompositorNodeGlare')
glare.glare_type = 'FOG_GLOW'
glare.quality = 'HIGH'
glare.threshold = 1.2
glare.size = 6
comp = tree.nodes.new('CompositorNodeComposite')
tree.links.new(rl.outputs['Image'], glare.inputs['Image'])
tree.links.new(glare.outputs['Image'], comp.inputs['Image'])

SWEEP = arg('--sweep', 1, 0, int)
if SWEEP:
    # the knob turning end to end (eased), with the readout following it
    for i in range(SWEEP):
        f = 0.5 - 0.5 * math.cos(math.pi * i / (SWEEP - 1))
        knob.rotation_euler.z = -math.radians(-135 + 270 * f)
        digits.data.body = f"{-60 + 60 * f:.1f} dB"
        scene.render.filepath = f"{OUT}_{i:02d}.png"
        bpy.ops.render.render(write_still=True)
        print('WROTE', scene.render.filepath, flush=True)
else:
    bpy.ops.render.render(write_still=True)
    print('WROTE', OUT)
