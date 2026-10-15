#!/usr/bin/env python3
"""Writes Scene/Test/Cornell.glb: a closed Cornell box (open toward the camera) with a rectangular area light.

Y up, glTF conventions; the room spans x,z in [-1, 1], y in [0, 2]. Red left wall, green right wall, white elsewhere,
two white boxes. The light is a node with the extras the renderer reads (rt_light_type = "rect", rt_size_x/y, rt_radiance);
it emits toward local -Z, so it is rotated -90 degrees about X to face down.
Usage: python3 tools/make_cornell.py [radiance]
"""
import json, math, struct, sys

RADIANCE = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0
LIGHT_SIZE = 0.5

def quad(a, b, c, d):          # counter-clockwise seen from the side the normal points to
    return [(a, b, c), (a, c, d)]

def tri_normal(t):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
    u = (bx - ax, by - ay, bz - az); v = (cx - ax, cy - ay, cz - az)
    n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
    l = math.sqrt(sum(x*x for x in n)); return tuple(x/l for x in n)

def box(cx, cz, sx, sy, sz, yaw):
    c, s = math.cos(yaw), math.sin(yaw)
    def P(x, y, z): return (cx + c*x + s*z, y, cz - s*x + c*z)
    hx, hz = sx/2, sz/2
    p = {k: P(*v) for k, v in {
        'a': (-hx, 0, -hz), 'b': (hx, 0, -hz), 'c': (hx, 0, hz), 'd': (-hx, 0, hz),
        'e': (-hx, sy, -hz), 'f': (hx, sy, -hz), 'g': (hx, sy, hz), 'h': (-hx, sy, hz)}.items()}
    out = []
    out += quad(p['e'], p['h'], p['g'], p['f'])   # top (+y)
    out += quad(p['d'], p['c'], p['g'], p['h'])   # +z
    out += quad(p['c'], p['b'], p['f'], p['g'])   # +x
    out += quad(p['b'], p['a'], p['e'], p['f'])   # -z
    out += quad(p['a'], p['d'], p['h'], p['e'])   # -x
    return out

COLORS = {'white': (0.73, 0.73, 0.73), 'red': (0.65, 0.05, 0.05), 'green': (0.12, 0.45, 0.15)}
T = {k: [] for k in COLORS}
T['white'] += quad((-1,0,1), (1,0,1), (1,0,-1), (-1,0,-1))      # floor, normal +y
T['white'] += quad((-1,2,-1), (1,2,-1), (1,2,1), (-1,2,1))      # ceiling, normal -y
T['white'] += quad((-1,0,-1), (1,0,-1), (1,2,-1), (-1,2,-1))    # back wall, normal +z
T['red']   += quad((-1,0,1), (-1,0,-1), (-1,2,-1), (-1,2,1))    # left wall, normal +x
T['green'] += quad((1,0,-1), (1,0,1), (1,2,1), (1,2,-1))        # right wall, normal -x
T['white'] += box(-0.38, -0.30, 0.60, 1.20, 0.60, math.radians(18))    # tall box
T['white'] += box(0.40, 0.30, 0.60, 0.60, 0.60, math.radians(-15))      # short box

bin_data = bytearray(); views = []; accessors = []; meshes = []; mats = []
for i, name in enumerate(COLORS):
    col, tris = COLORS[name], T[name]
    pos = bytearray(); nrm = bytearray(); lo = [1e9]*3; hi = [-1e9]*3
    for t in tris:
        n = tri_normal(t)
        for v in t:
            pos += struct.pack('<3f', *v); nrm += struct.pack('<3f', *n)
            lo = [min(a, b) for a, b in zip(lo, v)]; hi = [max(a, b) for a, b in zip(hi, v)]
    nv = len(tris) * 3
    for data, kind, mm in ((pos, 'POSITION', (lo, hi)), (nrm, 'NORMAL', None)):
        views.append({'buffer': 0, 'byteOffset': len(bin_data), 'byteLength': len(data)})
        bin_data += data
        acc = {'bufferView': len(views)-1, 'componentType': 5126, 'count': nv, 'type': 'VEC3'}
        if mm: acc['min'], acc['max'] = mm
        accessors.append(acc)
    meshes.append({'name': name, 'primitives': [{'attributes': {'POSITION': len(accessors)-2, 'NORMAL': len(accessors)-1}, 'material': i, 'mode': 4}]})
    mats.append({'name': name, 'pbrMetallicRoughness': {'baseColorFactor': [*col, 1], 'metallicFactor': 0, 'roughnessFactor': 1}})

h = math.sqrt(0.5)
nodes = [{'name': n, 'mesh': i} for i, n in enumerate(COLORS)]
nodes.append({'name': 'Area_Light', 'translation': [0, 1.995, 0], 'rotation': [-h, 0, 0, h],
              'extras': {'rt_light_type': 'rect', 'rt_size_x': LIGHT_SIZE, 'rt_size_y': LIGHT_SIZE, 'rt_radiance': RADIANCE}})
nodes.append({'name': 'Camera', 'camera': 0, 'translation': [0, 1, 3.8]})
doc = {'asset': {'version': '2.0', 'generator': 'tools/make_cornell.py'}, 'scene': 0,
       'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes, 'meshes': meshes, 'materials': mats,
       'cameras': [{'type': 'perspective', 'perspective': {'yfov': 0.68, 'aspectRatio': 4/3, 'znear': 0.1, 'zfar': 100}}],
       'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(bin_data)}]}
js = json.dumps(doc, separators=(',', ':')).encode()
js += b' ' * (-len(js) % 4); bin_data += b'\0' * (-len(bin_data) % 4)
total = 12 + 8 + len(js) + 8 + len(bin_data)
with open('Scene/Test/Cornell.glb', 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, total))
    f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
    f.write(struct.pack('<II', len(bin_data), 0x004E4942) + bin_data)
print('wrote Scene/Test/Cornell.glb, radiance', RADIANCE)
