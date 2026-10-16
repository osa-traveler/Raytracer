// Build and run from this folder: clang++ -std=c++17 -O2 -o variance_probe variance_probe.cpp && ./variance_probe 0.02 0.35   (sphere roughness, glossy floor)
// Per-point mean / standard error of the radiance of a floor spot next to a mirror-like sphere, with and without Scene::glossyFloor.
#include <cstdio>
#include "../../src/raytracer.h"
static void sphere(Scene& sc, Vec3 c, float r, int material, int nu = 48, int nv = 24) {
    auto pt = [&](int iu, int iv) { float th = 3.14159265f * iv / nv, ph = 2 * 3.14159265f * iu / nu; return Vec3(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)); };
    for (int iv = 0; iv < nv; iv++) for (int iu = 0; iu < nu; iu++) {
        Vec3 a = pt(iu, iv), b = pt(iu + 1, iv), cc = pt(iu, iv + 1), d = pt(iu + 1, iv + 1);
        auto tri = [&](Vec3 u, Vec3 v, Vec3 w) { Triangle t; t.p0 = c + u * r; t.p1 = c + v * r; t.p2 = c + w * r; t.n0 = u; t.n1 = v; t.n2 = w; t.material = material;
            if (length(cross(t.p1 - t.p0, t.p2 - t.p0)) > 1e-9f) sc.tris.push_back(t); };
        tri(a, b, cc); tri(cc, b, d);
    }
}
int main(int argc, char** argv) {
    float rough = argc > 1 ? (float)std::atof(argv[1]) : 0.02f, fl = argc > 2 ? (float)std::atof(argv[2]) : 0.35f;
    Scene sc;
    Material floorM; floorM.base = Vec3(0.6f, 0.6f, 0.6f); floorM.metallic = 0; floorM.roughness = 1; floorM.transmission = 0;
    Material metal; metal.base = Vec3(0.95f, 0.95f, 0.95f); metal.metallic = 1; metal.roughness = rough;
    sc.materials = {floorM, metal};
    Triangle t1; t1.p0 = Vec3(-20, 0, -20); t1.p1 = Vec3(-20, 0, 20); t1.p2 = Vec3(20, 0, -20); t1.n0 = t1.n1 = t1.n2 = Vec3(0, 1, 0); t1.material = 0;
    Triangle t2; t2.p0 = Vec3(20, 0, 20); t2.p1 = Vec3(20, 0, -20); t2.p2 = Vec3(-20, 0, 20); t2.n0 = t2.n1 = t2.n2 = Vec3(0, 1, 0); t2.material = 0;
    sc.tris = {t1, t2};
    sphere(sc, Vec3(0, 0.5f, 0), 0.5f, 1);
    AreaLight a; a.valid = true; a.center = Vec3(1.2f, 2.5f, 0.0f); a.edgeU = Vec3(0.4f, 0, 0); a.edgeV = Vec3(0, 0, 0.4f); a.normal = Vec3(0, -1, 0); a.area = 0.16f; a.radiance = 40.0f;
    sc.area = sc.areaFile = a;
    sc.integrator = Integrator::Path; sc.bsdfModel = BsdfModel::Pbr; sc.maxDepth = 4; sc.nee = true; sc.mis = true;
    sc.buildBvh();
    Scene bl = sc; bl.glossyFloor = fl;
    std::printf("sphere roughness %.3f, floor %.2f. floor spot (x, z): plain mean/se | blurred mean/se | se ratio\n", rough, fl);
    for (float x : {-1.5f, -1.0f, -0.7f, 0.7f, 1.0f, 1.5f, 2.0f})
        for (float z : {0.0f, 0.5f}) {
            Ray r{Vec3(x, 1.0f, z), Vec3(0, -1, 0)};
            double m[2], se[2];
            for (int k = 0; k < 2; k++) {
                const Scene& S = k ? bl : sc; double s1 = 0, s2 = 0; int n = 60000;
                for (int i = 0; i < n; i++) { Sampler smp(21, 0, (uint32_t)i); double v = pathTrace(S, r, smp).x; s1 += v; s2 += v * v; }
                m[k] = s1 / n; se[k] = std::sqrt(std::max(0.0, s2 / n - m[k] * m[k]) / n);
            }
            std::printf("  (%5.1f, %3.1f): %.4f/%.4f | %.4f/%.4f | %.2f\n", x, z, m[0], se[0], m[1], se[1], se[1] / std::max(se[0], 1e-12));
        }
}
