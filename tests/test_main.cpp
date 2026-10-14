// Small self-contained tests for the renderer core (no test framework needed).
//   cmake --build build && ./build/raytrace_tests      (or: ctest --test-dir build)
#include <cmath>
#include <cstdio>
#include <random>

#include "../src/raytracer.h"

static int g_checks = 0, g_failed = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(cond)) { g_failed++; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((double)(a) - (double)(b)) <= (tol))
#define TEST(name) static void name(); static struct Reg_##name { Reg_##name() { tests().push_back({#name, name}); } } reg_##name; static void name()

struct Case { const char* name; void (*fn)(); };
static std::vector<Case>& tests() { static std::vector<Case> t; return t; }

// A triangle with all three vertex normals equal to the face normal.
static Triangle makeTri(Vec3 a, Vec3 b, Vec3 c, int material = 0) {
    Triangle t;
    t.p0 = a; t.p1 = b; t.p2 = c;
    t.n0 = t.n1 = t.n2 = normalize(cross(b - a, c - a));
    t.material = material;
    return t;
}

static Material diffuseMat(Vec3 base) {
    Material m; m.base = base; m.metallic = 0; m.roughness = 1; m.transmission = 0; return m;
}

// ---------------------------------------------------------------- ray / triangle
// Reference triangle in the z = 0 plane: p0 = (0,0,0), p1 = (1,0,0), p2 = (0,1,0).
static const Triangle kTri = makeTri({0, 0, 0}, {1, 0, 0}, {0, 1, 0});

TEST(ray_triangle_hit_gives_distance_and_barycentrics) {
    float t, u, v;
    Ray r{{0.25f, 0.25f, 1}, {0, 0, -1}};
    CHECK(intersectTriangle(r, kTri, t, u, v));
    CHECK_NEAR(t, 1.0, 1e-6); CHECK_NEAR(u, 0.25, 1e-6); CHECK_NEAR(v, 0.25, 1e-6);
    Ray r2{{0.5f, 0.25f, 3}, {0, 0, -1}};
    CHECK(intersectTriangle(r2, kTri, t, u, v));
    CHECK_NEAR(t, 3.0, 1e-6); CHECK_NEAR(u, 0.5, 1e-6); CHECK_NEAR(v, 0.25, 1e-6);
}

TEST(ray_triangle_miss_outside) {
    float t, u, v;
    CHECK(!intersectTriangle(Ray{{2, 2, 1}, {0, 0, -1}}, kTri, t, u, v));       // far outside
    CHECK(!intersectTriangle(Ray{{0.6f, 0.6f, 1}, {0, 0, -1}}, kTri, t, u, v)); // beyond the hypotenuse (u+v > 1)
    CHECK(!intersectTriangle(Ray{{-0.01f, 0.5f, 1}, {0, 0, -1}}, kTri, t, u, v)); // just left of the edge u = 0
    CHECK(!intersectTriangle(Ray{{0.55f, 0.5f, 1}, {0, 0, -1}}, kTri, t, u, v));  // just beyond the hypotenuse (u+v = 1.05)
    CHECK(!intersectTriangle(Ray{{0.5f, -0.01f, 1}, {0, 0, -1}}, kTri, t, u, v));  // just below the edge v = 0
}

TEST(ray_triangle_parallel_misses) {
    float t, u, v;
    CHECK(!intersectTriangle(Ray{{0.25f, 0.25f, 1}, {1, 0, 0}}, kTri, t, u, v));
    CHECK(!intersectTriangle(Ray{{0.25f, 0.25f, 0}, {1, 0, 0}}, kTri, t, u, v)); // lying in the plane
}

TEST(ray_triangle_is_double_sided) {
    float t, u, v;
    CHECK(intersectTriangle(Ray{{0.25f, 0.25f, -1}, {0, 0, 1}}, kTri, t, u, v));  // from behind
    CHECK_NEAR(t, 1.0, 1e-6);
}

TEST(ray_triangle_edges_and_vertices_count_as_hits) {
    float t, u, v;
    CHECK(intersectTriangle(Ray{{0.5f, 0.0f, 1}, {0, 0, -1}}, kTri, t, u, v));  // on edge v = 0
    CHECK(intersectTriangle(Ray{{0.0f, 0.0f, 1}, {0, 0, -1}}, kTri, t, u, v));  // on vertex p0
    CHECK(intersectTriangle(Ray{{0.5f, 0.5f, 1}, {0, 0, -1}}, kTri, t, u, v));  // on the hypotenuse
}

TEST(ray_triangle_reports_negative_t_behind_origin) {
    // intersectTriangle tests the whole line; accepting only t > EPS is the caller's (Scene::intersect) job.
    float t, u, v;
    CHECK(intersectTriangle(Ray{{0.25f, 0.25f, 1}, {0, 0, 1}}, kTri, t, u, v));
    CHECK(t < 0);
}

