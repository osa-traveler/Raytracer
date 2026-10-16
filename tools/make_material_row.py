#!/usr/bin/env python3
"""Writes Scene/Test/MaterialRow.glb: rows of spheres for looking at materials (M7).

Back row: gold metal, roughness 0.05 ... 1.0.   Middle row: red plastic (dielectric), the same roughnesses.
Front row: glass balls (IOR 1.2, 1.5, 2.4), a perfect mirror and a grey mirror.
Y up, glTF conventions. A large floor and a back wall, a rectangular area light above the front (extras as the renderer reads them:
rt_light_type, rt_size_x/y, rt_radiance; it emits toward local -Z, so it is rotated -90 degrees about X to face down).
Usage: python3 tools/make_material_row.py
"""
import json, math, struct

ROUGH = [0.05, 0.1, 0.2, 0.35, 0.5, 0.75, 1.0]
RADIUS = 0.42
LIGHT = dict(size=(3.2, 1.2), radiance=7.0, pos=(0.0, 4.2, 1.2))

def uv_sphere(cx, cy, cz, r, nu=64, nv=32):
    pts = []
    def P(iu, iv):
        th = math.pi * iv / nv; ph = 2 * math.pi * iu / nu
        n = (math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph))
        return (cx + r * n[0], cy + r * n[1], cz + r * n[2]), n
    tris = []
    for iv in range(nv):
        for iu in range(nu):
            a, b, c, d = P(iu, iv), P(iu + 1, iv), P(iu, iv + 1), P(iu + 1, iv + 1)
            for t in ((a, b, c), (c, b, d)):
                pa, pb, pc = t[0][0], t[1][0], t[2][0]
                u = [pb[i] - pa[i] for i in range(3)]; v = [pc[i] - pa[i] for i in range(3)]
                cr = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
                if math.sqrt(sum(x*x for x in cr)) > 1e-9: tris.append(t)
    return tris

def quad(p0, p1, p2, p3, n):
    return [((p0, n), (p1, n), (p2, n)), ((p0, n), (p2, n), (p3, n))]

meshes, mats, nodes, accessors, views = [], [], [], [], []
bin_data = bytearray()

def add_mesh(name, tris, material):
    global bin_data
    pos = bytearray(); nrm = bytearray(); lo = [1e9]*3; hi = [-1e9]*3
    for t in tris:
        for (p, n) in t:
            pos += struct.pack('<3f', *p); nrm += struct.pack('<3f', *n)
            lo = [min(a, b) for a, b in zip(lo, p)]; hi = [max(a, b) for a, b in zip(hi, p)]
    nv = len(tris) * 3
    first = len(accessors)
    for data, mm in ((pos, (lo, hi)), (nrm, None)):
        views.append({'buffer': 0, 'byteOffset': len(bin_data), 'byteLength': len(data)})
        bin_data += data
        acc = {'bufferView': len(views) - 1, 'componentType': 5126, 'count': nv, 'type': 'VEC3'}
        if mm: acc['min'], acc['max'] = mm
        accessors.append(acc)
    meshes.append({'name': name, 'primitives': [{'attributes': {'POSITION': first, 'NORMAL': first + 1}, 'material': material, 'mode': 4}]})
    nodes.append({'name': name, 'mesh': len(meshes) - 1})

def add_material(name, base, metallic, roughness, extensions=None):
    m = {'name': name, 'pbrMetallicRoughness': {'baseColorFactor': [*base, 1], 'metallicFactor': metallic, 'roughnessFactor': roughness}}
    if extensions: m['extensions'] = extensions
    mats.append(m); return len(mats) - 1

# environment
floor = add_material('floor', (0.55, 0.55, 0.55), 0, 0.9)
wall = add_material('wall', (0.30, 0.34, 0.42), 0, 0.9)
add_mesh('floor', quad((-20, 0, 20), (20, 0, 20), (20, 0, -20), (-20, 0, -20), (0, 1, 0)), floor)
add_mesh('wall', quad((-20, 0, -4.2), (20, 0, -4.2), (20, 14, -4.2), (-20, 14, -4.2), (0, 0, 1)), wall)

xs = [(i - 3) * 1.05 for i in range(7)]
for i, r in enumerate(ROUGH):                                             # back row: gold metal
    m = add_material(f'gold_r{r}', (1.0, 0.78, 0.34), 1.0, r)
    add_mesh(f'gold_r{r}', uv_sphere(xs[i], RADIUS, -2.4, RADIUS), m)
