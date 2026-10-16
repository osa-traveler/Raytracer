"""Run inside Blender (Scripting tab / MCP execute_blender_code). Writes, into out/ next to this file's folder:
  nm_test.glb      a 2 m plane with a 2 x 2 tangent-space normal map (one tilt per quadrant), exported by Blender's glTF exporter
  nm_test_aov.npy  Blender's own normal (world space, Z up) for that plane: an orthographic camera straight above, emission = normal-map normal * 0.5 + 0.5
The plane is created far from the scene and everything (objects, materials, image, camera, render settings) is removed / restored afterwards.
The .blend must be saved in the repository's Scene/Test/ folder (the output folder is derived from its path)."""
import bpy, numpy as np
import os
repo = os.path.dirname(os.path.dirname(os.path.dirname(bpy.data.filepath)))    # the .blend is saved in Scene/Test/
OUT = os.path.join(repo, "experiments", "2026-10-17_normal-map-convention", "out") + os.sep
sc=bpy.context.scene; cy=sc.cycles; vl=bpy.context.view_layer
prev=dict(cam=sc.camera, samples=cy.samples, den=cy.use_denoising, ad=cy.use_adaptive_sampling, rx=sc.render.resolution_x, ry=sc.render.resolution_y, rp=sc.render.resolution_percentage, fp=sc.render.filepath, ff=sc.render.image_settings.file_format, cd=sc.render.image_settings.color_depth, vt=sc.view_settings.view_transform, look=sc.view_settings.look, ov=vl.material_override)
created=[]
try:
    bpy.ops.mesh.primitive_plane_add(size=2, location=(40,40,0))          # facing +Z, UV (0,0) bottom-left .. (1,1) top-right
    plane=bpy.context.active_object; plane.name="NM_test_plane"; created.append(plane)
    tns=[(0.6,0,0.8),(0,0.6,0.8),(-0.6,0,0.8),(0,-0.6,0.8)]               # image pixels: bottom-left, bottom-right, top-left, top-right
    img=bpy.data.images.new("NM_test_img",2,2,alpha=False); img.colorspace_settings.name='Non-Color'
    px=[]
    for tn in tns: px += [tn[0]*0.5+0.5, tn[1]*0.5+0.5, tn[2]*0.5+0.5, 1.0]
    img.pixels=px; img.pack()
    def build(mat, final):
        mat.use_nodes=True; nt=mat.node_tree
        for n in list(nt.nodes): nt.nodes.remove(n)
        tex=nt.nodes.new('ShaderNodeTexImage'); tex.image=img; tex.interpolation='Closest'; tex.extension='EXTEND'
        nmap=nt.nodes.new('ShaderNodeNormalMap'); nmap.space='TANGENT'
        nt.links.new(tex.outputs['Color'],nmap.inputs['Color'])
        out=nt.nodes.new('ShaderNodeOutputMaterial')
        if final=='principled':
            b=nt.nodes.new('ShaderNodeBsdfPrincipled'); nt.links.new(nmap.outputs['Normal'],b.inputs['Normal']); nt.links.new(b.outputs['BSDF'],out.inputs['Surface'])
        else:
            vm=nt.nodes.new('ShaderNodeVectorMath'); vm.operation='MULTIPLY_ADD'; vm.inputs[1].default_value=(0.5,0.5,0.5); vm.inputs[2].default_value=(0.5,0.5,0.5)
            em=nt.nodes.new('ShaderNodeEmission'); nt.links.new(nmap.outputs['Normal'],vm.inputs[0]); nt.links.new(vm.outputs['Vector'],em.inputs['Color']); nt.links.new(em.outputs['Emission'],out.inputs['Surface'])
    m1=bpy.data.materials.new("NM_test_mat"); build(m1,'principled'); plane.data.materials.append(m1); created.append(m1)
    bpy.ops.object.select_all(action='DESELECT'); plane.select_set(True); vl.objects.active=plane
    bpy.ops.export_scene.gltf(filepath=OUT+"nm_test.glb", export_format='GLB', use_selection=True, export_cameras=False, export_lights=False, export_apply=True, export_yup=True, export_image_format='AUTO')
    cd=bpy.data.cameras.new("NM_test_cam"); cd.type='ORTHO'; cd.ortho_scale=2.0
    co=bpy.data.objects.new("NM_test_cam",cd); sc.collection.objects.link(co); co.location=(40,40,3); created.append(co); sc.camera=co
    aov=bpy.data.materials.new("NM_test_aov"); build(aov,'emission'); created.append(aov); vl.material_override=aov
    cy.samples=4; cy.use_denoising=False; cy.use_adaptive_sampling=False
    sc.render.resolution_x=64; sc.render.resolution_y=64; sc.render.resolution_percentage=100
    sc.view_settings.view_transform='Standard'; sc.view_settings.look='None'
    sc.render.image_settings.file_format='OPEN_EXR'; sc.render.image_settings.color_depth='32'; sc.render.filepath=OUT+"nm_test_aov.exr"
    bpy.ops.render.render(write_still=True)
    im=bpy.data.images.load(OUT+"nm_test_aov.exr", check_existing=False); im.colorspace_settings.name='Non-Color'
    a=np.empty(64*64*4,np.float32); im.pixels.foreach_get(a); a=a.reshape(64,64,4)[::-1]; np.save(OUT+"nm_test_aov.npy",a); bpy.data.images.remove(im)
finally:
    vl.material_override=prev['ov']; sc.camera=prev['cam']
    cy.samples=prev['samples']; cy.use_denoising=prev['den']; cy.use_adaptive_sampling=prev['ad']
    sc.render.resolution_x=prev['rx']; sc.render.resolution_y=prev['ry']; sc.render.resolution_percentage=prev['rp']; sc.render.filepath=prev['fp']
    sc.render.image_settings.file_format=prev['ff']; sc.render.image_settings.color_depth=prev['cd']
    sc.view_settings.view_transform=prev['vt']; sc.view_settings.look=prev['look']
    for o in created:
        if isinstance(o,bpy.types.Object): bpy.data.objects.remove(o,do_unlink=True)
    for m in [x for x in bpy.data.materials if x.name.startswith("NM_test")]: bpy.data.materials.remove(m)
    for c in [x for x in bpy.data.cameras if x.name.startswith("NM_test")]: bpy.data.cameras.remove(c)
    for i in [x for x in bpy.data.images if x.name.startswith("NM_test")]: bpy.data.images.remove(i)
