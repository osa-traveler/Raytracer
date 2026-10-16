#!/usr/bin/env python3
"""Writes Scene/Test/TextureTest.glb: the smallest scene that exercises texture loading (M8, step 1).

A floor (8 x 8 m, UV 0..2 so the texture repeats twice each way) with
  * baseColorTexture: a 16 x 16 checker, 4 x 4 pixel cells, red (even cells) / white (odd cells), and
  * metallicRoughnessTexture (glTF packing: G = roughness, B = metalness): the top half of the image is metal with G = 64
    (roughness 0.25), the bottom half is a rough dielectric (G = 255, B = 0);
a left (x = -4) and a right (x = +4) side wall with a tangent-space normal map (16 x 16: left half of the image tilts the normal towards +u,
(tn = (0.6, 0, 0.8)), right half towards "up in the image" (tn = (0, 0.6, 0.8))); the two walls are seen from inside, so the tangent
frame has handedness -1 on the left wall and +1 on the right one; and a back wall with a 128 x 128 test pattern, pixel (x, y) = ((3x + 5y) & 255, (x*x + y) & 255, (x ^ y) & 255), which is
compressible enough for zlib to use dynamic Huffman blocks. The PNGs are written with all five scanline filters (row y uses
filter y % 5) and zlib level 9, so the renderer's decoder meets every case. tests/test_main.cpp recomputes the same pixels.
Y up, glTF conventions; area light and camera as in make_material_row.py.
Usage: python3 tools/make_texture_test.py
"""
import json, math, struct, zlib

def png_bytes(w, h, pixel):
    """8-bit RGB PNG; row y uses scanline filter y % 5."""
    raw = bytearray()
    prev = bytes(w * 3)
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row += bytes(pixel(x, y))
        ft = y % 5
        out = bytearray([ft])
        for i in range(len(row)):
            a = row[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if ft == 0: p = 0
            elif ft == 1: p = a
            elif ft == 2: p = b
            elif ft == 3: p = (a + b) >> 1
            else:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                p = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            out.append((row[i] - p) & 255)
        raw += out
        prev = bytes(row)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))