for i, r in enumerate(ROUGH):                                             # middle row: red plastic
    m = add_material(f'plastic_r{r}', (0.8, 0.08, 0.06), 0.0, r)
    add_mesh(f'plastic_r{r}', uv_sphere(xs[i], RADIUS, -0.7, RADIUS), m)
front = [('glass_ior1.2', dict(transmission=1.0, ior=1.2)), ('glass_ior1.5', dict(transmission=1.0, ior=1.5)), ('glass_ior2.4', dict(transmission=1.0, ior=2.4))]
for i, (name, p) in enumerate(front):                                     # front row: glass, mirror, grey mirror
    ext = {'KHR_materials_transmission': {'transmissionFactor': p['transmission']}, 'KHR_materials_ior': {'ior': p['ior']}}
    m = add_material(name, (1, 1, 1), 0.0, 0.0, ext)
    add_mesh(name, uv_sphere(xs[i + 1], RADIUS, 1.0, RADIUS), m)
mm = add_material('mirror', (1, 1, 1), 1.0, 0.0)
add_mesh('mirror', uv_sphere(xs[4], RADIUS, 1.0, RADIUS), mm)
gm = add_material('grey_mirror', (0.5, 0.5, 0.5), 1.0, 0.0)
add_mesh('grey_mirror', uv_sphere(xs[5], RADIUS, 1.0, RADIUS), gm)

h = math.sqrt(0.5)
nodes.append({'name': 'Area_Light', 'translation': list(LIGHT['pos']), 'rotation': [-h, 0, 0, h],
              'extras': {'rt_light_type': 'rect', 'rt_size_x': LIGHT['size'][0], 'rt_size_y': LIGHT['size'][1], 'rt_radiance': LIGHT['radiance']}})

def look_at_quat(eye, target, up=(0, 1, 0)):
    z = [eye[i] - target[i] for i in range(3)]; l = math.sqrt(sum(c*c for c in z)); z = [c / l for c in z]
    x = [up[1]*z[2]-up[2]*z[1], up[2]*z[0]-up[0]*z[2], up[0]*z[1]-up[1]*z[0]]; l = math.sqrt(sum(c*c for c in x)); x = [c / l for c in x]
    y = [z[1]*x[2]-z[2]*x[1], z[2]*x[0]-z[0]*x[2], z[0]*x[1]-z[1]*x[0]]
    m = [[x[0], y[0], z[0]], [x[1], y[1], z[1]], [x[2], y[2], z[2]]]
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        s = math.sqrt(t + 1) * 2; return [(m[2][1]-m[1][2])/s, (m[0][2]-m[2][0])/s, (m[1][0]-m[0][1])/s, 0.25*s]
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1 + m[0][0] - m[1][1] - m[2][2]) * 2; return [0.25*s, (m[0][1]+m[1][0])/s, (m[0][2]+m[2][0])/s, (m[2][1]-m[1][2])/s]
    if m[1][1] > m[2][2]:
        s = math.sqrt(1 + m[1][1] - m[0][0] - m[2][2]) * 2; return [(m[0][1]+m[1][0])/s, 0.25*s, (m[1][2]+m[2][1])/s, (m[0][2]-m[2][0])/s]
    s = math.sqrt(1 + m[2][2] - m[0][0] - m[1][1]) * 2; return [(m[0][2]+m[2][0])/s, (m[1][2]+m[2][1])/s, 0.25*s, (m[1][0]-m[0][1])/s]

eye, target = (0.0, 3.9, 9.2), (0.0, 0.3, -0.5)
nodes.append({'name': 'Camera', 'camera': 0, 'translation': list(eye), 'rotation': look_at_quat(eye, target)})
doc = {'asset': {'version': '2.0', 'generator': 'tools/make_material_row.py'}, 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}],
       'nodes': nodes, 'meshes': meshes, 'materials': mats,
       'extensionsUsed': ['KHR_materials_transmission', 'KHR_materials_ior'],
       'cameras': [{'type': 'perspective', 'perspective': {'yfov': 0.52, 'aspectRatio': 4 / 3, 'znear': 0.1, 'zfar': 100}}],
       'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(bin_data)}]}
js = json.dumps(doc, separators=(',', ':')).encode()
js += b' ' * (-len(js) % 4); bin_data += b'\0' * (-len(bin_data) % 4)
total = 12 + 8 + len(js) + 8 + len(bin_data)
with open('Scene/Test/MaterialRow.glb', 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, total))
    f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
    f.write(struct.pack('<II', len(bin_data), 0x004E4942) + bin_data)
print('wrote Scene/Test/MaterialRow.glb:', len(meshes), 'meshes,', len(mats), 'materials,', sum(a['count'] for a in accessors[::2]) // 3, 'triangles')