// ---------------------------------------------------------------- scene intersection
static Scene twoLayerScene() {
    Scene sc;
    sc.materials = {diffuseMat({1, 1, 1}), diffuseMat({1, 0, 0})};
    sc.tris.push_back(makeTri({-5, 0, -5}, {0, 0, 5}, {5, 0, -5}, 0));      // y = 0, normal +y (counter-clockwise seen from above)
    sc.tris.push_back(makeTri({-5, -1, -5}, {0, -1, 5}, {5, -1, -5}, 1));   // y = -1
    sc.buildBvh();
    return sc;
}

TEST(scene_returns_closest_hit) {
    Scene sc = twoLayerScene();
    Hit h;
    CHECK(sc.intersect(Ray{{0, 3, 0}, {0, -1, 0}}, INF, h));
    CHECK_NEAR(h.t, 3.0, 1e-5); CHECK(h.material == 0);
    CHECK_NEAR(h.p.y, 0.0, 1e-5);
    CHECK_NEAR(h.n.y, 1.0, 1e-5);                       // interpolated normal is the face normal here
}

TEST(scene_respects_tmax_and_ignores_hits_behind_the_origin) {
    Scene sc = twoLayerScene();
    Hit h;
    CHECK(!sc.intersect(Ray{{0, 3, 0}, {0, -1, 0}}, 2.0f, h));       // plane is 3 away, tmax = 2
    CHECK(!sc.intersect(Ray{{0, 3, 0}, {0, 1, 0}}, INF, h));         // looking away
    CHECK(sc.intersect(Ray{{0, -0.5f, 0}, {0, -1, 0}}, INF, h));     // starting between the layers: only y = -1 is ahead
    CHECK(h.material == 1);
}

TEST(bvh_agrees_with_brute_force) {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> pos(-10, 10), small(-1, 1), dir(-1, 1);
    Scene sc;
    sc.materials = {diffuseMat({1, 1, 1})};
    for (int i = 0; i < 3000; i++) {
        Vec3 c(pos(rng), pos(rng), pos(rng));
        sc.tris.push_back(makeTri(c + Vec3(small(rng), small(rng), small(rng)), c + Vec3(small(rng), small(rng), small(rng)),
                                  c + Vec3(small(rng), small(rng), small(rng))));
    }
    sc.buildBvh();
    int hits = 0, mismatches = 0;
    for (int i = 0; i < 2000; i++) {
        Ray r{{pos(rng), pos(rng), pos(rng)}, normalize(Vec3(dir(rng), dir(rng), dir(rng)))};
        float best = INF; bool found = false;
        for (const Triangle& tr : sc.tris) {
            float t, u, v;
            if (intersectTriangle(r, tr, t, u, v) && t > EPS && t < best) { best = t; found = true; }
        }
        Hit h;
        bool bvhFound = sc.intersect(r, INF, h);
        if (bvhFound != found || (found && h.t != best)) mismatches++;
        hits += found;
    }
    CHECK(hits > 100);          // the test is only meaningful if many rays actually hit something
    CHECK(mismatches == 0);
}

// ---------------------------------------------------------------- shadow rays
static Scene shadowScene(float blockerY, bool glass) {
    Scene sc;
    sc.light = {Vec3(0, 5, 0), Vec3(1, 1, 1)};
    Material m = diffuseMat({0.5f, 0.5f, 0.5f});
    if (glass) { m.transmission = 1.0f; m.metallic = 0; m.roughness = 0; }
    sc.materials = {m};
    sc.tris.push_back(makeTri({-5, blockerY, -5}, {5, blockerY, -5}, {0, blockerY, 5}));
    sc.buildBvh();
    return sc;
}

TEST(shadow_ray_opaque_blocker_gives_full_shadow) {
    Vec3 t = shadowTransmittance(shadowScene(2.0f, false), Vec3(0, 0, 0), Vec3(0, 1, 0));
    CHECK(t.x == 0 && t.y == 0 && t.z == 0);
}

TEST(shadow_ray_ignores_blockers_beyond_the_light) {
    Vec3 t = shadowTransmittance(shadowScene(6.0f, false), Vec3(0, 0, 0), Vec3(0, 1, 0));   // above the light at y = 5
    CHECK(t.x == 1 && t.y == 1 && t.z == 1);
}

TEST(shadow_ray_glass_blocker_attenuates) {
    Vec3 t = shadowTransmittance(shadowScene(2.0f, true), Vec3(0, 0, 0), Vec3(0, 1, 0));
    CHECK_NEAR(t.x, 0.5 * 0.9, 1e-5);   // baseColor * 0.9 * transmission (the renderer's simple glass-shadow rule)
}

