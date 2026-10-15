// Small self-contained tests for the renderer core (no test framework needed).
//   cmake --build build && ./build/raytrace_tests      (or: ctest --test-dir build)
#include <cmath>
#include <cstdio>
#include <random>

#include "../src/imageio.h"
#include "../src/raytracer.h"
#include "../src/render_job.h"

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
        Sampler smp(1, 0, 0);
        Vec3 above = trace(sc, Ray{{0, 3, 0}, {0, -1, 0}}, 0, smp);
        Vec3 below = trace(sc, Ray{{0, -3, 0}, {0, 1, 0}}, 0, smp);
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
    CHECK_NEAR(cam.pos.x, 0.0, 1e-4); CHECK_NEAR(cam.pos.y, 2.2, 1e-4); CHECK_NEAR(cam.pos.z, 6.8, 1e-4);     // Blender (0, -6.8, 2.2), Z-up -> Y-up
    CHECK(cam.forward.z < -0.9f && cam.forward.y < 0);                 // looks toward -z, tilted slightly down
    CHECK_NEAR(cam.tanHalfFov, std::tan(0.8985202f * 0.5f) * (16.0 / 9.0) / (4.0 / 3.0), 1e-4);
    Hit h;                                                             // sphere centres: metal (-1.25,1,0), glass (1.25,1,0), r = 1
    CHECK(sc.intersect(Ray{{-1.25f, 3, 0}, {0, -1, 0}}, INF, h));
    CHECK_NEAR(h.t, 1.0, 1e-2); CHECK_NEAR(h.n.y, 1.0, 1e-2);
    CHECK(sc.intersect(Ray{{1.25f, 3, 0}, {0, -1, 0}}, INF, h));
    CHECK_NEAR(h.t, 1.0, 1e-2);
    CHECK(sc.intersect(Ray{{0, 3, 0}, {0, -1, 0}}, INF, h));           // between the two spheres: hits the floor at y = 0
    CHECK_NEAR(h.t, 3.0, 1e-4); CHECK_NEAR(h.n.y, 1.0, 1e-4);
    // The room is centred on x = 0 and so is the camera: the ray through the image centre stays on x = 0 where it hits (the floor, seen from above).
    CHECK(sc.intersect(cam.ray(WIDTH / 2.0f, HEIGHT / 2.0f), INF, h));
    CHECK_NEAR(h.p.x, 0.0, 1e-3);
}

TEST(glb_area_light_is_read_from_node_extras) {
    for (const char* name : {"Untitled.glb", "Room_2Spheres.glb"}) {
        Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/" + name);
        const AreaLight& a = sc.area;
        CHECK(a.valid);
        CHECK_NEAR(a.center.x, -4.0, 1e-4); CHECK_NEAR(a.center.y, 5.0, 1e-4); CHECK_NEAR(a.center.z, 2.0, 1e-4);  // Blender (-4, -2, 5)
        CHECK_NEAR(length(a.edgeU), 2.0, 1e-4); CHECK_NEAR(length(a.edgeV), 2.0, 1e-4);                           // rt_size_x/y = 2 m
        CHECK_NEAR(a.area, 4.0, 1e-4);
        CHECK_NEAR(a.radiance, 11.56, 1e-4);
        CHECK_NEAR(length(a.normal), 1.0, 1e-5);
        CHECK_NEAR(dot(a.edgeU, a.normal), 0.0, 1e-4); CHECK_NEAR(dot(a.edgeV, a.normal), 0.0, 1e-4);              // rectangle is perpendicular to its normal
        CHECK_NEAR(dot(a.edgeU, a.edgeV), 0.0, 1e-4);
        // Emission direction as Blender reports it for this light, (0.6667, 0.3333, -0.6667) Z-up -> (x, z, -y) Y-up.
        CHECK_NEAR(a.normal.x, 0.6667, 1e-3); CHECK_NEAR(a.normal.y, -0.6667, 1e-3); CHECK_NEAR(a.normal.z, -0.3333, 1e-3);
        CHECK(dot(a.normal, Vec3(0, 1, 0) - a.center) > 0);       // it faces the middle of the room
        applyDefaultLighting(sc);
        CHECK_NEAR(sc.light.pos.x, a.center.x, 1e-6);             // step 1: shading still uses a point light at the centre
    }
}

// ---------------------------------------------------------------- random numbers
TEST(pcg32_matches_the_published_reference_sequence) {
    // pcg32_srandom(42, 54): the first outputs listed in the PCG reference implementation's demo.
    Pcg32 r; r.seed(42, 54);
    const uint32_t expect[6] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (uint32_t e : expect) CHECK(r.nextU32() == e);
}

TEST(sampler_is_reproducible_and_streams_differ) {
    Sampler a(7, 100, 0), b(7, 100, 0), otherPixel(7, 101, 0), otherSample(7, 100, 1), otherSeed(8, 100, 0);
    int same = 0, diffPixel = 0, diffSample = 0, diffSeed = 0;
    for (int i = 0; i < 1000; i++) {
        float x = a.next1D();
        same += (x == b.next1D());
        diffPixel += (x != otherPixel.next1D());
        diffSample += (x != otherSample.next1D());
        diffSeed += (x != otherSeed.next1D());
    }
    CHECK(same == 1000);                                    // same (seed, pixel, sample) -> same numbers
    CHECK(diffPixel > 990 && diffSample > 990 && diffSeed > 990);
}

TEST(random_floats_are_uniform_in_zero_one) {
    Pcg32 r; r.seed(1, 2);
    const int N = 1000000;
    double sum = 0, sum2 = 0; int bins[10] = {0}; float lo = 1, hi = 0;
    for (int i = 0; i < N; i++) {
        float x = r.nextFloat();
        lo = std::min(lo, x); hi = std::max(hi, x);
        sum += x; sum2 += (double)x * x;
        bins[(int)(x * 10)]++;
    }
    CHECK(lo >= 0.0f && hi < 1.0f);                         // [0, 1): never exactly 1
    CHECK_NEAR(sum / N, 0.5, 0.002);                        // standard error of the mean is 0.0003
    CHECK_NEAR(sum2 / N - (sum / N) * (sum / N), 1.0 / 12.0, 0.001);
    for (int b = 0; b < 10; b++) CHECK(std::abs(bins[b] - N / 10) < 1500);   // each bin: 100000 +- 300 (sigma)
}

