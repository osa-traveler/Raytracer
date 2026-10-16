"""Compares the renderer's shading normal (out/nm_tracer.bin, from nm_probe) with Blender's own (out/nm_test_aov.npy).
Run with a Python that has numpy (e.g. /opt/anaconda3/bin/python3) from this folder."""
import numpy as np
t=np.fromfile("out/nm_tracer.bin",dtype=np.float32).reshape(64,64,7)
b=np.load("out/nm_test_aov.npy")[:,:,:3]
bn=2*b-1; bn=np.stack([bn[...,0],bn[...,2],-bn[...,1]],-1); bn/=np.linalg.norm(bn,axis=-1,keepdims=True)   # Blender Z-up world -> glTF Y-up world
ang=lambda a,c: np.degrees(np.arccos(np.clip((a*c).sum(-1),-1,1)))
centres=np.zeros((64,64),bool)
for cy in (16,48):
    for cx in (16,48): centres[cy-2:cy+2,cx-2:cx+2]=True          # +-2 pixels around the texel centres (u, v = 0.25 / 0.75), where bilinear = nearest
print("at the texel centres, angle between the renderer's normal and Blender's:")
for name,n in (("as implemented (+Y up, OpenGL)",t[...,1:4]),("green inverted",t[...,4:7])):
    a=ang(n,bn)[centres]; print("  %-34s median %.2f deg, max %.2f deg (n=%d)"%(name,np.median(a),a.max(),centres.sum()))
