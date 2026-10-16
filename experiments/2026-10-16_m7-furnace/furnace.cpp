// White furnace: a sphere of one material in a uniform environment of radiance 1 (no light, no absorption). A model that conserves
// energy and has no gain gives a mean radiance of 1 over the sphere. Build and run with run.sh.
#include <cstdio>
#include "src/raytracer.h"

static void addSphere(Scene& sc, int mat) {
    int nu = 64, nv = 32; float r = 1;
    auto pt = [&](int iu, int iv) { float th = 3.14159265f * iv / nv, ph = 2 * 3.14159265f * iu / nu; return Vec3(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)); };
    for (int iv = 0; iv < nv; iv++) for (int iu = 0; iu < nu; iu++) {
        Vec3 a = pt(iu, iv), b = pt(iu + 1, iv), c = pt(iu, iv + 1), d = pt(iu + 1, iv + 1);
        auto tri = [&](Vec3 u, Vec3 v, Vec3 w) { Triangle t; t.p0 = u * r; t.p1 = v * r; t.p2 = w * r; t.n0 = u; t.n1 = v; t.n2 = w; t.material = mat;
                                                 if (length(cross(t.p1 - t.p0, t.p2 - t.p0)) > 1e-9f) sc.tris.push_back(t); };
        tri(a, b, c); tri(c, b, d);
    }
}
static double furnace(Material m, bool multiScatter, int n = 200000) {
    Scene sc; sc.materials = {m}; addSphere(sc, 0); sc.buildBvh();
    sc.integrator = Integrator::Path; sc.bsdfModel = BsdfModel::Pbr; sc.multiScatter = multiScatter;
    sc.envUniform = 1; sc.nee = false; sc.maxDepth = 40;
    double sum = 0;
    for (int i = 0; i < n; i++) {                                    // camera rays on a deterministic scatter of the sphere's disc
        Sampler s(8, 1, i);
        float ox = ((i * 7919) % 1000) / 1000.0f * 1.6f - 0.8f, oy = ((i * 104729) % 1000) / 1000.0f * 1.6f - 0.8f;
        sum += pathTracePbr(sc, Ray{{ox, oy, 5.0f}, {0, 0, -1}}, s).y;
    }
    return sum / n;
}
int main() {
    auto mat = [](float metallic, float rough, float trans = 0, float ior = 1.5f) {
        Material m; m.base = {1, 1, 1}; m.metallic = metallic; m.roughness = rough; m.transmission = trans; m.ior = ior; return m; };
    std::printf("{\n");
    std::printf("  \"mirror\": %.4f,\n  \"glass_ior_1.5\": %.4f,\n  \"glass_ior_1.9\": %.4f,\n", furnace(mat(1, 0), true), furnace(mat(0, 0, 1), true), furnace(mat(0, 0, 1, 1.9f), true));
    const float rs[] = {0.1f, 0.2f, 0.35f, 0.5f, 0.75f, 1.0f};
    std::printf("  \"roughness\": [0.1, 0.2, 0.35, 0.5, 0.75, 1.0],\n");
    for (int kind = 0; kind < 2; kind++) for (int ms = 0; ms < 2; ms++) {
        std::printf("  \"%s_%s\": [", kind == 0 ? "metal" : "plastic", ms ? "multiscatter" : "single");
        for (int i = 0; i < 6; i++) std::printf("%s%.4f", i ? ", " : "", furnace(mat(kind == 0 ? 1.0f : 0.0f, rs[i]), ms != 0));
        std::printf("]%s\n", (kind == 1 && ms == 1) ? "" : ",");
    }
    std::printf("}\n");
}