TEST(neighbouring_pixels_get_uncorrelated_streams) {
    const int N = 100000;
    Sampler a(1, 1000, 0), b(1, 1001, 0), c(1, 1000, 1);
    double sa = 0, sb = 0, sc = 0, saa = 0, sbb = 0, scc = 0, sab = 0, sac = 0;
    for (int i = 0; i < N; i++) {
        double x = a.next1D(), y = b.next1D(), z = c.next1D();
        sa += x; sb += y; sc += z; saa += x * x; sbb += y * y; scc += z * z; sab += x * y; sac += x * z;
    }
    auto corr = [&](double s1, double s2, double s11, double s22, double s12) {
        double cov = s12 / N - (s1 / N) * (s2 / N);
        return cov / std::sqrt((s11 / N - (s1 / N) * (s1 / N)) * (s22 / N - (s2 / N) * (s2 / N)));
    };
    CHECK(std::fabs(corr(sa, sb, saa, sbb, sab)) < 0.01);   // pixel 1000 vs pixel 1001 (standard error ~0.003)
    CHECK(std::fabs(corr(sa, sc, saa, scc, sac)) < 0.01);   // sample 0 vs sample 1 of the same pixel
}

TEST(monte_carlo_error_shrinks_like_one_over_sqrt_n) {
    // Estimate the area of a step function, f(x) = 1 for x < 0.3 (true value 0.3), with N uniform samples.
    // The root-mean-square error should be sqrt(0.3 * 0.7 / N): 4x smaller when N grows 16x.
    auto rmsError = [&](int n, int trials) {
        double acc = 0;
        for (int t = 0; t < trials; t++) {
            Sampler smp(99, (uint64_t)t, (uint32_t)n);
            int hits = 0;
            for (int i = 0; i < n; i++) hits += smp.next1D() < 0.3f;
            double e = (double)hits / n - 0.3;
            acc += e * e;
        }
        return std::sqrt(acc / trials);
    };
    double e16 = rmsError(16, 4000), e256 = rmsError(256, 4000);
    CHECK_NEAR(e16, std::sqrt(0.21 / 16), 0.004);
    CHECK_NEAR(e256, std::sqrt(0.21 / 256), 0.001);
    CHECK_NEAR(e16 / e256, 4.0, 0.3);
}

TEST(pfm_round_trip_and_rmse) {
    Film a(4, 3), b(4, 3);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 4; x++) { a.add(x, y, Vec3(1.0f + x, 2.0f, 4.0f + y)); b.add(x, y, Vec3(1.0f + x, 2.5f, 4.0f + y)); }
    CHECK(writePFM("test_a.pfm", a) && writePFM("test_b.pfm", b));
    int w, h; std::vector<float> d;
    CHECK(readPFM("test_a.pfm", w, h, d) && w == 4 && h == 3);
    CHECK(d.size() == 36);
    double r = -1, self = -1;
    CHECK(rmsePFM("test_a.pfm", "test_b.pfm", r));
    CHECK(rmsePFM("test_a.pfm", "test_a.pfm", self));
    CHECK_NEAR(r, std::sqrt(0.25 / 3.0), 1e-9);        // only the green channel differs, by 0.5 -> sqrt(0.5^2 / 3 channels)
    CHECK(self == 0.0);
    std::remove("test_a.pfm"); std::remove("test_b.pfm");
}

// ---------------------------------------------------------------- render determinism
static bool sameFilm(const Film& a, const Film& b) {
    for (size_t i = 0; i < a.sum.size(); i++)
        if (a.sum[i].x != b.sum[i].x || a.sum[i].y != b.sum[i].y || a.sum[i].z != b.sum[i].z || a.count[i] != b.count[i]) return false;
    return true;
}

TEST(render_does_not_depend_on_thread_count_or_seed_without_jitter) {
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Room_2Spheres.glb");
    applyDefaultLighting(sc);
    Camera cam = makeCamera(sc);
    RenderSettings rs; rs.spp = 1; rs.jitter = false;
    Film a(WIDTH, HEIGHT), b(WIDTH, HEIGHT), c(WIDTH, HEIGHT);
    Stats st;
    rs.threads = 1; rs.seed = 1; renderFrame(sc, cam, a, rs, nullptr, nullptr, st);
    rs.threads = 5; rs.seed = 1; renderFrame(sc, cam, b, rs, nullptr, nullptr, st);
    rs.threads = 5; rs.seed = 12345; renderFrame(sc, cam, c, rs, nullptr, nullptr, st);
    CHECK(sameFilm(a, b));      // thread count never changes the result
    CHECK(sameFilm(a, c));      // nothing is random yet, so the seed has no effect either
}

