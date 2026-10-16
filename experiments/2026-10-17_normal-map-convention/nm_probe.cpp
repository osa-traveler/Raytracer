// Orthographic probe of the synthetic normal-map plane (exported by Blender): glTF world, rays straight down.
#include <cstdio>
#include "../../src/raytracer.h"
int main(int, char** argv) {
    Scene sc = loadScene(argv[1]);
    std::printf("materials %zu textures %zu tris %zu normalTex %d\n", sc.materials.size(), sc.textures.size(), sc.tris.size(), sc.materials.empty() ? -9 : sc.materials[0].normalTex);
    std::vector<float> out(64 * 64 * 7, 0.0f);
    for (int j = 0; j < 64; j++)
        for (int i = 0; i < 64; i++) {
            float x = 40 - 1 + (i + 0.5f) / 32, yb = 40 + 1 - (j + 0.5f) / 32;          // Blender world x, y of this pixel of the ortho camera
            Ray r{Vec3(x, 3, -yb), Vec3(0, -1, 0)};                                       // glTF: (x, z_b, -y_b)
            Hit h;
            if (!sc.intersect(r, INF, h)) continue;
            float* o = &out[((size_t)j * 64 + i) * 7];
            o[0] = 1;
            Hit h1 = h; applyNormalMap(sc, h1);
            o[1] = h1.n.x; o[2] = h1.n.y; o[3] = h1.n.z;
            Hit h2 = h;                                                                   // green inverted
            const Material& m = sc.materials[(size_t)h.material];
            Vec3 c = sc.textures[m.normalTex].sample<false>(h.tu, h.tv);
            float tx = c.x * 2 - 1, ty = -(c.y * 2 - 1), tz = c.z * 2 - 1;
            Vec3 t = normalize(h.tg - h.n * dot(h.n, h.tg)); Vec3 b = cross(h.n, t) * h.tw;
            Vec3 n2 = normalize(t * tx + b * ty + h.n * tz); o[4] = n2.x; o[5] = n2.y; o[6] = n2.z;
        }
    std::FILE* f = std::fopen(argv[2], "wb"); std::fwrite(out.data(), 4, out.size(), f); std::fclose(f);
}