def checker(x, y):
    return (230, 40, 40) if ((x // 4) + (y // 4)) % 2 == 0 else (240, 240, 240)

def metal_rough(x, y):                     # R unused, G = roughness, B = metalness
    return (0, 64, 255) if y < 8 else (0, 255, 0)

def pattern(x, y):
    return ((3 * x + 5 * y) & 255, (x * x + y) & 255, (x ^ y) & 255)

def normal_map(x, y):                      # tangent-space normals, 8 bit: tn = (0.6, 0, 0.8) on the left half, (0, 0.6, 0.8) on the right
    return (204, 128, 230) if x < 8 else (128, 204, 230)

images = [png_bytes(16, 16, checker), png_bytes(16, 16, metal_rough), png_bytes(128, 128, pattern), png_bytes(16, 16, normal_map)]

meshes, mats, nodes, accessors, views = [], [], [], [], []
bin_data = bytearray()

def add_view(data):
    views.append({'buffer': 0, 'byteOffset': len(bin_data), 'byteLength': len(data)})
    bin_data.extend(data)
    bin_data.extend(b'\0' * (-len(bin_data) % 4))
    return len(views) - 1

image_views = [add_view(d) for d in images]

def quad(name, corners, normal, uvs, material):
    """corners and uvs in the order p0, p1, p2, p3 (counter-clockwise seen from the normal side)."""
    pos = bytearray(); nrm = bytearray(); tex = bytearray()
    for i in (0, 1, 2, 0, 2, 3):
        pos += struct.pack('<3f', *corners[i]); nrm += struct.pack('<3f', *normal); tex += struct.pack('<2f', *uvs[i])
    lo = [min(c[k] for c in corners) for k in range(3)]; hi = [max(c[k] for c in corners) for k in range(3)]
    first = len(accessors)
    for data, typ, mm in ((pos, 'VEC3', (lo, hi)), (nrm, 'VEC3', None), (tex, 'VEC2', None)):
        v = add_view(data)
        acc = {'bufferView': v, 'componentType': 5126, 'count': 6, 'type': typ}
        if mm: acc['min'], acc['max'] = mm
        accessors.append(acc)
    meshes.append({'name': name, 'primitives': [{'attributes': {'POSITION': first, 'NORMAL': first + 1, 'TEXCOORD_0': first + 2},
                                                 'material': material, 'mode': 4}]})
    nodes.append({'name': name, 'mesh': len(meshes) - 1})

mats.append({'name': 'floor', 'doubleSided': True, 'pbrMetallicRoughness': {
    'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 1.0, 'roughnessFactor': 1.0,
    'baseColorTexture': {'index': 0}, 'metallicRoughnessTexture': {'index': 1}}})
mats.append({'name': 'wall', 'doubleSided': True, 'pbrMetallicRoughness': {
    'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0.0, 'roughnessFactor': 0.8, 'baseColorTexture': {'index': 2}}})
mats.append({'name': 'sidewall', 'doubleSided': True, 'pbrMetallicRoughness': {
    'baseColorFactor': [0.7, 0.7, 0.7, 1], 'metallicFactor': 0.0, 'roughnessFactor': 0.9}, 'normalTexture': {'index': 3, 'scale': 1.0}})
# floor: world (x, z) in [-4, 4]^2  ->  uv = ((x + 4) / 4, (z + 4) / 4)  (0..2)
quad('floor', [(-4, 0, 4), (4, 0, 4), (4, 0, -4), (-4, 0, -4)], (0, 1, 0), [(0, 2), (2, 2), (2, 0), (0, 0)], 0)
# wall at z = -4, x in [-4, 4], y in [0, 6]  ->  uv = ((x + 4) / 8, 1 - y / 6)
quad('wall', [(-4, 0, -4), (4, 0, -4), (4, 6, -4), (-4, 6, -4)], (0, 0, 1), [(0, 1), (1, 1), (1, 0), (0, 0)], 1)

# left wall at x = -4 (normal +x): uv = ((z + 4) / 8, 1 - y / 6), so u grows with +z and v runs down; handedness -1
quad('left_wall', [(-4, 0, 4), (-4, 0, -4), (-4, 6, -4), (-4, 6, 4)], (1, 0, 0), [(1, 1), (0, 1), (0, 0), (1, 0)], 2)
# right wall at x = +4 (normal -x): the same uv rule, seen from inside the room; handedness +1
quad('right_wall', [(4, 0, -4), (4, 0, 4), (4, 6, 4), (4, 6, -4)], (-1, 0, 0), [(0, 1), (1, 1), (1, 0), (0, 0)], 2)

h = math.sqrt(0.5)
nodes.append({'name': 'Area_Light', 'translation': [0.0, 5.0, 0.0], 'rotation': [-h, 0, 0, h],
              'extras': {'rt_light_type': 'rect', 'rt_size_x': 3.0, 'rt_size_y': 3.0, 'rt_radiance': 6.0}})
eye = (0.0, 3.2, 8.5)
# camera looking at (0, 1.5, -4): pitch down by atan2(3.2 - 1.5, 12.5)
pitch = -math.atan2(eye[1] - 1.5, eye[2] + 4)
nodes.append({'name': 'Camera', 'camera': 0, 'translation': list(eye), 'rotation': [math.sin(pitch / 2), 0, 0, math.cos(pitch / 2)]})
doc = {'asset': {'version': '2.0', 'generator': 'tools/make_texture_test.py'}, 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}],
       'nodes': nodes, 'meshes': meshes, 'materials': mats,
       'images': [{'bufferView': v, 'mimeType': 'image/png'} for v in image_views],
       'samplers': [{'magFilter': 9729, 'minFilter': 9729, 'wrapS': 10497, 'wrapT': 10497}],
       'textures': [{'sampler': 0, 'source': i} for i in range(4)],
       'cameras': [{'type': 'perspective', 'perspective': {'yfov': 0.9, 'aspectRatio': 4 / 3, 'znear': 0.1, 'zfar': 100}}],
       'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(bin_data)}]}
js = json.dumps(doc, separators=(',', ':')).encode()
js += b' ' * (-len(js) % 4); bin_data += b'\0' * (-len(bin_data) % 4)
total = 12 + 8 + len(js) + 8 + len(bin_data)
with open('Scene/Test/TextureTest.glb', 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, total))
    f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
    f.write(struct.pack('<II', len(bin_data), 0x004E4942) + bin_data)
print('wrote Scene/Test/TextureTest.glb:', len(images), 'PNGs (', [len(i) for i in images], 'bytes ),', len(mats), 'materials')