TEST(jittered_render_is_deterministic_per_seed_and_independent_of_threads) {
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Room_2Spheres.glb");
    applyDefaultLighting(sc);
    Camera cam = makeCamera(sc);
    RenderSettings rs; rs.spp = 2; rs.jitter = true;
    Film a(WIDTH, HEIGHT), b(WIDTH, HEIGHT), c(WIDTH, HEIGHT), centre(WIDTH, HEIGHT);
    Stats st;
    rs.threads = 1; rs.seed = 1; renderFrame(sc, cam, a, rs, nullptr, nullptr, st);
    rs.threads = 7; rs.seed = 1; renderFrame(sc, cam, b, rs, nullptr, nullptr, st);
    rs.threads = 7; rs.seed = 2; renderFrame(sc, cam, c, rs, nullptr, nullptr, st);
    CHECK(sameFilm(a, b));      // same seed -> identical film, 1 thread or 7
    CHECK(!sameFilm(a, c));     // a different seed really changes the random samples
    CHECK(a.count[0] == 2 && a.count[WIDTH * HEIGHT - 1] == 2);   // two samples per pixel were stored
    rs.jitter = false; rs.spp = 1; rs.threads = 4; renderFrame(sc, cam, centre, rs, nullptr, nullptr, st);
    // Jitter moves every sample a little, so smooth areas change slightly everywhere (measured: ~99% of pixels, mean
    // difference ~0.002) while the large changes (> 0.1, measured ~1000 pixels) happen only along object edges.
    int tiny = 0, large = 0; double meanDiff = 0;
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++) {
            Vec3 p = a.pixel(x, y), q = centre.pixel(x, y);
            float d = std::max(std::fabs(p.x - q.x), std::max(std::fabs(p.y - q.y), std::fabs(p.z - q.z)));
            tiny += d > 0; large += d > 0.1f; meanDiff += d;
        }
    CHECK(tiny > WIDTH * HEIGHT * 9 / 10);
    CHECK(large > 300 && large < 4000);
    CHECK(meanDiff / (WIDTH * HEIGHT) < 0.01);
}

// ---------------------------------------------------------------- render job (what the GUI's Render button drives)
static RenderParams jobParams(int spp = 1) {
    RenderParams p;
    p.scene = std::string(PROJECT_DIR) + "/Scene/Test/Room_2Spheres.glb";
    p.spp = spp; p.threads = 4;
    p.lightMode = LightMode::Point;     // the tests below compare with direct renders, which default to the point light
    return p;
}

TEST(render_job_matches_a_direct_render_and_can_be_restarted) {
    RenderJob job;
    CHECK(job.state() == RenderJob::State::Idle);
    CHECK(job.start(jobParams(1)));
    job.wait();
    CHECK(job.state() == RenderJob::State::Done);
    CHECK(job.rowsDone() == HEIGHT);
    CHECK(job.renderMs() > 0 && job.stats().rays > 100000);
    // same settings rendered directly give the identical film
    Scene sc = loadScene(jobParams().scene);
    applyDefaultLighting(sc);
    Camera cam = makeCamera(sc);
    RenderSettings rs; rs.threads = 4; rs.spp = 1;
    Film direct(WIDTH, HEIGHT); Stats st;
    renderFrame(sc, cam, direct, rs, nullptr, nullptr, st);
    CHECK(sameFilm(*job.film(), direct));
    // restart with other settings: fresh film, new sample count, scene is not loaded again
    std::shared_ptr<Scene> sceneBefore = job.scene();
    RenderParams p2 = jobParams(2); p2.jitter = true;
    CHECK(job.start(p2));
    job.wait();
    CHECK(job.runs() == 2 && job.state() == RenderJob::State::Done);
    CHECK(job.film()->count[0] == 2);
    CHECK(job.scene() == sceneBefore);
}

TEST(render_job_refuses_to_start_while_busy_and_can_be_cancelled) {
    RenderJob job;
    CHECK(job.start(jobParams(64)));              // several seconds of work
    CHECK(!job.start(jobParams(1)));              // a second start is refused while the first one runs
    job.cancel();
    job.wait();
    CHECK(job.state() == RenderJob::State::Cancelled);
    CHECK(job.rowsDone() < HEIGHT);               // it stopped early
    CHECK(job.runs() == 1);
    CHECK(job.start(jobParams(1)));               // and a new render can start afterwards
    job.wait();
    CHECK(job.state() == RenderJob::State::Done && job.rowsDone() == HEIGHT);
}

TEST(render_job_reports_a_missing_scene_and_recovers) {
    RenderJob job;
    RenderParams bad = jobParams(); bad.scene = std::string(PROJECT_DIR) + "/Scene/Test/does_not_exist.glb";
    CHECK(job.start(bad));
    job.wait();
    CHECK(job.state() == RenderJob::State::Error);
    CHECK(job.error().find("does_not_exist.glb") != std::string::npos);
    CHECK(job.start(jobParams()));                // the next render with a valid scene works
    job.wait();
    CHECK(job.state() == RenderJob::State::Done && job.error().empty());
}

TEST(render_job_applies_max_bounces) {
    RenderJob a, b;
    RenderParams p0 = jobParams(); p0.maxDepth = 0;
    RenderParams p6 = jobParams(); p6.maxDepth = 6;
    CHECK(a.start(p0)); CHECK(b.start(p6));
    a.wait(); b.wait();
    CHECK(a.scene()->maxDepth == 0 && b.scene()->maxDepth == 6);
    CHECK(!sameFilm(*a.film(), *b.film()));      // no bounces: mirror and glass spheres lose their reflections / refractions
    CHECK(a.stats().rays < b.stats().rays);
}

// ---------------------------------------------------------------- area light (M1 step 3)
// A white diffuse floor (y = 0, normal +y) and a 2 x 2 m rectangular light 5 m above its middle, facing down.
static Scene areaFloorScene(float radiance = 10.0f, bool facingDown = true) {
    Scene sc;
    sc.ambient = Vec3(0, 0, 0);
    sc.materials = {diffuseMat({1, 1, 1})};
    sc.tris.push_back(makeTri({-50, 0, -50}, {0, 0, 50}, {50, 0, -50}));
    AreaLight a;
    a.valid = true;
    a.center = Vec3(0, 5, 0);
    a.edgeU = Vec3(2, 0, 0); a.edgeV = Vec3(0, 0, 2);
    a.normal = Vec3(0, facingDown ? -1.0f : 1.0f, 0);
    a.area = 4.0f; a.radiance = radiance;
    sc.area = sc.areaFile = a;
    sc.light = {a.center, Vec3(1, 1, 1)};
    sc.lightMode = LightMode::Area;
    sc.buildBvh();
    return sc;
}
static const Ray kDownAtOrigin{{0, 2, 0}, {0, -1, 0}};      // starts below the blockers used in the tests (y = 2.5)