// ---------------------------------------------------------------- normal orientation
TEST(shading_normal_faces_the_viewer) {
    // One white diffuse floor, normal +y, light above. Seen from above it is lit; seen from below the renderer
    // flips the normal to face the ray, so N.L < 0 and only the ambient term is left.
    // The result must not depend on which way the stored normal (the vertex winding) happens to point.
    for (int flipped = 0; flipped < 2; flipped++) {
        Scene sc;
        sc.light = {Vec3(0, 5, 0), Vec3(1, 1, 1)};
        sc.ambient = Vec3(0.12f, 0.12f, 0.12f);
        sc.materials = {diffuseMat({1, 1, 1})};
        Triangle t = flipped ? makeTri({-5, 0, -5}, {5, 0, -5}, {0, 0, 5}) : makeTri({-5, 0, -5}, {0, 0, 5}, {5, 0, -5});
        CHECK_NEAR(t.n0.y, flipped ? -1.0 : 1.0, 1e-6);
        sc.tris.push_back(t);
        sc.buildBvh();
        Vec3 above = trace(sc, Ray{{0, 3, 0}, {0, -1, 0}}, 0);
        Vec3 below = trace(sc, Ray{{0, -3, 0}, {0, 1, 0}}, 0);
        CHECK(above.x > 1.0f);
        CHECK_NEAR(below.x, 0.12, 1e-6);
    }
}

// ---------------------------------------------------------------- film / display
TEST(film_averages_in_linear_space) {
    Film f(2, 1);
    f.add(0, 0, Vec3(0, 0, 0));
    f.add(0, 0, Vec3(1, 1, 1));
    CHECK_NEAR(f.pixel(0, 0).x, 0.5, 1e-7);
    Display d;
    CHECK(d.encode(f.pixel(0, 0).x) == 186);                        // 0.5^(1/2.2) * 255
    int wrong = (d.encode(0.0f) + d.encode(1.0f)) / 2;              // averaging after encoding is darker
    CHECK(wrong < 186);
    CHECK(f.pixel(1, 0).x == 0);                                    // no samples -> black, not NaN
}

TEST(film_keeps_values_above_one_until_display) {
    Film f(1, 1);
    f.add(0, 0, Vec3(4, 0.5f, 0));
    CHECK_NEAR(f.pixel(0, 0).x, 4.0, 1e-7);                         // not clamped in the film
    Display d;
    CHECK(d.encode(f.pixel(0, 0).x) == 255);                        // clamped only when displayed
    d.exposure = 0.25f;
    CHECK(d.encode(f.pixel(0, 0).x) == 255);                        // 4 * 0.25 = 1.0
    d.exposure = 0.125f;
    CHECK(d.encode(f.pixel(0, 0).x) == 186);                        // 4 * 0.125 = 0.5
}

// ---------------------------------------------------------------- math
TEST(normal_transform_uses_inverse_transpose) {
    const float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {2, 1, 1};
    Mat4 m = makeTRS(t, q, s);
    Vec3 n = m.normal(normalize(Vec3(1, 1, 0)));
    Vec3 expect = normalize(Vec3(0.5f, 1, 0));                      // diag(1/2, 1, 1) applied to (1,1,0)
    CHECK_NEAR(n.x, expect.x, 1e-6); CHECK_NEAR(n.y, expect.y, 1e-6);
}

// ---------------------------------------------------------------- glb loading (coordinate conversion)
TEST(glb_room_scene_loads_with_camera_and_spheres_in_place) {
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Room_2Spheres.glb");
    CHECK(sc.tris.size() > 1000);
    CHECK(sc.camera.valid);
    Camera cam(sc.camera);
    CHECK_NEAR(cam.pos.x, 1.25, 1e-4); CHECK_NEAR(cam.pos.y, 2.2, 1e-4); CHECK_NEAR(cam.pos.z, 6.8, 1e-4);   // Blender (1.25, -6.8, 2.2), Z-up -> Y-up
    CHECK(cam.forward.z < -0.9f && cam.forward.y < 0);                 // looks toward -z, tilted slightly down
    CHECK_NEAR(cam.tanHalfFov, std::tan(0.8985202f * 0.5f) * (16.0 / 9.0) / (4.0 / 3.0), 1e-4);
    Hit h;                                                             // sphere centres: metal (0,1,0), glass (2.5,1,0), r = 1
    CHECK(sc.intersect(Ray{{0, 3, 0}, {0, -1, 0}}, INF, h));
    CHECK_NEAR(h.t, 1.0, 1e-2); CHECK_NEAR(h.n.y, 1.0, 1e-2);
    CHECK(sc.intersect(Ray{{2.5f, 3, 0}, {0, -1, 0}}, INF, h));
    CHECK_NEAR(h.t, 1.0, 1e-2);
    CHECK(sc.intersect(Ray{{-4, 3, 0}, {0, -1, 0}}, INF, h));          // nothing there: hits the floor at y = 0
    CHECK_NEAR(h.t, 3.0, 1e-4); CHECK_NEAR(h.n.y, 1.0, 1e-4);
}

int main() {
    for (auto& c : tests()) {
        int before = g_failed;
        c.fn();
        std::printf("%s %s\n", g_failed == before ? "[ ok ]" : "[FAIL]", c.name);
    }
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
