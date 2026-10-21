import bpy
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
c = scene.cycles
print("denoisers:", [i.identifier for i in c.bl_rna.properties['denoiser'].enum_items])
try:
    c.denoiser = 'OPENIMAGEDENOISE'
    print("OIDN set ok")
except Exception as e:
    print("OIDN error", e)