// Mean of `n` area-light shading samples at the floor point under the light (first channel).
static double meanAreaShade(const Scene& sc, int n, uint64_t seed = 5) {
    double sum = 0;
    for (int i = 0; i < n; i++) { Sampler smp(seed, 0, (uint32_t)i); sum += trace(sc, kDownAtOrigin, 0, smp).x; }
    return sum / n;
}

TEST(area_light_estimator_is_unbiased_against_a_quadrature_of_point_lights) {
    // The area light is the sum of many tiny point lights. Build that sum independently: a 100 x 100 grid of
    // point lights, each with the weight radiance * area * cos(theta_light) / d^2 (written out here again),
    // shaded by the point-light code path. The random estimate must converge to it.
    Scene area = areaFloorScene();
    Scene point = areaFloorScene(); point.lightMode = LightMode::Point;
    Sampler smp(1, 0, 0);
    const int G = 100;
    double quad = 0;
    for (int i = 0; i < G; i++)
        for (int j = 0; j < G; j++) {
            Vec3 p(-1 + 2 * (i + 0.5f) / G, 5, -1 + 2 * (j + 0.5f) / G);
            float d2 = p.x * p.x + 25 + p.z * p.z;
            float cosLight = 5.0f / std::sqrt(d2);                     // light faces straight down
            float w = 10.0f * 4.0f * cosLight / d2;
            point.light = {p, Vec3(w, w, w)};
            quad += trace(point, kDownAtOrigin, 0, smp).x;
        }
    quad /= G * G;
    double est = meanAreaShade(area, 40000);
    CHECK(quad > 1.0);                                             // sanity: the floor really is lit (radiance*area/d^2 = 1.6)
    CHECK_NEAR(est / quad, 1.0, 0.01);
}

TEST(area_light_shrinks_to_the_point_light) {
    // A 1 cm light of the same total power gives the point light's value: radiance * area * cos / d^2 at the centre.
    Scene sc = areaFloorScene(10.0f);
    configureLight(sc, LightMode::Area, 0.01f);
    Scene point = areaFloorScene(10.0f); point.lightMode = LightMode::Point;
    float w = 10.0f * 4.0f / 25.0f;                                // cos = 1, d^2 = 25, power = radiance * area = 40
    point.light = {Vec3(0, 5, 0), Vec3(w, w, w)};
    Sampler smp(1, 0, 0);
    double pointValue = trace(point, kDownAtOrigin, 0, smp).x;
    CHECK_NEAR(meanAreaShade(sc, 200) / pointValue, 1.0, 1e-3);
}

TEST(area_light_half_blocked_gives_half_the_light) {
    // A blocker plane at y = 2.5 covering x > 0: from the floor point below the light it hides exactly the
    // half of the rectangle with x > 0 (rays cross y = 2.5 at half the light's x).
    Scene open = areaFloorScene();
    Scene half = areaFloorScene();
    half.materials.push_back(diffuseMat({0.2f, 0.2f, 0.2f}));
    half.tris.push_back(makeTri({0, 2.5f, -40}, {0, 2.5f, 40}, {40, 2.5f, 0}, 1));
    half.buildBvh();
    Scene covered = areaFloorScene();
    covered.materials.push_back(diffuseMat({0.2f, 0.2f, 0.2f}));
    covered.tris.push_back(makeTri({-40, 2.5f, -40}, {0, 2.5f, 40}, {40, 2.5f, -40}, 1));
    covered.buildBvh();
    double full = meanAreaShade(open, 20000);
    CHECK_NEAR(meanAreaShade(half, 20000) / full, 0.5, 0.02);      // penumbra: half of the light is visible
    CHECK(meanAreaShade(covered, 2000) == 0.0);                    // umbra: nothing reaches the floor
}

TEST(area_light_emits_from_its_front_side_only) {
    Scene lit = areaFloorScene(10.0f, true), back = areaFloorScene(10.0f, false);
    CHECK(meanAreaShade(lit, 100) > 1.0);
    CHECK(meanAreaShade(back, 100) == 0.0);                        // the floor is behind the light: no light, ambient is 0
}

TEST(area_light_resizing_keeps_the_total_power) {
    Scene sc = areaFloorScene(10.0f);
    for (float size : {0.5f, 2.0f, 6.0f}) {
        configureLight(sc, LightMode::Area, size);
        CHECK_NEAR(length(sc.area.edgeU), size, 1e-5); CHECK_NEAR(length(sc.area.edgeV), size, 1e-5);
        CHECK_NEAR(sc.area.area, size * size, 1e-4);
        CHECK_NEAR(sc.area.radiance * sc.area.area, 40.0, 1e-3);   // power = radiance * area stays 10 * 4
        CHECK_NEAR(sc.area.center.y, 5.0, 1e-6);                   // same centre
    }
    configureLight(sc, LightMode::Area, 0);                         // 0 = back to the file's light
    CHECK_NEAR(sc.area.area, 4.0, 1e-6); CHECK_NEAR(sc.area.radiance, 10.0, 1e-6);
}

TEST(glb_area_light_is_calibrated_to_the_old_point_light) {
    // rt_radiance in the glb was chosen so that the middle of the room floor gets, averaged over the whole rectangle,
    // the same light as the M0 point light (colour 1): the mean weight there should be about 1.
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Room_2Spheres.glb");
    configureLight(sc, LightMode::Area, 0);
    double sum = 0;
    const int N = 5000;
    for (int i = 0; i < N; i++) { Sampler smp(3, 0, (uint32_t)i); sum += sampleLight(sc, Vec3(0, 0, 0), smp).weight.x; }
    CHECK_NEAR(sum / N, 1.0, 0.01);
}

TEST(area_light_render_is_deterministic_and_independent_of_threads) {
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Room_2Spheres.glb");
    applyDefaultLighting(sc);
    configureLight(sc, LightMode::Area, 0);
    Camera cam = makeCamera(sc);
    RenderSettings rs; rs.spp = 2; rs.jitter = false;
    Film a(WIDTH, HEIGHT), b(WIDTH, HEIGHT), c(WIDTH, HEIGHT);
    Stats st;
    rs.threads = 1; rs.seed = 1; renderFrame(sc, cam, a, rs, nullptr, nullptr, st);
    rs.threads = 6; rs.seed = 1; renderFrame(sc, cam, b, rs, nullptr, nullptr, st);
    rs.threads = 6; rs.seed = 2; renderFrame(sc, cam, c, rs, nullptr, nullptr, st);
    CHECK(sameFilm(a, b));          // random light samples, still independent of the thread count
    CHECK(!sameFilm(a, c));         // and different with another seed (even without pixel jitter)
}

TEST(render_job_area_mode_matches_a_direct_area_render) {
    RenderJob job;
    RenderParams p = jobParams(2); p.lightMode = LightMode::Area; p.lightSize = 3.0f; p.seed = 9;
    CHECK(job.start(p));
    job.wait();
    Scene sc = loadScene(p.scene);
    applyDefaultLighting(sc);
    configureLight(sc, LightMode::Area, 3.0f);
    Camera cam = makeCamera(sc);
    RenderSettings rs; rs.threads = 3; rs.spp = 2; rs.seed = 9;
    Film direct(WIDTH, HEIGHT); Stats st;
    renderFrame(sc, cam, direct, rs, nullptr, nullptr, st);
    CHECK(sameFilm(*job.film(), direct));
}

// ---------------------------------------------------------------- silhouettes of smooth-shaded spheres
// A UV sphere made of flat triangles with smooth (outward) vertex normals, like the spheres in the glb files.
static void addSphere(Scene& sc, Vec3 c, float r, int material, int nu = 48, int nv = 24) {
    auto pt = [&](int iu, int iv) {
        float th = 3.14159265f * iv / nv, ph = 2 * 3.14159265f * iu / nu;
        return Vec3(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
    };
    for (int iv = 0; iv < nv; iv++)
        for (int iu = 0; iu < nu; iu++) {
            Vec3 a = pt(iu, iv), b = pt(iu + 1, iv), cc = pt(iu, iv + 1), d = pt(iu + 1, iv + 1);
            auto tri = [&](Vec3 u, Vec3 v, Vec3 w) {
                Triangle t;
                t.p0 = c + u * r; t.p1 = c + v * r; t.p2 = c + w * r;
                t.n0 = u; t.n1 = v; t.n2 = w;
                t.material = material;
                if (length(cross(t.p1 - t.p0, t.p2 - t.p0)) > 1e-9f) sc.tris.push_back(t);   // skip degenerate pole triangles
            };
            tri(a, b, cc); tri(cc, b, d);
        }
}

static Scene sphereScene(bool glass, bool legacy) {
    Scene sc;
    Material m;
    if (glass) { m.base = Vec3(1, 1, 1); m.metallic = 0; m.roughness = 0; m.transmission = 1; }
    else { m.base = Vec3(0.8f, 0.02f, 0.02f); m.metallic = 1; m.roughness = 0.05f; }
    sc.materials = {m};
    addSphere(sc, Vec3(0, 0, 0), 1.0f, 0);
    sc.buildBvh();
    sc.ambient = Vec3(0, 0, 0);
    sc.legacyNormals = legacy;
    return sc;
}

// Parallel rays in a generic direction (not aligned with the mesh's axes) that pass at distance b from the sphere's
// centre (b -> 1 is the outline).
static int countDark(const Scene& sc, float bMin, float bMax, int n, bool exactlyBlack) {
    const Vec3 d = normalize(Vec3(0.3f, 0.45f, -0.84f));
    const Vec3 u = normalize(cross(d, Vec3(0, 1, 0))), v = cross(u, d);
    int dark = 0;
    for (int i = 0; i < n; i++) {
        Sampler smp(11, (uint64_t)i, 0);
        float b = bMin + (bMax - bMin) * smp.next1D(), phi = 2 * 3.14159265f * smp.next1D();
        Vec3 o = d * -5.0f + u * (b * std::cos(phi)) + v * (b * std::sin(phi));
        Vec3 c = trace(sc, Ray{o, d}, 0, smp);
        dark += exactlyBlack ? (c.x == 0 && c.y == 0 && c.z == 0) : (c.x + c.y + c.z < 1e-3f);
    }
    return dark;
}

TEST(mirror_sphere_outline_does_not_turn_black) {
    // Legacy: near the outline the interpolated normal can face away from the viewer, the renderer flips it and the
    // reflection ray runs inside the sphere until the depth limit (colour 0). Fixed: no such rays.
    CHECK(countDark(sphereScene(false, true), 0.99f, 0.99999f, 40000, true) > 0);      // the bug is reproducible in legacy mode
    CHECK(countDark(sphereScene(false, false), 0.99f, 0.99999f, 40000, true) == 0);
}

TEST(glass_sphere_outline_does_not_turn_black) {
    CHECK(countDark(sphereScene(true, true), 0.99f, 0.99999f, 40000, false) > 0);
    CHECK(countDark(sphereScene(true, false), 0.99f, 0.99999f, 40000, false) == 0);
}

TEST(silhouette_fix_leaves_rays_away_from_the_outline_unchanged) {
    const Vec3 d = normalize(Vec3(0.3f, 0.45f, -0.84f));
    const Vec3 u = normalize(cross(d, Vec3(0, 1, 0))), v = cross(u, d);
    for (bool glass : {false, true}) {
        Scene legacy = sphereScene(glass, true), fixed = sphereScene(glass, false);
        int same = 0, n = 5000;
        for (int i = 0; i < n; i++) {
            Sampler s1(21, (uint64_t)i, 0), s2(21, (uint64_t)i, 0);
            float b = 0.7f * s1.next1D(), phi = 2 * 3.14159265f * s1.next1D();
            s2.next1D(); s2.next1D();
            Ray r{d * -5.0f + u * (b * std::cos(phi)) + v * (b * std::sin(phi)), d};
            Vec3 x = trace(legacy, r, 0, s1), y = trace(fixed, r, 0, s2);
            same += (x.x == y.x && x.y == y.y && x.z == y.z);
        }
        CHECK(same == n);       // bit for bit, inside 70% of the sphere's radius
    }
}

TEST(geometric_normal_follows_the_vertex_normals_not_the_winding) {
    for (int flipped = 0; flipped < 2; flipped++) {
        Scene sc;
        sc.materials = {diffuseMat({1, 1, 1})};
        Triangle t = flipped ? makeTri({-5, 0, -5}, {5, 0, -5}, {0, 0, 5}) : makeTri({-5, 0, -5}, {0, 0, 5}, {5, 0, -5});
        t.n0 = t.n1 = t.n2 = Vec3(0, 1, 0);                        // vertex normals say "up" whatever the winding is
        sc.tris.push_back(t);
        sc.buildBvh();
        Hit h;
        CHECK(sc.intersect(Ray{{0, 3, 0}, {0, -1, 0}}, INF, h));
        CHECK_NEAR(length(h.ng), 1.0, 1e-5);
        CHECK(dot(h.ng, h.n) > 0.99f);                             // same side as the shading normal
    }
}

// ---------------------------------------------------------------- path tracing (M2)
TEST(cosine_hemisphere_sampling_has_the_cosine_distribution) {
    Sampler smp(3, 0, 0);
    const int n = 200000;
    for (Vec3 normal : {Vec3(0, 1, 0), normalize(Vec3(1, 2, -0.5f)), Vec3(1, 0, 0)}) {
        double c1 = 0, c2 = 0; int below = 0;
        for (int i = 0; i < n; i++) {
            Vec3 d = sampleCosineHemisphere(normal, smp.next1D(), smp.next1D());
            float c = dot(d, normal);
            below += c < 0; c1 += c; c2 += c * c;
            if (i == 0) CHECK_NEAR(length(d), 1.0, 1e-5);
        }
        CHECK(below == 0);
        CHECK_NEAR(c1 / n, 2.0 / 3.0, 0.005);     // E[cos] for pdf cos/pi
        CHECK_NEAR(c2 / n, 0.5, 0.005);           // E[cos^2]
    }
}

TEST(area_light_intersection_is_one_sided_and_bounded) {
    AreaLight a; a.valid = true;
    a.center = Vec3(0, 5, 0); a.edgeU = Vec3(2, 0, 0); a.edgeV = Vec3(0, 0, 2); a.normal = Vec3(0, -1, 0);
    CHECK_NEAR(intersectAreaLight(a, Ray{{0, 0, 0}, {0, 1, 0}}), 5.0, 1e-5);       // from below, the emitting side
    CHECK_NEAR(intersectAreaLight(a, Ray{{0.9f, 0, -0.9f}, {0, 1, 0}}), 5.0, 1e-5);
    CHECK(intersectAreaLight(a, Ray{{1.1f, 0, 0}, {0, 1, 0}}) < 0);                 // beside the rectangle
    CHECK(intersectAreaLight(a, Ray{{0, 9, 0}, {0, -1, 0}}) < 0);                   // from above: the back emits nothing
    CHECK(intersectAreaLight(a, Ray{{0, 6, 0}, {0, 1, 0}}) < 0);                    // rectangle behind the origin
}

// A floor of albedo rho under a huge light facing down: the form factor is ~1, so radiance leaving the floor is
// rho * Le after one bounce, and (the light is the only thing above) nothing more however many bounces are allowed.
static Scene pathFloorScene(float rho, float Le) {
    Scene sc;
    sc.materials = {diffuseMat({rho, rho, rho})};
    sc.tris.push_back(makeTri({-50, 0, -50}, {0, 0, 50}, {50, 0, -50}));
    AreaLight a; a.valid = true; a.center = Vec3(0, 1, 0);
    a.edgeU = Vec3(2000, 0, 0); a.edgeV = Vec3(0, 0, 2000); a.normal = Vec3(0, -1, 0); a.area = 4e6f; a.radiance = Le;
    sc.area = sc.areaFile = a;
    sc.integrator = Integrator::Path;
    sc.buildBvh();
    return sc;
}
static double meanPath(const Scene& sc, const Ray& r, int n) {
    double sum = 0;
    for (int i = 0; i < n; i++) { Sampler smp(11, 0, (uint32_t)i); sum += pathTrace(sc, r, smp).x; }
    return sum / n;
}

TEST(path_tracer_matches_the_analytic_radiance_of_a_floor_under_a_huge_light) {
    Scene sc = pathFloorScene(0.5f, 2.0f);
    sc.nee = false;
    Ray down{{0, 0.5f, 0}, {0, -1, 0}};
    sc.maxDepth = 0; CHECK_NEAR(meanPath(sc, down, 1000), 0.0, 1e-9);             // 0 bounces: the floor itself does not emit
    sc.maxDepth = 1; CHECK_NEAR(meanPath(sc, down, 20000), 0.5 * 2.0 * 0.999, 0.01);   // rho * Le * form factor
    sc.maxDepth = 6; CHECK_NEAR(meanPath(sc, down, 20000), 0.5 * 2.0 * 0.999, 0.01);   // the extra bounces find nothing new
    CHECK_NEAR(meanPath(sc, Ray{{0, 0.5f, 0}, {0, 1, 0}}, 10), 2.0, 1e-5);        // looking straight at the light sees Le
    sc.envScale = 0; Ray away{{0, 0.5f, 0}, {1, 0, 0}};
    CHECK_NEAR(meanPath(sc, away, 10), 0.0, 1e-9);                                 // escapes into a black sky
    sc.envScale = 1; CHECK(meanPath(sc, away, 10) > 0.3);                          // with sky light it sees the gradient
}

TEST(path_tracer_adds_light_with_every_allowed_bounce) {
    // On the Cornell box: the radiance seen on the floor grows with the bounce limit, and is 0 at 0 bounces.
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Cornell.glb");
    configureLight(sc, LightMode::Area, 0);
    sc.integrator = Integrator::Path;
    Camera cam = makeCamera(sc);
    Ray r = cam.ray(WIDTH / 2.0f, HEIGHT * 0.9f);                                 // floor in front of the boxes
    double prev = -1;
    for (int depth : {0, 1, 2, 4}) {
        sc.maxDepth = depth;
        double m = meanPath(sc, r, 20000);
        CHECK(m >= prev - 0.002);                                                  // more bounces never lose light
        if (depth == 0) CHECK_NEAR(m, 0.0, 1e-9);
        if (depth == 2) CHECK(m > prev * 1.05);                                    // and indirect light is really added
        prev = m;
    }
}

TEST(path_render_is_deterministic_and_independent_of_threads) {
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Cornell.glb");
    configureLight(sc, LightMode::Area, 0);
    sc.integrator = Integrator::Path; sc.maxDepth = 3;
    Camera cam = makeCamera(sc);
    RenderSettings rs; rs.spp = 1; rs.jitter = true;
    Film a(WIDTH, HEIGHT), b(WIDTH, HEIGHT), c(WIDTH, HEIGHT);
    Stats st;
    rs.threads = 1; rs.seed = 4; renderFrame(sc, cam, a, rs, nullptr, nullptr, st);
    rs.threads = 5; rs.seed = 4; renderFrame(sc, cam, b, rs, nullptr, nullptr, st);
    rs.threads = 5; rs.seed = 5; renderFrame(sc, cam, c, rs, nullptr, nullptr, st);
    CHECK(sameFilm(a, b));
    CHECK(!sameFilm(a, c));
}

TEST(cornell_box_shows_colour_bleeding_only_with_bounces) {
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Cornell.glb");
    configureLight(sc, LightMode::Area, 0);
    sc.integrator = Integrator::Path;
    // A point on the white back wall near the red left wall: indirect light from the red wall tints it red.
    Ray r{{-0.8f, 1.0f, 3.0f}, normalize(Vec3(0, 0, -1))};
    sc.maxDepth = 1; Vec3 one(0, 0, 0), four(0, 0, 0);
    const int n = 40000;
    for (int i = 0; i < n; i++) { Sampler s(2, 0, (uint32_t)i); one += pathTrace(sc, r, s); }
    sc.maxDepth = 6;
    for (int i = 0; i < n; i++) { Sampler s(2, 0, (uint32_t)i); four += pathTrace(sc, r, s); }
    CHECK(four.x > one.x);                                              // more light with more bounces
    CHECK(four.x / four.y > one.x / one.y);                             // and redder: red/green ratio grows
}

// Mean and standard error of the first channel over n path samples.
static void meanAndSe(const Scene& sc, const Ray& r, int n, double& mean, double& se, uint64_t seed = 21) {
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { Sampler smp(seed, 0, (uint32_t)i); double x = pathTrace(sc, r, smp).x; s1 += x; s2 += x * x; }
    mean = s1 / n; se = std::sqrt(std::max(0.0, s2 / n - mean * mean) / n);
}

TEST(nee_matches_a_quadrature_of_the_floor_radiance) {
    // Floor (albedo 0.5) under a 20 m x 20 m light (Le = 2) at height 1: radiance = rho/pi * Le * integral of
    // cos(x) cos(light) / d^2 dA, computed here on a fine grid.
    Scene sc = pathFloorScene(0.5f, 2.0f);
    AreaLight& a = sc.area; a.edgeU = Vec3(20, 0, 0); a.edgeV = Vec3(0, 0, 20); a.area = 400;
    sc.areaFile = a;
    double ff = 0; const int g = 1000;
    for (int i = 0; i < g; i++) for (int j = 0; j < g; j++) {
        double x = (i + 0.5) / g * 20 - 10, z = (j + 0.5) / g * 20 - 10, d2 = x * x + z * z + 1;
        ff += (1.0 / d2) * (1.0 / d2) / 3.14159265358979 * (400.0 / (g * g));       // cos(x) = cos(light) = 1/d
    }
    double expected = 0.5 * 2.0 * ff;
    sc.nee = true;
    Ray down{{0, 0.5f, 0}, {0, -1, 0}};
    double m, se;
    sc.maxDepth = 0; meanAndSe(sc, down, 100, m, se); CHECK_NEAR(m, 0.0, 1e-9);
    sc.maxDepth = 1; meanAndSe(sc, down, 40000, m, se); CHECK(std::fabs(m - expected) < 4 * se + 1e-4);
    sc.maxDepth = 5; meanAndSe(sc, down, 40000, m, se); CHECK(std::fabs(m - expected) < 4 * se + 1e-4);   // counted once, not once per bounce
    CHECK_NEAR(meanPath(sc, Ray{{0, 0.5f, 0}, {0, 1, 0}}, 10), 2.0, 1e-5);          // the camera ray still sees the emitter
}

TEST(nee_and_plain_path_tracing_have_the_same_expectation_at_every_bounce_limit) {
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Cornell.glb");
    configureLight(sc, LightMode::Area, 0);
    sc.integrator = Integrator::Path;
    Camera cam = makeCamera(sc);
    for (Ray r : {cam.ray(WIDTH / 2.0f, HEIGHT * 0.9f),        // floor
                  cam.ray(WIDTH * 0.2f, HEIGHT * 0.5f),        // red wall
                  cam.ray(WIDTH / 2.0f, HEIGHT * 0.15f)}) {    // ceiling / back wall
        for (int depth : {1, 2, 3}) {
            sc.maxDepth = depth;
            double mp, sp, mn, sn;
            sc.nee = false; meanAndSe(sc, r, 60000, mp, sp);
            sc.nee = true;  meanAndSe(sc, r, 60000, mn, sn);
            CHECK(std::fabs(mp - mn) < 5 * std::sqrt(sp * sp + sn * sn) + 1e-4);          // agree within 5 standard errors
        }
    }
}

TEST(nee_has_much_less_noise_than_plain_path_tracing) {
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Cornell.glb");
    configureLight(sc, LightMode::Area, 0);
    sc.integrator = Integrator::Path; sc.maxDepth = 1;
    Camera cam = makeCamera(sc);
    Ray r = cam.ray(WIDTH / 2.0f, HEIGHT * 0.9f);
    double mp, sp, mn, sn;
    sc.nee = false; meanAndSe(sc, r, 20000, mp, sp);
    sc.nee = true;  meanAndSe(sc, r, 20000, mn, sn);
    CHECK(sn * 3 < sp);                                                                  // standard error at least 3x smaller
}

// ---------------------------------------------------------------- progressive rendering (M3)
TEST(progressive_schedule_is_1_4_16_64_and_ends_exactly_at_the_target) {
    std::vector<int> totals;
    for (int done = 0; done < 100;) { done = nextPassTotal(done, 100); totals.push_back(done); }
    CHECK(totals == (std::vector<int>{1, 4, 16, 64, 100}));
    totals.clear();
    for (int done = 0; done < 64;) { done = nextPassTotal(done, 64); totals.push_back(done); }
    CHECK(totals == (std::vector<int>{1, 4, 16, 64}));
    CHECK(nextPassTotal(0, 1) == 1);
    CHECK(nextPassTotal(5, 8) == 8);        // continuing from an arbitrary total never overshoots
}

TEST(rendering_in_passes_gives_exactly_the_film_of_a_single_pass) {
    Scene sc = loadScene(std::string(PROJECT_DIR) + "/Scene/Test/Cornell.glb");
    configureLight(sc, LightMode::Area, 0);
    sc.integrator = Integrator::Path; sc.maxDepth = 2;
    Camera cam = makeCamera(sc);
    RenderSettings rs; rs.threads = 4; rs.jitter = true; rs.seed = 8;
    Film whole(WIDTH, HEIGHT), parts(WIDTH, HEIGHT);
    Stats st;
    rs.spp = 5; renderFrame(sc, cam, whole, rs, nullptr, nullptr, st);
    for (auto range : {std::pair<int, int>{0, 1}, {1, 4}, {4, 5}}) {
        rs.firstSample = range.first; rs.spp = range.second - range.first;
        renderFrame(sc, cam, parts, rs, nullptr, nullptr, st);
    }
    CHECK(sameFilm(whole, parts));          // bit for bit: the same samples are summed in the same order
    CHECK(parts.count[0] == 5);
}

TEST(progressive_render_job_publishes_every_pass_and_equals_a_direct_render) {
    RenderParams p = jobParams(20); p.progressive = true; p.jitter = true; p.lightMode = LightMode::Area; p.seed = 3;
    RenderJob job;
    CHECK(job.start(p));
    job.wait();
    CHECK(job.state() == RenderJob::State::Done);
    CHECK(job.doneSpp() == 20 && job.targetSpp() == 20);
    CHECK(job.passes() == 4);                                    // 1, 4, 16, 20
    auto log = job.passLog();
    CHECK(log.size() == 4 && log[0].spp == 1 && log[1].spp == 4 && log[2].spp == 16 && log[3].spp == 20);
    CHECK(log[0].ms <= log[1].ms && log[1].ms <= log[2].ms && log[2].ms <= log[3].ms);
    std::vector<Vec3> snap;
    CHECK(job.copySnapshot(snap) == 4 && snap.size() == (size_t)WIDTH * HEIGHT);
    bool snapMatches = true;
    for (int y = 0; y < HEIGHT && snapMatches; y += 7) for (int x = 0; x < WIDTH; x += 5) {
        Vec3 a = snap[(size_t)y * WIDTH + x], b = job.film()->pixel(x, y);
        snapMatches = a.x == b.x && a.y == b.y && a.z == b.z;
    }
    CHECK(snapMatches);                                          // the published image is the film's mean

    Scene sc = loadScene(p.scene);
    applyDefaultLighting(sc);
    configureLight(sc, LightMode::Area, 0);
    Camera cam = makeCamera(sc);
    RenderSettings rs; rs.threads = 4; rs.spp = 20; rs.jitter = true; rs.seed = 3;
    Film direct(WIDTH, HEIGHT); Stats st;
    renderFrame(sc, cam, direct, rs, nullptr, nullptr, st);
    CHECK(sameFilm(*job.film(), direct));                        // passes or not, the same image
}

TEST(progressive_render_job_can_add_samples_without_resetting) {
    RenderParams p = jobParams(4); p.progressive = true; p.jitter = true; p.lightMode = LightMode::Area;
    RenderJob job;
    CHECK(!job.extend(4));                                       // nothing to extend yet
    CHECK(job.start(p));
    job.wait();
    const Film* film = job.film();
    CHECK(film->count[0] == 4 && job.runs() == 1);
    CHECK(job.extend(12));
    job.wait();
    CHECK(job.state() == RenderJob::State::Done && job.film() == film);   // same film, nothing restarted
    CHECK(job.runs() == 1 && job.doneSpp() == 16 && job.targetSpp() == 16 && film->count[0] == 16);
    RenderParams direct = p; direct.spp = 16; direct.progressive = false;
    RenderJob ref;
    CHECK(ref.start(direct));
    ref.wait();
    CHECK(sameFilm(*job.film(), *ref.film()));                   // 4 + 12 samples = 16 samples in one go
    CHECK(!job.extend(0));
}

TEST(progressive_render_job_cannot_be_extended_after_a_cancel) {
    RenderParams p = jobParams(256); p.progressive = true;
    RenderJob job;
    CHECK(job.start(p));
    CHECK(!job.extend(4));                                       // busy
    job.cancel();
    job.wait();
    CHECK(job.state() == RenderJob::State::Cancelled);
    CHECK(!job.extend(4));                                       // pixels have uneven sample counts now
    CHECK(job.doneSpp() < 256);
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
