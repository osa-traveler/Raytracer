// Whitted-style ray tracer core: glTF(.glb) scene loading, BVH, shading, camera, film.
// (Window / command line handling lives in main.cpp.)
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "png_decode.h"

static constexpr int WIDTH = 640;
static constexpr int HEIGHT = 480;
static constexpr int MAX_DEPTH = 6;
static constexpr float EPS = 1e-4f;
static constexpr float INF = 1e30f;

// ---------------------------------------------------------------- math
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3& b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(const Vec3& b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& b) const { return {x * b.x, y * b.y, z * b.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& b) { x += b.x; y += b.y; z += b.z; return *this; }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) { float l = length(a); return l > 0 ? a * (1.0f / l) : a; }
inline Vec3 mix(const Vec3& a, const Vec3& b, float t) { return a * (1 - t) + b * t; }
inline Vec3 reflect(const Vec3& d, const Vec3& n) { return d - n * (2 * dot(d, n)); }

// glTF column-major 4x4
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
    Vec3 point(const Vec3& p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    // Normal transform = inverse-transpose of the upper 3x3 (cofactor matrix is proportional to it).
    Vec3 normal(const Vec3& n) const {
        Vec3 c0{m[0], m[1], m[2]}, c1{m[4], m[5], m[6]}, c2{m[8], m[9], m[10]};
        Vec3 r0 = cross(c1, c2), r1 = cross(c2, c0), r2 = cross(c0, c1);
        float det = dot(c0, r0);
        float s = det < 0 ? -1.0f : 1.0f;
        return normalize((r0 * n.x + r1 * n.y + r2 * n.z) * s);
    }
};

inline Mat4 makeTRS(const float t[3], const float q[4], const float s[3]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 r;
    r.m[0] = (1 - 2 * (y * y + z * z)) * s[0];
    r.m[1] = (2 * (x * y + z * w)) * s[0];
    r.m[2] = (2 * (x * z - y * w)) * s[0];
    r.m[4] = (2 * (x * y - z * w)) * s[1];
    r.m[5] = (1 - 2 * (x * x + z * z)) * s[1];
    r.m[6] = (2 * (y * z + x * w)) * s[1];
    r.m[8] = (2 * (x * z + y * w)) * s[2];
    r.m[9] = (2 * (y * z - x * w)) * s[2];
    r.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
    r.m[12] = t[0]; r.m[13] = t[1]; r.m[14] = t[2];
    return r;
}

// ---------------------------------------------------------------- minimal JSON
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    double num = 0;
    bool b = false;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    bool has(const char* k) const { return find(k) != nullptr; }
    const Json* find(const char* k) const {
        for (auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    const Json& operator[](const char* k) const {
        static const Json null;
        const Json* j = find(k);
        return j ? *j : null;
    }
    const Json& operator[](int i) const { return arr.at((size_t)i); }
    size_t size() const { return arr.size(); }
    float f(float def = 0) const { return type == Num ? (float)num : def; }
    int i(int def = 0) const { return type == Num ? (int)num : def; }
};

struct JsonParser {
    const char* p;
    const char* end;
    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("JSON: ") + m); }
    Json parse() {
        ws();
        if (p >= end) fail("unexpected end");
        Json j;
        char c = *p;
        if (c == '{') {
            p++; j.type = Json::Obj; ws();
            if (*p == '}') { p++; return j; }
            for (;;) {
                ws();
                std::string k = parseString();
                ws(); if (*p++ != ':') fail("expected ':'");
                j.obj.emplace_back(std::move(k), parse());
                ws();
                if (*p == ',') { p++; continue; }
                if (*p == '}') { p++; break; }
                fail("expected ',' or '}'");
            }
        } else if (c == '[') {
            p++; j.type = Json::Arr; ws();
            if (*p == ']') { p++; return j; }
            for (;;) {
                j.arr.push_back(parse());
                ws();
                if (*p == ',') { p++; continue; }
                if (*p == ']') { p++; break; }
                fail("expected ',' or ']'");
            }
        } else if (c == '"') {
            j.type = Json::Str; j.str = parseString();
        } else if (!strncmp(p, "true", 4)) { p += 4; j.type = Json::Bool; j.b = true; }
        else if (!strncmp(p, "false", 5)) { p += 5; j.type = Json::Bool; }
        else if (!strncmp(p, "null", 4)) { p += 4; }
        else {
            char* e;
            j.num = strtod(p, &e);
            if (e == p) fail("bad token");
            p = e; j.type = Json::Num;
        }
        return j;
    }
    std::string parseString() {
        if (*p != '"') fail("expected string");
        p++;
        std::string s;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                p++;
                switch (*p) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'u': { // keep BMP escape as-is (names only; not needed for rendering)
                        s += "\\u"; break;
                    }
                    default: s += *p;
                }
                p++;
            } else s += *p++;
        }
        p++;
        return s;
    }
};

// ---------------------------------------------------------------- scene
// An 8-bit RGB image used as a texture. Lookups follow glTF: (u, v) = (0, 0) is the top-left corner of the image, v grows
// downwards, the wrap mode is repeat and the filter is bilinear (the sampler object in the file is not read).
struct Texture {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
    // Bilinear lookup, 0..1 per channel. Srgb: decode each texel from sRGB to linear *before* blending (colour textures);
    // otherwise the numbers are used as they are (roughness / metalness data).
    template <bool Srgb>
    Vec3 sample(float u, float v) const {
        static const struct Lut { float v[256]; Lut() { for (int i = 0; i < 256; i++) { float c = i / 255.0f; v[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); } } } lut;
        float x = u * w - 0.5f, y = v * h - 0.5f;
        float fx = std::floor(x), fy = std::floor(y);
        float tx = x - fx, ty = y - fy;
        auto wrap = [](int i, int n) { i %= n; return i < 0 ? i + n : i; };
        int x0 = wrap((int)fx, w), x1 = wrap((int)fx + 1, w), y0 = wrap((int)fy, h), y1 = wrap((int)fy + 1, h);
        auto texel = [&](int xi, int yi) {
            const uint8_t* p = &rgb[((size_t)yi * w + xi) * 3];
            return Srgb ? Vec3(lut.v[p[0]], lut.v[p[1]], lut.v[p[2]]) : Vec3(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f);
        };
        return mix(mix(texel(x0, y0), texel(x1, y0), tx), mix(texel(x0, y1), texel(x1, y1), tx), ty);
    }
};

struct Material {
    Vec3 base{1, 1, 1};
    float metallic = 1.0f;     // glTF default
    float roughness = 1.0f;    // glTF default
    float transmission = 0.0f;
    float ior = 1.5f;          // KHR_materials_ior (glass refraction, and the dielectric reflectance at normal incidence)
    float specular = 1.0f;     // KHR_materials_specular specularFactor: scales the dielectric reflectance (0 = no specular highlight)
    int baseColorTex = -1;     // index into Scene::textures (glTF baseColorTexture, multiplied with `base`), -1 = none
    int normalTex = -1;        // glTF normalTexture: tangent-space normal map (+X right, +Y up in the image), -1 = none
    float normalScale = 1.0f;  // glTF normalTexture.scale: multiplies the X and Y of the decoded normal
    int metalRoughTex = -1;    // glTF metallicRoughnessTexture: G = roughness, B = metalness (multiplied with the factors), -1 = none
    bool doubleSided = true;   // glTF doubleSided (the loader reads it, the default of the file format is false). Only used with Scene::cullBackfaces
    std::string name;
};

struct Triangle {
    Vec3 p0, p1, p2;
    Vec3 n0, n1, n2;
    float uv[6] = {0, 0, 0, 0, 0, 0};   // TEXCOORD_0 of the three vertices (u0, v0, u1, v1, u2, v2), glTF convention
    Vec3 tg[3];                         // vertex tangents (world space), only for materials with a normal map
    float tw[3] = {0, 0, 0};            // handedness of the tangent frame, +1 / -1; 0 = no tangent (no normal map)
    int material;
};

struct Ray { Vec3 o, d; };

struct Hit {
    float t = INF;
    Vec3 p, n;          // n: interpolated (smooth) shading normal
    Vec3 ng;            // geometric normal of the triangle, on the same side as the vertex normals
    int material = -1;
    float tu = 0, tv = 0;   // interpolated texture coordinates
    Vec3 tg;                // interpolated tangent (not yet orthogonal to n); tw = handedness, 0 = none
    float tw = 0;
};

struct PointLight { Vec3 pos; Vec3 color; };

// Per-thread counters (summed by the caller after rendering). Cheap enough to leave on.
struct Stats {
    uint64_t rays = 0;          // calls to Scene::intersect (camera, shadow, reflection, refraction)
    uint64_t nodeVisits = 0;    // BVH nodes whose box was tested
    uint64_t triTests = 0;      // ray-triangle tests
    uint64_t shadowRays = 0;    // Path mode: shadow rays of the light sampling (also counted in `rays`)
    Stats& operator+=(const Stats& o) { rays += o.rays; nodeVisits += o.nodeVisits; triTests += o.triTests; shadowRays += o.shadowRays; return *this; }
};
inline thread_local Stats tlStats;

// Moller-Trumbore ray/triangle test, double sided.
// Returns true if the ray's supporting line crosses the triangle (any t, including t <= 0);
// the caller decides which t range it accepts. u, v are barycentric coordinates of p1 and p2.
inline bool intersectTriangle(const Ray& r, const Triangle& tr, float& t, float& u, float& v) {
    Vec3 e1 = tr.p1 - tr.p0, e2 = tr.p2 - tr.p0;
    Vec3 pv = cross(r.d, e2);
    float det = dot(e1, pv);
    if (std::fabs(det) < 1e-12f) return false;   // ray parallel to the triangle plane
    float idet = 1.0f / det;
    Vec3 tv = r.o - tr.p0;
    u = dot(tv, pv) * idet;
    if (u < 0 || u > 1) return false;
    Vec3 qv = cross(tv, e1);
    v = dot(r.d, qv) * idet;
    if (v < 0 || u + v > 1) return false;
    t = dot(e2, qv) * idet;
    return true;
}

struct BvhNode {
    Vec3 bmin, bmax;
    int left = -1;       // inner node: child indices
    int right = -1;
    int first = 0;       // leaf: first index into triOrder
    int count = 0;       // leaf: number of triangles (0 for inner nodes)
};

// How direct light is computed.
//   Point: one point light at the area light's centre, no distance falloff, hard shadows (the M0 baseline).
//   Area:  the rectangle itself is sampled at random (M1): soft shadows, 1/d^2 falloff, one-sided emission.
enum class LightMode { Point, Area };

// How a pixel's radiance is estimated.
//   Classic: Whitted-style (direct light + mirror/glass recursion, ambient constant). No diffuse interreflection.
//   Path:    Monte Carlo path tracing (M2): every surface is Lambertian, the path continues in a cosine-sampled
//            direction. Light is found by hitting the emitter (Scene::nee = false), or, with next event estimation
//            (Scene::nee = true), by sampling a point on the emitter at every vertex.
enum class Integrator { Classic, Path };

// How the triangles are searched for the nearest hit (M5). All three give the same hits; they differ in speed.
//   None:   test every triangle (the reference; only practical for small scenes).
//   Median: BVH, each node split at the median centroid along its longest axis.
//   Sah:    BVH, each node split where the surface area heuristic (binned) estimates the cheapest traversal.
enum class Accel { None, Median, Sah };

// Surface model of the path tracer (the Classic integrator has its own shading).
//   Lambert: every surface is diffuse (M2); metallic / roughness / transmission are ignored.
//   Pbr:     glTF metallic-roughness: Lambert diffuse * (1 - Fresnel) + GGX microfacet specular, mirrors and smooth glass as
//            delta lobes (M7).
enum class BsdfModel { Lambert, Pbr };

// Rectangular area light, defined in the glb by a node named after the Blender light plus custom properties
// (exported as glTF "extras"): rt_light_type = "rect", rt_size_x, rt_size_y (metres), rt_radiance.
// Blender's own light settings are not exported to glTF (AREA lights are unsupported), so these properties
// carry what the renderer needs. Convention (same as glTF cameras and KHR_lights_punctual, and what the Blender
// exporter writes for light objects): the rectangle spans the node's local X and Y axes and emits toward local -Z.
struct AreaLight {
    bool valid = false;
    Vec3 center;        // world position of the rectangle's centre
    Vec3 edgeU, edgeV;  // full edge vectors (corner = center - edgeU/2 - edgeV/2)
    Vec3 normal;        // unit vector in the emission direction
    float area = 0;     // |edgeU x edgeV|
    float radiance = 1; // emitted radiance, renderer units
};

struct CameraInfo {
    bool valid = false;
    Mat4 world;
    float yfov = 0.9f;
    float aspect = 16.0f / 9.0f;
};

struct Scene {
    std::vector<Material> materials;
    std::vector<Texture> textures;        // decoded images that materials refer to by index
    // Ways to trade a little accuracy for less noise in Path mode (all off by default: the earlier images are reproduced bit for bit).
    float clampIndirect = 0.0f;           // > 0: every contribution of a path with two or more scatterings is scaled down so that its largest component is at most this (0 = off)
    float glossyFloor = 0.0f;             // > 0: after a diffuse / glossy bounce, glossy surfaces get at least this roughness (blurs what indirect light sees; 0 = off)
    bool glassShadows = false;            // true: shadow rays pass through glass (tinted, Fresnel-reduced) instead of being blocked; light that reaches a surface through glass by BSDF sampling alone is then not counted (the shadow ray stands for it)
    bool useNormalMaps = true;            // false: ignore normal maps (baseColor / metallicRoughness textures are unaffected)
    bool useTextures = true;              // false: ignore textures and use the material factors only (all earlier images)
    std::vector<Triangle> tris;
    std::vector<BvhNode> nodes;
    std::vector<int> triOrder;
    Accel accel = Accel::Sah;             // how intersect() searches (setAccel rebuilds the BVH when needed)
    Accel builtAccel = Accel::None;       // what nodes / triOrder currently hold (None = nothing built)
    bool orderedTraversal = true;         // BVH: visit the nearer child first, skip boxes beyond the closest hit found
    bool anyHitShadows = true;            // Path: shadow rays stop at the first hit (occluded()) instead of finding the nearest
    double bvhBuildMs = 0;                // time of the last BVH build
    PointLight light;
    Vec3 ambient;
    CameraInfo camera;
    AreaLight area;       // active area light (the file's one, possibly resized by configureLight)
    AreaLight areaFile;   // the area light exactly as the glb defines it
    LightMode lightMode = LightMode::Point;
    // false (default): front/back is decided by the triangle's geometric normal and a shading normal that faces
    // away from the viewer at a silhouette is clamped (see trace). true: the M0 behaviour, which decides by the
    // interpolated normal and flips it, sending rays into the inside of spheres at their outline.
    bool legacyNormals = false;
    // false (default): every triangle is double sided, whatever the material says (all earlier images). true: a triangle of
    // a material with doubleSided = false is invisible to every ray that hits its back side (glTF: front = counter-clockwise
    // when seen from the front). Blender writes doubleSided = false for materials with "Backface Culling" on. Rays that
    // hit the front of the same triangle still see it, so a wall can be seen from inside the room but not from outside.
    bool cullBackfaces = false;
    int maxDepth = MAX_DEPTH;   // reflection/refraction recursion limit (runtime setting); number of bounces in Path mode
    Integrator integrator = Integrator::Classic;
    BsdfModel bsdfModel = BsdfModel::Lambert;   // legacy default: the earlier experiments are reproduced bit for bit; the app asks for Pbr
    bool multiScatter = true;   // Pbr: put back the light single-scattering GGX loses between microfacets (Kulla-Conty)
    float envUniform = -1.0f;   // Path mode, >= 0: rays that escape see this constant radiance (a white furnace for tests); < 0 = off
    bool nee = true;            // Path mode: next event estimation (direct light sampled at every vertex)
    bool mis = true;            // Path mode, with nee: combine light sampling and BSDF sampling (power heuristic)
    bool russianRoulette = false;   // Path mode: end paths at random after rrStartDepth bounces (unbiased, maxDepth stays a hard cap)
    int rrStartDepth = 3;
    float envScale = 0.0f;      // Path mode: multiplier of the sky gradient seen as environment light (0 = only the area light emits)

    // Builds the BVH for `accel` (Median or Sah). Accel::None builds nothing: intersect() then tests every triangle.
    void buildBvh() {
        auto t0 = std::chrono::steady_clock::now();
        size_t n = tris.size();
        nodes.clear();
        triOrder.resize(n);
        for (size_t i = 0; i < n; i++) triOrder[i] = (int)i;
        builtAccel = accel;
        if (accel != Accel::None && n > 0) {
            std::vector<Vec3> cen(n), lo(n), hi(n);          // per-triangle centroid and bounds
            for (size_t i = 0; i < n; i++) {
                const Triangle& t = tris[i];
                cen[i] = (t.p0 + t.p1 + t.p2) * (1.0f / 3.0f);
                lo[i] = {std::min(t.p0.x, std::min(t.p1.x, t.p2.x)), std::min(t.p0.y, std::min(t.p1.y, t.p2.y)), std::min(t.p0.z, std::min(t.p1.z, t.p2.z))};
                hi[i] = {std::max(t.p0.x, std::max(t.p1.x, t.p2.x)), std::max(t.p0.y, std::max(t.p1.y, t.p2.y)), std::max(t.p0.z, std::max(t.p1.z, t.p2.z))};
            }
            nodes.reserve(2 * n / 2 + 1);
            build(0, (int)n, cen, lo, hi);
        }
        bvhBuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    // Chooses the search structure; rebuilds the BVH only when what is built does not match.
    void setAccel(Accel a) {
        accel = a;
        if (builtAccel != a) buildBvh();
    }

    static float surfaceArea(const Vec3& lo, const Vec3& hi) {
        Vec3 d = hi - lo;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }

    int build(int begin, int end, const std::vector<Vec3>& cen, const std::vector<Vec3>& tlo, const std::vector<Vec3>& thi, int depth = 0) {
        int id = (int)nodes.size();
        nodes.emplace_back();
        Vec3 lo(INF, INF, INF), hi(-INF, -INF, -INF), clo = lo, chi = hi;
        for (int i = begin; i < end; i++) {
            int t = triOrder[i];
            lo = {std::min(lo.x, tlo[t].x), std::min(lo.y, tlo[t].y), std::min(lo.z, tlo[t].z)};
            hi = {std::max(hi.x, thi[t].x), std::max(hi.y, thi[t].y), std::max(hi.z, thi[t].z)};
            const Vec3& c = cen[t];
            clo = {std::min(clo.x, c.x), std::min(clo.y, c.y), std::min(clo.z, c.z)};
            chi = {std::max(chi.x, c.x), std::max(chi.y, c.y), std::max(chi.z, c.z)};
        }
        nodes[id].bmin = lo - Vec3(EPS, EPS, EPS);
        nodes[id].bmax = hi + Vec3(EPS, EPS, EPS);
        int count = end - begin;
        Vec3 ext = chi - clo;
        auto makeLeaf = [&]() { nodes[id].first = begin; nodes[id].count = count; return id; };
        if (count <= 1 || depth >= 56 || std::max(ext.x, std::max(ext.y, ext.z)) <= 0) return makeLeaf();   // depth cap: the traversal stack holds 64
        const int maxLeaf = 4;
        int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
        auto key = [&](int t) { const Vec3& c = cen[t]; return axis == 0 ? c.x : axis == 1 ? c.y : c.z; };
        int mid = (begin + end) / 2;

        if (accel == Accel::Sah) {
            // Binned SAH: put the centroids into B bins along each axis, and for every bin boundary estimate
            //   cost = traversal + (area_left / area_parent) * n_left * intersect + (area_right / area_parent) * n_right * intersect.
            const int B = 16;
            const float costTraverse = 1.0f, costIntersect = 1.0f;
            float parentArea = surfaceArea(lo, hi);
            float bestCost = INF; int bestAxis = -1, bestBin = -1;
            for (int ax = 0; ax < 3; ax++) {
                float cmin = ax == 0 ? clo.x : ax == 1 ? clo.y : clo.z, cext = ax == 0 ? ext.x : ax == 1 ? ext.y : ext.z;
                if (cext <= 0) continue;
                Vec3 blo[B], bhi[B]; int bcount[B];
                for (int k = 0; k < B; k++) { blo[k] = Vec3(INF, INF, INF); bhi[k] = Vec3(-INF, -INF, -INF); bcount[k] = 0; }
                for (int i = begin; i < end; i++) {
                    int t = triOrder[i];
                    float c = ax == 0 ? cen[t].x : ax == 1 ? cen[t].y : cen[t].z;
                    int k = std::min(B - 1, (int)(B * (c - cmin) / cext));
                    bcount[k]++;
                    blo[k] = {std::min(blo[k].x, tlo[t].x), std::min(blo[k].y, tlo[t].y), std::min(blo[k].z, tlo[t].z)};
                    bhi[k] = {std::max(bhi[k].x, thi[t].x), std::max(bhi[k].y, thi[t].y), std::max(bhi[k].z, thi[t].z)};
                }
                // sweep from the right to get the areas of the right parts, then from the left
                float rightArea[B]; int rightCount[B];
                Vec3 rlo(INF, INF, INF), rhi(-INF, -INF, -INF); int rc = 0;
                for (int k = B - 1; k >= 1; k--) {
                    rc += bcount[k];
                    rlo = {std::min(rlo.x, blo[k].x), std::min(rlo.y, blo[k].y), std::min(rlo.z, blo[k].z)};
                    rhi = {std::max(rhi.x, bhi[k].x), std::max(rhi.y, bhi[k].y), std::max(rhi.z, bhi[k].z)};
                    rightArea[k] = rc ? surfaceArea(rlo, rhi) : 0.0f; rightCount[k] = rc;
                }
                Vec3 llo(INF, INF, INF), lhi(-INF, -INF, -INF); int lc = 0;
                for (int k = 0; k < B - 1; k++) {
                    lc += bcount[k];
                    llo = {std::min(llo.x, blo[k].x), std::min(llo.y, blo[k].y), std::min(llo.z, blo[k].z)};
                    lhi = {std::max(lhi.x, bhi[k].x), std::max(lhi.y, bhi[k].y), std::max(lhi.z, bhi[k].z)};
                    if (lc == 0 || rightCount[k + 1] == 0) continue;           // one side would be empty
                    float cost = costTraverse + costIntersect * (surfaceArea(llo, lhi) * lc + rightArea[k + 1] * rightCount[k + 1]) / parentArea;
                    if (cost < bestCost) { bestCost = cost; bestAxis = ax; bestBin = k; }
                }
            }
            if (bestAxis >= 0) {
                if (count <= maxLeaf && bestCost >= costIntersect * count) return makeLeaf();   // splitting does not pay off
                float cmin = bestAxis == 0 ? clo.x : bestAxis == 1 ? clo.y : clo.z;
                float cext = bestAxis == 0 ? ext.x : bestAxis == 1 ? ext.y : ext.z;
                auto binOf = [&](int t) {
                    float c = bestAxis == 0 ? cen[t].x : bestAxis == 1 ? cen[t].y : cen[t].z;
                    return std::min(B - 1, (int)(B * (c - cmin) / cext));
                };
                int* m = std::partition(triOrder.data() + begin, triOrder.data() + end, [&](int t) { return binOf(t) <= bestBin; });
                mid = (int)(m - triOrder.data());
                if (mid == begin || mid == end) {                              // cannot happen with the cost test above; stay safe
                    mid = (begin + end) / 2;
                    std::nth_element(triOrder.begin() + begin, triOrder.begin() + mid, triOrder.begin() + end,
                                     [&](int a, int b) { return key(a) < key(b); });
                }
                build(begin, mid, cen, tlo, thi, depth + 1);
                int right = build(mid, end, cen, tlo, thi, depth + 1);
                nodes[id].left = id + 1; nodes[id].right = right;
                return id;
            }
            return makeLeaf();
        }

        // Median split
        if (count <= maxLeaf) return makeLeaf();
        std::nth_element(triOrder.begin() + begin, triOrder.begin() + mid, triOrder.begin() + end,
                         [&](int a, int b) { return key(a) < key(b); });
        build(begin, mid, cen, tlo, thi, depth + 1);
        int right = build(mid, end, cen, tlo, thi, depth + 1);
        nodes[id].left = id + 1;
        nodes[id].right = right;
        return id;
    }

    static bool hitBox(const BvhNode& n, const Ray& r, const Vec3& inv, float tmax) {
        float tx0 = (n.bmin.x - r.o.x) * inv.x, tx1 = (n.bmax.x - r.o.x) * inv.x;
        float ty0 = (n.bmin.y - r.o.y) * inv.y, ty1 = (n.bmax.y - r.o.y) * inv.y;
        float tz0 = (n.bmin.z - r.o.z) * inv.z, tz1 = (n.bmax.z - r.o.z) * inv.z;
        float t0 = std::max(std::max(std::min(tx0, tx1), std::min(ty0, ty1)), std::max(std::min(tz0, tz1), 0.0f));
        float t1 = std::min(std::min(std::max(tx0, tx1), std::max(ty0, ty1)), std::min(std::max(tz0, tz1), tmax));
        return t0 <= t1;
    }

    // Moller-Trumbore, double sided. Returns closest hit.
    // Entry distance of the ray into a node's box (inside tmax), for visiting the nearer child first.
    static bool hitBoxT(const BvhNode& n, const Ray& r, const Vec3& inv, float tmax, float& tnear) {
        float tx0 = (n.bmin.x - r.o.x) * inv.x, tx1 = (n.bmax.x - r.o.x) * inv.x;
        float ty0 = (n.bmin.y - r.o.y) * inv.y, ty1 = (n.bmax.y - r.o.y) * inv.y;
        float tz0 = (n.bmin.z - r.o.z) * inv.z, tz1 = (n.bmax.z - r.o.z) * inv.z;
        float t0 = std::max(std::max(std::min(tx0, tx1), std::min(ty0, ty1)), std::max(std::min(tz0, tz1), 0.0f));
        float t1 = std::min(std::min(std::max(tx0, tx1), std::max(ty0, ty1)), std::min(std::max(tz0, tz1), tmax));
        tnear = t0;
        return t0 <= t1;
    }

    void fillHit(Hit& hit, const Ray& r, float best, const Triangle* bt, float bu, float bv) const {
        hit.t = best;
        hit.p = r.o + r.d * best;
        hit.n = normalize(bt->n0 * (1 - bu - bv) + bt->n1 * bu + bt->n2 * bv);
        Vec3 g = normalize(cross(bt->p1 - bt->p0, bt->p2 - bt->p0));
        hit.ng = dot(g, bt->n0 + bt->n1 + bt->n2) < 0 ? -g : g;   // follow the vertex normals, not the winding order
        hit.material = bt->material;
        float w0 = 1 - bu - bv;
        hit.tu = bt->uv[0] * w0 + bt->uv[2] * bu + bt->uv[4] * bv;
        hit.tv = bt->uv[1] * w0 + bt->uv[3] * bu + bt->uv[5] * bv;
        if (bt->tw[0] != 0.0f) {
            hit.tg = bt->tg[0] * w0 + bt->tg[1] * bu + bt->tg[2] * bv;
            hit.tw = bt->tw[0] * w0 + bt->tw[1] * bu + bt->tw[2] * bv < 0 ? -1.0f : 1.0f;
        } else hit.tw = 0.0f;
    }

    // True if the ray hit the back side of a one-sided triangle and cullBackfaces is on (the hit must then be ignored).
    bool culled(const Triangle& tr, const Ray& r) const {
        return cullBackfaces && !materials[tr.material].doubleSided && dot(cross(tr.p1 - tr.p0, tr.p2 - tr.p0), r.d) > 0;
    }

    // Nearest hit with EPS < t < tmax. Which triangles are looked at depends on `accel` (None / BVH, optionally ordered).
    bool intersect(const Ray& r, float tmax, Hit& hit) const {
        if (tris.empty()) return false;
        tlStats.rays++;
        bool found = false;
        float best = tmax;
        const Triangle* bt = nullptr;
        float bu = 0, bv = 0;
        if (accel == Accel::None || builtAccel == Accel::None || nodes.empty()) {
            for (const Triangle& tr : tris) {
                float t, u, v;
                tlStats.triTests++;
                if (!intersectTriangle(r, tr, t, u, v) || culled(tr, r)) continue;
                if (t > EPS && t < best) { best = t; bt = &tr; bu = u; bv = v; found = true; }
            }
        } else if (!orderedTraversal) {
            Vec3 inv(1.0f / r.d.x, 1.0f / r.d.y, 1.0f / r.d.z);
            int stack[64], sp = 0;
            stack[sp++] = 0;
            while (sp) {
                const BvhNode& nd = nodes[stack[--sp]];
                tlStats.nodeVisits++;
                if (!hitBox(nd, r, inv, best)) continue;
                if (nd.count == 0) { stack[sp++] = nd.left; stack[sp++] = nd.right; continue; }
                for (int k = nd.first; k < nd.first + nd.count; k++) {
                    const Triangle& tr = tris[triOrder[k]];
                    float t, u, v;
                    tlStats.triTests++;
                    if (!intersectTriangle(r, tr, t, u, v) || culled(tr, r)) continue;
                    if (t > EPS && t < best) { best = t; bt = &tr; bu = u; bv = v; found = true; }
                }
            }
        } else {
            // Ordered: test both children when a node is opened, push the farther one first so the nearer is visited
            // first, and drop entries whose box starts beyond the closest hit found so far.
            Vec3 inv(1.0f / r.d.x, 1.0f / r.d.y, 1.0f / r.d.z);
            struct Entry { int node; float tnear; };
            Entry stack[64]; int sp = 0;
            float tn;
            tlStats.nodeVisits++;
            if (hitBoxT(nodes[0], r, inv, best, tn)) stack[sp++] = {0, tn};
            while (sp) {
                Entry e = stack[--sp];
                if (e.tnear > best) continue;
                const BvhNode& nd = nodes[e.node];
                if (nd.count == 0) {
                    float tl = 0, tr2 = 0;
                    tlStats.nodeVisits += 2;
                    bool hl = hitBoxT(nodes[nd.left], r, inv, best, tl), hr = hitBoxT(nodes[nd.right], r, inv, best, tr2);
                    if (hl && hr) {
                        if (tl <= tr2) { stack[sp++] = {nd.right, tr2}; stack[sp++] = {nd.left, tl}; }
                        else           { stack[sp++] = {nd.left, tl};  stack[sp++] = {nd.right, tr2}; }
                    } else if (hl) stack[sp++] = {nd.left, tl};
                    else if (hr) stack[sp++] = {nd.right, tr2};
                    continue;
                }
                for (int k = nd.first; k < nd.first + nd.count; k++) {
                    const Triangle& tr = tris[triOrder[k]];
                    float t, u, v;
                    tlStats.triTests++;
                    if (!intersectTriangle(r, tr, t, u, v) || culled(tr, r)) continue;
                    if (t > EPS && t < best) { best = t; bt = &tr; bu = u; bv = v; found = true; }
                }
            }
        }
        if (found) fillHit(hit, r, best, bt, bu, bv);
        return found;
    }

    // Is there any hit with EPS < t < tmax? Same answer as intersect(), but stops at the first one found (shadow rays).
    bool occluded(const Ray& r, float tmax) const {
        if (tris.empty()) return false;
        if (!anyHitShadows) { Hit h; return intersect(r, tmax, h); }
        tlStats.rays++;
        if (accel == Accel::None || builtAccel == Accel::None || nodes.empty()) {
            for (const Triangle& tr : tris) {
                float t, u, v;
                tlStats.triTests++;
                if (intersectTriangle(r, tr, t, u, v) && t > EPS && t < tmax && !culled(tr, r)) return true;
            }
            return false;
        }
        Vec3 inv(1.0f / r.d.x, 1.0f / r.d.y, 1.0f / r.d.z);
        int stack[64], sp = 0;
        stack[sp++] = 0;
        while (sp) {
            const BvhNode& nd = nodes[stack[--sp]];
            tlStats.nodeVisits++;
            if (!hitBox(nd, r, inv, tmax)) continue;
            if (nd.count == 0) { stack[sp++] = nd.left; stack[sp++] = nd.right; continue; }
            for (int k = nd.first; k < nd.first + nd.count; k++) {
                const Triangle& tr = tris[triOrder[k]];
                float t, u, v;
                tlStats.triTests++;
                if (intersectTriangle(r, tr, t, u, v) && t > EPS && t < tmax && !culled(tr, r)) return true;
            }
        }
        return false;
    }
};

// The material at a hit: the file's factors, multiplied by the textures at the hit's (u, v) when the material has any and
// textures are on. Returns the scene's own material when nothing is textured (no copy); otherwise fills `tmp` (everything
// but the name) and returns it. Every shading routine goes through this, so the textures reach Classic and Path alike.
inline const Material& materialAt(const Scene& sc, const Hit& h, Material& tmp) {
    const Material& m = sc.materials[(size_t)h.material];
    if (!sc.useTextures || (m.baseColorTex < 0 && m.metalRoughTex < 0)) return m;
    tmp.base = m.base; tmp.metallic = m.metallic; tmp.roughness = m.roughness; tmp.transmission = m.transmission;
    tmp.ior = m.ior; tmp.specular = m.specular; tmp.doubleSided = m.doubleSided;
    if (m.baseColorTex >= 0) {
        Vec3 c = sc.textures[m.baseColorTex].sample<true>(h.tu, h.tv);
        tmp.base = Vec3(m.base.x * c.x, m.base.y * c.y, m.base.z * c.z);
    }
    if (m.metalRoughTex >= 0) {
        Vec3 c = sc.textures[m.metalRoughTex].sample<false>(h.tu, h.tv);
        tmp.roughness = m.roughness * c.y;
        tmp.metallic = m.metallic * c.z;
    }
    return tmp;
}

// Normal mapping: replaces the hit's shading normal by the normal stored in the material's normal map, which is given in the
// tangent frame (t, b, n) of the surface: n' = x t + y b + z n with (x, y, z) = (r, g, b) * 2 - 1, x and y times the material's
// scale. glTF convention: +X = the direction of increasing u, +Y = "up" in the image (decreasing v), so b = cross(n, t) * w,
// where w (+1 / -1) was chosen at load time so that b points along -dP/dv even where the UVs are mirrored. Only the shading
// normal changes; the geometric normal (which side is front, the silhouette, shadow-ray offsets) stays the triangle's own.
inline void applyNormalMap(const Scene& sc, Hit& h) {
    if (!sc.useTextures || !sc.useNormalMaps || h.tw == 0.0f) return;
    const Material& m = sc.materials[(size_t)h.material];
    if (m.normalTex < 0) return;
    Vec3 c = sc.textures[m.normalTex].sample<false>(h.tu, h.tv);
    float x = (c.x * 2 - 1) * m.normalScale, y = (c.y * 2 - 1) * m.normalScale, z = std::max(c.z * 2 - 1, 0.0f);
    Vec3 t = h.tg - h.n * dot(h.n, h.tg);                       // the interpolated tangent, made perpendicular to the normal
    float lt = length(t);
    if (lt < 1e-8f) return;
    t = t * (1.0f / lt);
    Vec3 b = cross(h.n, t) * h.tw;
    Vec3 np = t * x + b * y + h.n * z;
    float l = length(np);
    if (l < 1e-8f) return;
    h.n = np * (1.0f / l);
}

// ---------------------------------------------------------------- glb loader
struct Glb {
    Json json;
    std::vector<uint8_t> bin;
};

inline Glb readGlb(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &d[o], 4); return v; };
    if (d.size() < 20 || u32(0) != 0x46546C67) throw std::runtime_error("not a glb file");
    Glb g;
    size_t off = 12;
    while (off + 8 <= d.size()) {
        uint32_t len = u32(off), type = u32(off + 4);
        off += 8;
        if (type == 0x4E4F534A) {  // JSON
            JsonParser jp{(const char*)&d[off], (const char*)&d[off] + len};
            g.json = jp.parse();
        } else if (type == 0x004E4942) {  // BIN
            g.bin.assign(d.begin() + off, d.begin() + off + len);
        }
        off += (len + 3) & ~3u;
    }
    return g;
}

// Reads an accessor as floats (components per element) or as unsigned indices.
inline std::vector<float> readFloats(const Glb& g, int acc, int comps) {
    const Json& a = g.json["accessors"][acc];
    if (a["componentType"].i() != 5126) throw std::runtime_error("expected float accessor");
    const Json& bv = g.json["bufferViews"][a["bufferView"].i()];
    size_t off = (size_t)bv["byteOffset"].f(0) + (size_t)a["byteOffset"].f(0);
    size_t stride = bv.has("byteStride") ? bv["byteStride"].i() : comps * 4;
    int count = a["count"].i();
    std::vector<float> out(count * comps);
    for (int i = 0; i < count; i++) std::memcpy(&out[i * comps], &g.bin[off + i * stride], comps * 4);
    return out;
}

inline std::vector<uint32_t> readIndices(const Glb& g, int acc) {
    const Json& a = g.json["accessors"][acc];
    const Json& bv = g.json["bufferViews"][a["bufferView"].i()];
    size_t off = (size_t)bv["byteOffset"].f(0) + (size_t)a["byteOffset"].f(0);
    int ct = a["componentType"].i(), count = a["count"].i();
    std::vector<uint32_t> out(count);
    for (int i = 0; i < count; i++) {
        if (ct == 5125) { uint32_t v; std::memcpy(&v, &g.bin[off + i * 4], 4); out[i] = v; }
        else if (ct == 5123) { uint16_t v; std::memcpy(&v, &g.bin[off + i * 2], 2); out[i] = v; }
        else if (ct == 5121) out[i] = g.bin[off + i];
        else throw std::runtime_error("unsupported index type");
    }
    return out;
}

inline void addMesh(Scene& sc, const Glb& g, int meshIdx, const Mat4& world) {
    const Json& mesh = g.json["meshes"][meshIdx];
    const Json& prims = mesh["primitives"];
    for (size_t pi = 0; pi < prims.size(); pi++) {
        const Json& pr = prims[pi];
        if (pr.has("mode") && pr["mode"].i() != 4) continue;  // triangles only
        auto pos = readFloats(g, pr["attributes"]["POSITION"].i(), 3);
        size_t nv = pos.size() / 3;
        std::vector<float> nrm;
        if (pr["attributes"].has("NORMAL")) nrm = readFloats(g, pr["attributes"]["NORMAL"].i(), 3);
        std::vector<uint32_t> idx;
        if (pr.has("indices")) idx = readIndices(g, pr["indices"].i());
        else for (size_t i = 0; i < nv; i++) idx.push_back((uint32_t)i);

        std::vector<float> uvs;   // TEXCOORD_0 (float accessors only, what Blender writes)
        if (pr["attributes"].has("TEXCOORD_0")) {
            int ua = pr["attributes"]["TEXCOORD_0"].i();
            if (g.json["accessors"][ua]["componentType"].i() == 5126) uvs = readFloats(g, ua, 2);
        }
        std::vector<Vec3> wp(nv), wn(nv);
        for (size_t i = 0; i < nv; i++) {
            wp[i] = world.point({pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]});
            if (!nrm.empty()) wn[i] = world.normal({nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]});
        }
        int mat = pr.has("material") ? pr["material"].i() : -1;
        if (mat < 0) { mat = (int)sc.materials.size(); sc.materials.push_back(Material{}); }

        // Vertex tangents for normal mapping: for each triangle, dP/du and dP/dv from its UVs; summed per vertex (weighted by the
        // triangle's area, so shared vertices get a smooth frame), made perpendicular to the vertex normal. The handedness w is
        // +1 when cross(n, t) points along "up in the image" = -dP/dv (glTF's v runs downwards), -1 for mirrored UVs.
        std::vector<Vec3> vtan(nv);
        std::vector<float> vw(nv, 0.0f);
        if (!uvs.empty() && !nrm.empty() && sc.materials[(size_t)mat].normalTex >= 0) {
            std::vector<Vec3> ts(nv), bs(nv);
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
                Vec3 e1 = wp[b] - wp[a], e2 = wp[c] - wp[a];
                float du1 = uvs[b * 2] - uvs[a * 2], dv1 = uvs[b * 2 + 1] - uvs[a * 2 + 1];
                float du2 = uvs[c * 2] - uvs[a * 2], dv2 = uvs[c * 2 + 1] - uvs[a * 2 + 1];
                float det = du1 * dv2 - du2 * dv1;
                if (std::fabs(det) < 1e-20f) continue;
                Vec3 tdir = (e1 * dv2 - e2 * dv1) * (1.0f / det), bdir = (e2 * du1 - e1 * du2) * (1.0f / det);
                float lt = length(tdir), lb = length(bdir), area = length(cross(e1, e2));
                if (lt <= 0 || lb <= 0) continue;
                tdir = tdir * (area / lt); bdir = bdir * (area / lb);
                for (uint32_t v : {a, b, c}) { ts[v] += tdir; bs[v] += bdir; }
            }
            for (size_t i = 0; i < nv; i++) {
                Vec3 n = wn[i], t = ts[i] - n * dot(n, ts[i]);
                if (length(t) < 1e-12f) {                       // no usable UVs around this vertex: any tangent
                    t = cross(n, std::fabs(n.x) < 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0));
                    vtan[i] = normalize(t); vw[i] = 1.0f;
                } else {
                    t = normalize(t);
                    vtan[i] = t;
                    vw[i] = dot(cross(n, t), bs[i] * -1.0f) < 0 ? -1.0f : 1.0f;
                }
            }
        }

        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            Triangle t;
            t.p0 = wp[idx[i]]; t.p1 = wp[idx[i + 1]]; t.p2 = wp[idx[i + 2]];
            if (nrm.empty()) t.n0 = t.n1 = t.n2 = normalize(cross(t.p1 - t.p0, t.p2 - t.p0));
            else { t.n0 = wn[idx[i]]; t.n1 = wn[idx[i + 1]]; t.n2 = wn[idx[i + 2]]; }
            t.material = mat;
            if (!uvs.empty())
                for (int k = 0; k < 3; k++) { t.uv[2 * k] = uvs[idx[i + k] * 2]; t.uv[2 * k + 1] = uvs[idx[i + k] * 2 + 1]; }
            for (int k = 0; k < 3; k++) { t.tg[k] = vtan[idx[i + k]]; t.tw[k] = vw[idx[i + k]]; }
            sc.tris.push_back(t);
        }
    }
}

inline void walkNode(Scene& sc, const Glb& g, int ni, const Mat4& parent) {
    const Json& n = g.json["nodes"][ni];
    Mat4 local;
    if (n.has("matrix")) {
        for (int i = 0; i < 16; i++) local.m[i] = n["matrix"][i].f();
    } else {
        float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
        if (n.has("translation")) for (int i = 0; i < 3; i++) t[i] = n["translation"][i].f();
        if (n.has("rotation")) for (int i = 0; i < 4; i++) q[i] = n["rotation"][i].f();
        if (n.has("scale")) for (int i = 0; i < 3; i++) s[i] = n["scale"][i].f();
        local = makeTRS(t, q, s);
    }
    Mat4 world = parent * local;
    if (n.has("mesh")) addMesh(sc, g, n["mesh"].i(), world);
    if (n.has("extras") && n["extras"]["rt_light_type"].str == "rect" && !sc.area.valid) {
        const Json& ex = n["extras"];
        float sizeX = ex["rt_size_x"].f(1.0f), sizeY = ex["rt_size_y"].f(1.0f);
        AreaLight& a = sc.area;
        a.valid = true;
        a.center = world.point(Vec3(0, 0, 0));
        a.edgeU = world.point(Vec3(sizeX, 0, 0)) - a.center;
        a.edgeV = world.point(Vec3(0, sizeY, 0)) - a.center;
        a.normal = normalize(world.point(Vec3(0, 0, -1)) - a.center);
        a.area = length(cross(a.edgeU, a.edgeV));
        a.radiance = ex["rt_radiance"].f(1.0f);
    }
    if (n.has("camera") && !sc.camera.valid) {
        const Json& cam = g.json["cameras"][n["camera"].i()]["perspective"];
        sc.camera.valid = true;
        sc.camera.world = world;
        sc.camera.yfov = cam["yfov"].f(0.9f);
        sc.camera.aspect = cam["aspectRatio"].f(16.0f / 9.0f);
    }
    if (n.has("children"))
        for (size_t i = 0; i < n["children"].size(); i++) walkNode(sc, g, n["children"][i].i(), world);
}

inline Scene loadScene(const std::string& path) {
    Glb g = readGlb(path);
    Scene sc;
    const Json& mats = g.json["materials"];
    std::vector<int> imgMap(g.json["images"].size(), -1);     // glTF image index -> Scene::textures (decoded once, on first use; textures that share an image share it)
    auto textureIndex = [&](const Json& info) -> int {
        if (!info.has("index")) return -1;
        int ti = info["index"].i();
        if (ti < 0 || ti >= (int)g.json["textures"].size()) return -1;
        int src = g.json["textures"][ti]["source"].i(-1);
        if (src < 0 || src >= (int)imgMap.size()) return -1;
        if (imgMap[src] < 0) {
            const Json& img = g.json["images"][src];
            if (!img.has("bufferView")) throw std::runtime_error("texture image must be embedded in the glb (no external file or data URI)");
            const Json& bv = g.json["bufferViews"][img["bufferView"].i()];
            size_t off = (size_t)bv["byteOffset"].f(0), len = (size_t)bv["byteLength"].f(0);
            if (off + len > g.bin.size()) throw std::runtime_error("texture image outside the binary chunk");
            png::Image im = png::decode(&g.bin[off], len);
            Texture t; t.w = im.w; t.h = im.h; t.rgb = std::move(im.rgb);
            imgMap[src] = (int)sc.textures.size();
            sc.textures.push_back(std::move(t));
        }
        return imgMap[src];
    };
    for (size_t i = 0; i < mats.size(); i++) {
        Material m;
        m.name = mats[i]["name"].str;
        const Json& pbr = mats[i]["pbrMetallicRoughness"];
        if (pbr.has("baseColorFactor"))
            m.base = {pbr["baseColorFactor"][0].f(), pbr["baseColorFactor"][1].f(), pbr["baseColorFactor"][2].f()};
        if (pbr.has("baseColorTexture") && pbr["baseColorTexture"]["texCoord"].i(0) == 0) m.baseColorTex = textureIndex(pbr["baseColorTexture"]);
        if (pbr.has("metallicRoughnessTexture") && pbr["metallicRoughnessTexture"]["texCoord"].i(0) == 0) m.metalRoughTex = textureIndex(pbr["metallicRoughnessTexture"]);
        if (mats[i].has("normalTexture") && mats[i]["normalTexture"]["texCoord"].i(0) == 0) {
            m.normalTex = textureIndex(mats[i]["normalTexture"]);
            m.normalScale = mats[i]["normalTexture"]["scale"].f(1.0f);
        }
        if (pbr.has("metallicFactor")) m.metallic = pbr["metallicFactor"].f();
        if (pbr.has("roughnessFactor")) m.roughness = pbr["roughnessFactor"].f();
        const Json& ext = mats[i]["extensions"];
        if (ext.has("KHR_materials_transmission")) m.transmission = ext["KHR_materials_transmission"]["transmissionFactor"].f(0);
        if (ext.has("KHR_materials_ior")) m.ior = ext["KHR_materials_ior"]["ior"].f(1.5f);
        m.doubleSided = mats[i].has("doubleSided") ? mats[i]["doubleSided"].b : false;   // glTF default
        if (ext.has("KHR_materials_specular")) m.specular = ext["KHR_materials_specular"]["specularFactor"].f(1.0f);
        sc.materials.push_back(m);
    }
    int sceneIdx = g.json.has("scene") ? g.json["scene"].i() : 0;
    const Json& roots = g.json["scenes"][sceneIdx]["nodes"];
    for (size_t i = 0; i < roots.size(); i++) walkNode(sc, g, roots[i].i(), Mat4{});
    sc.areaFile = sc.area;
    sc.buildBvh();
    return sc;
}

// ---------------------------------------------------------------- random numbers
// PCG32 (M.E. O'Neill): 64-bit state, 32-bit output. Small, fast, good statistical quality.
struct Pcg32 {
    uint64_t state = 0, inc = 1;
    void seed(uint64_t initState, uint64_t initSeq) {
        state = 0;
        inc = (initSeq << 1) | 1u;   // the stream selector must be odd
        nextU32();
        state += initState;
        nextU32();
    }
    uint32_t nextU32() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xorShifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorShifted >> rot) | (xorShifted << ((32u - rot) & 31u));
    }
    // Uniform float in [0, 1): the top 24 bits (a float has a 24-bit significand), so 1.0 is never returned.
    float nextFloat() { return (float)(nextU32() >> 8) * (1.0f / 16777216.0f); }
};

// Spreads nearby integers (pixel 100, pixel 101, ...) into unrelated 64-bit values (SplitMix64).
inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// One random number stream per (pixel, sample). Nothing is shared between streams or threads, so the same
// seed gives the same image whatever the thread count or the order in which pixels are rendered.
struct Sampler {
    Pcg32 rng;
    Sampler(uint64_t seed, uint64_t pixelIndex, uint32_t sampleIndex) {
        uint64_t h = splitmix64(seed);
        h = splitmix64(h ^ pixelIndex);
        h = splitmix64(h ^ ((uint64_t)sampleIndex << 32 | 0x5bd1e995u));
        rng.seed(h, splitmix64(h ^ pixelIndex));
    }
    float next1D() { return rng.nextFloat(); }
};

// ---------------------------------------------------------------- shading
inline float schlick(float cosTheta, float f0) {
    return f0 + (1 - f0) * std::pow(1 - cosTheta, 5.0f);
}

// Chooses the light model for a render. size > 0 rescales the area light's edges to `size` metres (square, centred
// on the same point) while keeping its total power radiance * area constant, so changing the size changes the
// shadow softness but not the overall brightness. size <= 0 keeps the light as the file defines it.
inline void configureLight(Scene& sc, LightMode mode, float size) {
    sc.lightMode = mode;
    sc.area = sc.areaFile;
    if (sc.areaFile.valid && size > 0) {
        float k = size / length(sc.areaFile.edgeU);
        sc.area.edgeU = sc.areaFile.edgeU * k;
        sc.area.edgeV = sc.areaFile.edgeV * k;
        sc.area.area = sc.areaFile.area * k * k;
        sc.area.radiance = sc.areaFile.radiance / (k * k);
    }
}

// One sample of the direct light at surface point x. `pos` is where the shadow ray goes; `weight` multiplies the
// BRDF * cos(theta at x) term. For a point light that is just its colour. For the area light (uniform sampling of
// the rectangle, pdf = 1 / area) it is
//     radiance * cos(theta at the light) / distance^2 / pdf  =  radiance * area * cos(theta_light) / distance^2
// (the cos at x stays in the shading code). Emission is one-sided: no light leaves the back of the rectangle.
struct LightSample { Vec3 pos; Vec3 weight; };
inline LightSample sampleLight(const Scene& sc, const Vec3& x, Sampler& smp) {
    if (sc.lightMode == LightMode::Area && sc.area.valid) {
        const AreaLight& a = sc.area;
        float u = smp.next1D(), v = smp.next1D();
        Vec3 p = a.center + a.edgeU * (u - 0.5f) + a.edgeV * (v - 0.5f);
        Vec3 d = p - x;
        float d2 = std::max(dot(d, d), 1e-6f);
        float cosLight = std::max(0.0f, dot(a.normal, d * (-1.0f / std::sqrt(d2))));
        float w = a.radiance * a.area * cosLight / d2;
        return {p, Vec3(w, w, w)};
    }
    return {sc.light.pos, sc.light.color};
}

// Fraction of light reaching `p` from `target`; glass casts a faint tinted shadow.
inline Vec3 shadowTransmittance(const Scene& sc, const Vec3& p, const Vec3& n, const Vec3& target) {
    Vec3 toL = target - p;
    float dist = length(toL);
    Ray r{p + n * (EPS * 10), toL * (1.0f / dist)};
    Vec3 T(1, 1, 1);
    float remaining = dist - EPS * 10;
    for (int i = 0; i < 8; i++) {
        Hit h;
        if (!sc.intersect(r, remaining, h)) return T;
        Material mtex;
        const Material& m = materialAt(sc, h, mtex);
        if (m.transmission <= 0.0f) return Vec3(0, 0, 0);
        T = T * m.base * (0.9f * m.transmission);
        r.o = h.p + r.d * (EPS * 10);
        remaining -= h.t + EPS * 10;
        if (remaining <= 0) return T;
    }
    return T;
}

inline Vec3 shadowTransmittance(const Scene& sc, const Vec3& p, const Vec3& n) {
    return shadowTransmittance(sc, p, n, sc.light.pos);
}

inline Vec3 background(const Ray& r) {
    float t = 0.5f * (r.d.y + 1.0f);
    return mix(Vec3(0.9f, 0.9f, 0.9f), Vec3(0.4f, 0.6f, 0.9f), t);
}

inline Vec3 trace(const Scene& sc, const Ray& ray, int depth, Sampler& smp) {
    Hit h;
    if (!sc.intersect(ray, INF, h)) return background(ray);
    Material mtex;
    const Material& m = materialAt(sc, h, mtex);
    applyNormalMap(sc, h);

    Vec3 V = -ray.d;
    bool front;
    Vec3 N;
    if (sc.legacyNormals) {
        front = dot(ray.d, h.n) < 0;
        N = front ? h.n : -h.n;
    } else {
        // Which side was hit is a property of the triangle, not of the smoothed normal: near a sphere's outline the
        // interpolated normal can point away from the viewer although the triangle faces it. Flipping it there (as the
        // legacy code did) sent reflection / refraction rays into the sphere, where they bounced until the depth limit.
        front = dot(ray.d, h.ng) < 0;
        N = front ? h.n : -h.n;
        float c = dot(N, V);
        const float minCos = 0.01f;
        if (c < minCos) N = normalize(N + V * (minCos - c));   // keep the shading normal (just) facing the viewer
    }
    float cosI = std::max(0.0f, dot(N, V));
    Vec3 color(0, 0, 0);
    LightSample ls = sampleLight(sc, h.p, smp);   // one random point on the light (Area mode) or the point light

    // Local lighting (diffuse + Blinn-Phong) for non-transmissive surfaces
    if (m.transmission < 1.0f) {
        Vec3 diffuseColor = m.base * (1 - m.metallic) * (1 - m.transmission);
        color += diffuseColor * sc.ambient;
        Vec3 L = normalize(ls.pos - h.p);
        float nl = dot(N, L);
        if (nl > 0) {
            Vec3 vis = shadowTransmittance(sc, h.p, N, ls.pos);
            Vec3 Hv = normalize(L + V);
            float shininess = 2.0f / std::max(0.002f, m.roughness * m.roughness * m.roughness * m.roughness) - 2.0f;
            shininess = std::min(shininess, 2000.0f);
            float spec = std::pow(std::max(0.0f, dot(N, Hv)), shininess);
            Vec3 f0 = mix(Vec3(0.04f, 0.04f, 0.04f), m.base, m.metallic);
            Vec3 lc = ls.weight * vis;
            color += diffuseColor * lc * nl;
            color += f0 * lc * (spec * nl * (shininess + 8) / 25.0f);
        }
    }
    // Specular highlight on glass
    if (m.transmission > 0.0f) {
        Vec3 L = normalize(ls.pos - h.p);
        Vec3 Hv = normalize(L + V);
        float spec = std::pow(std::max(0.0f, dot(N, Hv)), 800.0f);
        if (spec > 0) color += shadowTransmittance(sc, h.p, N, ls.pos) * ls.weight * (spec * 2.0f);
    }

    if (depth >= sc.maxDepth) return color;

    if (m.transmission > 0.0f) {
        // Dielectric: Fresnel-weighted reflection + refraction (IOR 1.5)
        const float ior = 1.5f;
        float eta = front ? 1.0f / ior : ior;
        Vec3 rd = normalize(reflect(ray.d, N));
        float cosT2 = 1 - eta * eta * (1 - cosI * cosI);
        Vec3 reflected = trace(sc, {h.p + N * (EPS * 10), rd}, depth + 1, smp);
        if (cosT2 < 0) {  // total internal reflection
            color += reflected;
        } else {
            float F = schlick(cosI, 0.04f);
            if (!front) F = schlick(std::sqrt(cosT2), 0.04f);
            Vec3 td = normalize(ray.d * eta + N * (eta * cosI - std::sqrt(cosT2)));
            Vec3 refracted = trace(sc, {h.p - N * (EPS * 10), td}, depth + 1, smp);
            color += reflected * F + refracted * m.base * ((1 - F) * m.transmission);
        }
    } else if (m.roughness < 0.5f) {
        // Mirror-like reflection tinted by Fresnel (base color for metals)
        Vec3 f0 = mix(Vec3(0.04f, 0.04f, 0.04f), m.base, m.metallic);
        Vec3 F = f0 + (Vec3(1, 1, 1) - f0) * std::pow(1 - cosI, 5.0f);
        Vec3 rd = normalize(reflect(ray.d, N));
        color += F * trace(sc, {h.p + N * (EPS * 10), rd}, depth + 1, smp) * (1 - m.roughness);
    }
    return color;
}


// ---------------------------------------------------------------- path tracing (M2)
// Distance along `ray` to the area light's rectangle, or -1. Only the emitting (front) side counts; the back is invisible.
// The light is not part of the triangle mesh, so the path tracer intersects it separately.
inline float intersectAreaLight(const AreaLight& a, const Ray& ray) {
    float denom = dot(a.normal, ray.d);
    if (denom >= -1e-6f) return -1.0f;
    float t = dot(a.normal, a.center - ray.o) / denom;
    if (t <= EPS) return -1.0f;
    Vec3 d = ray.o + ray.d * t - a.center;
    float u = dot(d, a.edgeU) / dot(a.edgeU, a.edgeU), v = dot(d, a.edgeV) / dot(a.edgeV, a.edgeV);
    return (std::fabs(u) <= 0.5f && std::fabs(v) <= 0.5f) ? t : -1.0f;
}

// Cosine-weighted direction on the hemisphere around n (pdf = cos(theta) / pi), by Malley's method: a uniform point
// on the unit disk lifted onto the hemisphere.
inline Vec3 sampleCosineHemisphere(const Vec3& n, float u1, float u2) {
    float r = std::sqrt(u1), phi = 2.0f * 3.14159265f * u2;
    float x = r * std::cos(phi), y = r * std::sin(phi), z = std::sqrt(std::max(0.0f, 1.0f - u1));
    Vec3 a = std::fabs(n.x) > 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 t = normalize(cross(a, n));
    Vec3 b = cross(n, t);
    return normalize(t * x + b * y + n * z);
}

// ---- probability densities of the two ways to pick a direction at a surface point, both per unit SOLID ANGLE
// (needed to weigh them against each other, see pathTrace).
// Cosine-weighted hemisphere sampling (BSDF sampling of a Lambert surface):  pdf = cos(theta) / pi.
inline float bsdfPdfLambert(float cosTheta) { return std::max(cosTheta, 0.0f) * (1.0f / 3.14159265f); }
// Uniform sampling of a point on the rectangle (pdf 1 / area per unit area) seen from a point at distance `dist`, where the
// direction makes the angle with cosine `cosLight` with the light's normal. Changing from area to solid angle multiplies
// the density by dist^2 / cosLight:   pdf = dist^2 / (area * cosLight).
inline float lightPdfSolidAngle(const AreaLight& a, float dist2, float cosLight) {
    return dist2 / (a.area * std::max(cosLight, 1e-6f));
}
inline float powerHeuristic(float pa, float pb) { float a2 = pa * pa, b2 = pb * pb; return a2 / std::max(a2 + b2, 1e-30f); }

// Direct light reaching surface point x (normal N, true side Ng, Lambert albedo `albedo`) from the area light, one sample:
// a uniformly random point p on the rectangle (pdf = 1 / area) and a shadow ray to it. The estimate is
//     albedo / pi * Le * cos(theta at x) * cos(theta at the light) / dist^2 / pdf
//   = albedo * Le * (cos(theta at x) / pi) / pdfSolidAngle.
// With `mis` it is multiplied by the power-heuristic weight of this technique against BSDF sampling, which could have
// produced the same direction with density cos/pi. Unlike Classic shading this ignores glass (everything casts a full
// shadow) and the one-sided emitter gives no light to points behind it.
inline Vec3 sampleDirectLight(const Scene& sc, const Vec3& x, const Vec3& N, const Vec3& Ng, const Vec3& albedo, Sampler& smp, bool mis = false) {
    const AreaLight& a = sc.area;
    float u = smp.next1D(), v = smp.next1D();
    Vec3 p = a.center + a.edgeU * (u - 0.5f) + a.edgeV * (v - 0.5f);
    Vec3 d = p - x;
    float dist2 = std::max(dot(d, d), 1e-6f), dist = std::sqrt(dist2);
    Vec3 wi = d * (1.0f / dist);
    float cosX = dot(N, wi), cosGeo = dot(Ng, wi), cosL = dot(a.normal, wi * -1.0f);
    if (cosX <= 0 || cosGeo <= 0 || cosL <= 0) return Vec3(0, 0, 0);
    tlStats.shadowRays++;
    if (sc.occluded(Ray{x + Ng * (EPS * 10), wi}, dist - EPS * 10)) return Vec3(0, 0, 0);
    float pdfLight = lightPdfSolidAngle(a, dist2, cosL), pdfBsdf = bsdfPdfLambert(cosX);
    float weight = mis ? powerHeuristic(pdfLight, pdfBsdf) : 1.0f;
    return albedo * (a.radiance * pdfBsdf / pdfLight * weight);
}

// ---------------------------------------------------------------- PBR surface model (M7)
// Orthonormal basis around a (shading) normal, to work with directions in a local frame where the normal is +z.
struct Frame {
    Vec3 t, b, n;
    explicit Frame(const Vec3& normal) {
        n = normal;
        Vec3 a = std::fabs(n.x) > 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        t = normalize(cross(a, n));
        b = cross(n, t);
    }
    Vec3 toLocal(const Vec3& v) const { return Vec3(dot(v, t), dot(v, b), dot(v, n)); }
    Vec3 toWorld(const Vec3& v) const { return t * v.x + b * v.y + n * v.z; }
};

constexpr float kPi = 3.14159265358979f, kInvPi = 1.0f / 3.14159265358979f;

// Schlick's approximation of the Fresnel reflectance: f0 + (f90 - f0) (1 - cos)^5 (f90 = reflectance at grazing angles).
inline float schlickScalar(float f0, float cosTheta, float f90 = 1.0f) {
    float m = 1.0f - std::min(1.0f, std::max(0.0f, cosTheta)), m2 = m * m;
    return f0 + (f90 - f0) * m2 * m2 * m;
}
inline Vec3 schlickColor(const Vec3& f0, float cosTheta, float f90 = 1.0f) {
    float m = 1.0f - std::min(1.0f, std::max(0.0f, cosTheta)), m2 = m * m, k = m2 * m2 * m;
    return f0 + (Vec3(f90, f90, f90) - f0) * k;
}

// Exact (unpolarised) Fresnel reflectance of a smooth dielectric interface. cosI = cos of the angle of incidence (> 0),
// eta = n_transmitted / n_incident. Returns 1 for total internal reflection.
inline float fresnelDielectric(float cosI, float eta) {
    cosI = std::min(1.0f, std::max(0.0f, cosI));
    float sin2T = (1.0f - cosI * cosI) / (eta * eta);
    if (sin2T >= 1.0f) return 1.0f;
    float cosT = std::sqrt(1.0f - sin2T);
    float rs = (cosI - eta * cosT) / (cosI + eta * cosT), rp = (eta * cosI - cosT) / (eta * cosI + cosT);
    return 0.5f * (rs * rs + rp * rp);
}

// Refracted direction of the unit direction d (pointing along the ray) at an interface with unit normal N facing the ray
// (dot(N, d) < 0). eta = n_incident / n_transmitted. Returns false on total internal reflection.
inline bool refractDirection(const Vec3& d, const Vec3& N, float eta, Vec3& out) {
    float cosI = -dot(N, d);
    float cosT2 = 1.0f - eta * eta * (1.0f - cosI * cosI);
    if (cosT2 < 0.0f) return false;
    out = normalize(d * eta + N * (eta * cosI - std::sqrt(cosT2)));
    return true;
}

// GGX / Trowbridge-Reitz microfacet distribution (isotropic, alpha = roughness^2). D is per unit solid angle of the
// microfacet normal, so that the integral of D(h) cos(theta_h) over the hemisphere is 1.
inline float ggxD(float cosH, float alpha) {
    float a2 = alpha * alpha, d = cosH * cosH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}
// Smith shadowing-masking: Lambda(w) = (sqrt(1 + alpha^2 tan^2(theta)) - 1) / 2, G1 = 1 / (1 + Lambda), and the
// height-correlated G2 = 1 / (1 + Lambda_o + Lambda_i).
inline float ggxLambda(float cosT, float alpha) {
    float c2 = cosT * cosT, t2 = (1.0f - c2) / std::max(c2, 1e-12f);
    return 0.5f * (std::sqrt(1.0f + alpha * alpha * t2) - 1.0f);
}
inline float ggxG1(float cosT, float alpha) { return 1.0f / (1.0f + ggxLambda(cosT, alpha)); }
inline float ggxG2(float cosO, float cosI, float alpha) { return 1.0f / (1.0f + ggxLambda(cosO, alpha) + ggxLambda(cosI, alpha)); }

// Samples a microfacet normal from the distribution of normals VISIBLE from direction wo (Heitz 2018). The density of
// the resulting reflected direction wi is G1(wo) D(h) / (4 cos(theta_o)): see PbrBsdf::pdf.
inline Vec3 ggxSampleVisibleNormal(const Vec3& wo, float alpha, float u1, float u2) {
    Vec3 vh = normalize(Vec3(alpha * wo.x, alpha * wo.y, wo.z));       // the view direction in the "unstretched" configuration
    float lensq = vh.x * vh.x + vh.y * vh.y;
    Vec3 t1 = lensq > 1e-12f ? Vec3(-vh.y, vh.x, 0) * (1.0f / std::sqrt(lensq)) : Vec3(1, 0, 0);
    Vec3 t2 = cross(vh, t1);
    float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
    float p1 = r * std::cos(phi), p2 = r * std::sin(phi);
    float s = 0.5f * (1.0f + vh.z);
    p2 = (1.0f - s) * std::sqrt(std::max(0.0f, 1.0f - p1 * p1)) + s * p2;
    Vec3 nh = t1 * p1 + t2 * p2 + vh * std::sqrt(std::max(0.0f, 1.0f - p1 * p1 - p2 * p2));
    return normalize(Vec3(alpha * nh.x, alpha * nh.y, std::max(0.0f, nh.z)));
}

// Directional albedo of the GGX specular lobe with Schlick Fresnel, as a function of the view direction (mu = cos theta_o) and the
// roughness. With F = f0 + (f90 - f0) (1 - wo.h)^5 it splits (split-sum, Karis) into two tables that do not depend on f0 / f90:
//     Es(mu) = f0 * A(mu, r) + f90 * B(mu, r)       A = E[(1 - (1-wo.h)^5) G2/G1],  B = E[(1-wo.h)^5 G2/G1]
// where E is taken over visible-normal samples (stratified, deterministic). Built once, on first use (~0.1 s in Release).
// The diffuse lobe needs it to conserve energy together with the specular lobe (see PbrBsdf).
struct SpecularAlbedoLut {
    static constexpr int NM = 24, NR = 24;
    static constexpr float rMin = 0.03f;
    float A[NM][NR], B[NM][NR], Abar[NR], Bbar[NR];

    SpecularAlbedoLut() {
        const int S = 48;
        for (int j = 0; j < NR; j++) {
            float rough = rMin + (1.0f - rMin) * j / (NR - 1), alpha = rough * rough;
            for (int i = 0; i < NM; i++) {
                float mu = (i + 0.5f) / NM;
                Vec3 wo(std::sqrt(std::max(0.0f, 1.0f - mu * mu)), 0, mu);
                double t = 0, k = 0;
                for (int a = 0; a < S; a++) for (int b = 0; b < S; b++) {
                    Vec3 h = ggxSampleVisibleNormal(wo, alpha, (a + 0.5f) / S, (b + 0.5f) / S);
                    Vec3 wi = h * (2.0f * dot(wo, h)) - wo;
                    if (wi.z <= 0) continue;
                    float w = ggxG2(wo.z, wi.z, alpha) / ggxG1(wo.z, alpha);
                    float m = 1.0f - std::max(0.0f, dot(wo, h)), m2 = m * m;
                    t += w; k += w * m2 * m2 * m;
                }
                A[i][j] = (float)((t - k) / (S * S)); B[i][j] = (float)(k / (S * S));
            }
            double ab = 0, bb = 0;                                   // cosine-weighted hemispherical averages: 2 * integral of Es(mu) mu dmu
            for (int i = 0; i < NM; i++) { float mu = (i + 0.5f) / NM; ab += 2.0 * A[i][j] * mu / NM; bb += 2.0 * B[i][j] * mu / NM; }
            Abar[j] = (float)ab; Bbar[j] = (float)bb;
        }
    }
    static const SpecularAlbedoLut& get() { static const SpecularAlbedoLut lut; return lut; }

    void lookup(float mu, float rough, float& a, float& b) const {
        float x = std::min((float)NM - 1.0f, std::max(0.0f, mu * NM - 0.5f));
        float y = std::min((float)NR - 1.0f, std::max(0.0f, (rough - rMin) / (1.0f - rMin) * (NR - 1)));
        int i0 = (int)x, j0 = (int)y, i1 = std::min(i0 + 1, NM - 1), j1 = std::min(j0 + 1, NR - 1);
        float fx = x - i0, fy = y - j0;
        a = (A[i0][j0] * (1 - fx) + A[i1][j0] * fx) * (1 - fy) + (A[i0][j1] * (1 - fx) + A[i1][j1] * fx) * fy;
        b = (B[i0][j0] * (1 - fx) + B[i1][j0] * fx) * (1 - fy) + (B[i0][j1] * (1 - fx) + B[i1][j1] * fx) * fy;
    }
    void average(float rough, float& a, float& b) const {
        float y = std::min((float)NR - 1.0f, std::max(0.0f, (rough - rMin) / (1.0f - rMin) * (NR - 1)));
        int j0 = (int)y, j1 = std::min(j0 + 1, NR - 1);
        float fy = y - j0;
        a = Abar[j0] * (1 - fy) + Abar[j1] * fy;
        b = Bbar[j0] * (1 - fy) + Bbar[j1] * fy;
    }
};

// Scales a contribution down so that its largest component is at most `limit` (the colour is kept). limit <= 0: no change.
inline Vec3 clampContribution(const Vec3& c, float limit) {
    if (limit <= 0.0f) return c;
    float m = std::max(c.x, std::max(c.y, c.z));
    return m > limit ? c * (limit / m) : c;
}

struct BsdfSample {
    Vec3 wi;               // sampled direction (local frame)
    Vec3 weight;           // f * cos / pdf: what the path throughput is multiplied by
    float pdf = 0;         // density (per solid angle) of the non-delta lobes at wi, with the lobe selection probabilities (for MIS)
    bool delta = false;    // a mirror lobe was chosen: the direction cannot be produced by light sampling
    bool valid = false;
};

// glTF metallic-roughness surface without transmission, in the local frame (normal = +z, all directions point away):
//   f = diffuseColor * (1 - Es(mu_o)) (1 - Es(mu_i)) / (pi (1 - Ebar))   (diffuse)
//     + F(wo.h) D(h) G2 / (4 cos_o cos_i)                                  (GGX specular, single scattering)
//     + Fms (1 - E(mu_o)) (1 - E(mu_i)) / (pi (1 - Ebar_E))                (multiple scattering between microfacets)
// diffuseColor = base * (1 - metallic); F is Schlick with f0 = mix(dielectric f0, base, metallic) and f90 = mix(specularFactor,
// 1, metallic), the dielectric f0 coming from the index of refraction (0.04 for 1.5) times KHR_materials_specular.
// alpha = roughness^2. Es(mu) is the directional albedo of the specular lobe (SpecularAlbedoLut), Ebar its cosine-weighted
// average. The diffuse part is what the specular lobe does not already reflect: its albedo is exactly 1 - Es(mu_o), so the two
// lobes together never exceed 1, and it is reciprocal (Weidlich-Wilkie / Kulla-Conty style coupling). The simpler glTF form
// (1 - F(wo.h)) * diffuse + specular is NOT energy conserving: it reaches an albedo of 1.4 at grazing angles (measured).
// The multiple-scattering term (Kulla & Conty 2017, optional) puts back the light that single-scattering GGX loses when it bounces
// a second time between microfacets (35% at roughness 0.75 and 61% at 1.0 for a white metal): E(mu) = A + B is the albedo of
// GGX with F = 1, Favg the cosine-weighted average of F, Fms = Favg^2 Ebar_E / (1 - Favg (1 - Ebar_E)); it is a cosine-like lobe,
// sampled like the diffuse lobe, and counts as part of the specular albedo in the diffuse coupling.
// Below alpha 1e-3 the specular lobe is a perfect mirror (a delta lobe: it has no eval() / pdf(), and is chosen with
// probability pSpec; its Es is just F).
struct PbrBsdf {
    Vec3 diffuseColor{0, 0, 0};
    Vec3 f0{0.04f, 0.04f, 0.04f};
    float f90 = 1.0f;              // reflectance of the specular lobe at grazing angles: mix(specularFactor, 1, metallic)
    float dielF0 = 0.04f;
    float dielF90 = 1.0f;          // specularFactor: the dielectric layer reflects nothing at all when it is 0
    float alpha = 1.0f;
    float rough = 1.0f;
    Vec3 ebar{0, 0, 0};            // cosine-weighted average of the specular lobe's total albedo (incl. multiple scattering), per channel
    Vec3 fms{0, 0, 0};             // multiple-scattering factor (0 when switched off, for mirrors, or without a specular lobe)
    float eBarScalar = 1.0f;       // cosine-weighted average of E(mu) (GGX albedo with F = 1)
    float pSpec = 0.5f;            // probability of sampling the specular lobe (else the diffuse lobe)
    bool specDelta = false;

    static float luminance(const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

    static PbrBsdf make(const Material& m, bool multiScatter = true, float minRoughness = 0.0f) {
        PbrBsdf b;
        float metallic = std::min(1.0f, std::max(0.0f, m.metallic));
        float r = (m.ior - 1.0f) / (m.ior + 1.0f);
        b.dielF0 = std::min(1.0f, std::max(0.0f, r * r * m.specular));
        b.dielF90 = std::min(1.0f, std::max(0.0f, m.specular));
        b.f90 = b.dielF90 * (1.0f - metallic) + metallic;
        b.diffuseColor = m.base * (1.0f - metallic);
        b.f0 = mix(Vec3(b.dielF0, b.dielF0, b.dielF0), m.base, metallic);
        float rough = std::min(1.0f, std::max(0.0f, std::max(m.roughness, minRoughness)));   // minRoughness: Scene::glossyFloor after a diffuse / glossy bounce
        b.rough = rough;
        b.alpha = rough * rough;
        b.specDelta = b.alpha < 1e-3f;
        Vec3 favg = b.f0 + (Vec3(b.f90, b.f90, b.f90) - b.f0) * (1.0f / 21.0f);        // 2 * integral of Schlick(mu) mu dmu
        float sL;
        if (b.specDelta) {
            b.ebar = favg;
            sL = luminance(b.f0);
        } else {
            float ab, bb;
            SpecularAlbedoLut::get().average(rough, ab, bb);
            Vec3 ebarSpec = b.f0 * ab + Vec3(b.f90, b.f90, b.f90) * bb;
            b.eBarScalar = ab + bb;
            b.ebar = ebarSpec;
            if (multiScatter) {
                float e = std::min(b.eBarScalar, 0.9999f);
                auto ms = [&](float fa) { return fa * fa * e / std::max(1.0f - fa * (1.0f - e), 1e-4f); };
                b.fms = Vec3(ms(favg.x), ms(favg.y), ms(favg.z));
                b.ebar = ebarSpec + b.fms * (1.0f - e);
            }
            sL = luminance(ebarSpec);
        }
        float dL = luminance(b.diffuseColor) * (1.0f - b.dielF0) + luminance(b.fms) * (1.0f - b.eBarScalar);
        if (sL <= 0) b.pSpec = 0.0f;
        else if (dL <= 0) b.pSpec = 1.0f;
        else b.pSpec = std::min(0.95f, std::max(0.05f, sL / (sL + dL)));
        return b;
    }

    // Directional albedo of the specular lobe incl. multiple scattering (per channel), and E(mu) alone, for a view cosine mu.
    Vec3 specAlbedo(float mu, float& eScalar) const {
        if (specDelta) { eScalar = 1.0f; return schlickColor(f0, mu, f90); }
        float a, b;
        SpecularAlbedoLut::get().lookup(mu, rough, a, b);
        eScalar = a + b;
        return f0 * a + Vec3(f90, f90, f90) * b + fms * (1.0f - std::min(eScalar, 1.0f));
    }

    // BSDF value of the non-delta lobes (not multiplied by cos).
    Vec3 eval(const Vec3& wo, const Vec3& wi) const {
        if (wo.z <= 0 || wi.z <= 0) return Vec3(0, 0, 0);
        Vec3 h = normalize(wo + wi);
        float woh = std::max(dot(wo, h), 0.0f);
        Vec3 f(0, 0, 0);
        if (diffuseColor.x + diffuseColor.y + diffuseColor.z + fms.x + fms.y + fms.z > 0) {
            float Eo, Ei;
            Vec3 eo = specAlbedo(wo.z, Eo), ei = specAlbedo(wi.z, Ei);
            auto w = [](float o, float i, float e) { return (1.0f - o) * (1.0f - i) / (kPi * std::max(1.0f - e, 1e-3f)); };
            f = Vec3(diffuseColor.x * w(eo.x, ei.x, ebar.x), diffuseColor.y * w(eo.y, ei.y, ebar.y), diffuseColor.z * w(eo.z, ei.z, ebar.z));
            float msw = (1.0f - std::min(Eo, 1.0f)) * (1.0f - std::min(Ei, 1.0f)) / (kPi * std::max(1.0f - eBarScalar, 1e-3f));
            f += fms * msw;
        }
        if (!specDelta && pSpec > 0) {
            f += schlickColor(f0, woh, f90) * (ggxD(h.z, alpha) * ggxG2(wo.z, wi.z, alpha) / (4.0f * wo.z * wi.z));
        }
        return f;
    }

    // Density per solid angle with which sample() produces wi through a non-delta lobe (selection probabilities included).
    float pdf(const Vec3& wo, const Vec3& wi) const {
        if (wo.z <= 0 || wi.z <= 0) return 0.0f;
        float p = (1.0f - pSpec) * wi.z * kInvPi;
        if (!specDelta && pSpec > 0) {
            Vec3 h = normalize(wo + wi);
            p += pSpec * ggxG1(wo.z, alpha) * ggxD(h.z, alpha) / (4.0f * wo.z);
        }
        return p;
    }

    BsdfSample sample(const Vec3& wo, float uLobe, float u1, float u2) const {
        BsdfSample s;
        if (wo.z <= 0) return s;
        if (uLobe < pSpec) {
            if (specDelta) {                                              // mirror
                s.wi = Vec3(-wo.x, -wo.y, wo.z);
                s.weight = schlickColor(f0, wo.z, f90) * (1.0f / pSpec);
                s.delta = true; s.valid = true;
                return s;
            }
            Vec3 h = ggxSampleVisibleNormal(wo, alpha, u1, u2);
            s.wi = h * (2.0f * dot(wo, h)) - wo;
        } else {
            float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
            s.wi = Vec3(r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u1)));
        }
        if (s.wi.z <= 0) return s;                                         // the reflected direction went below the surface
        float p = pdf(wo, s.wi);
        if (p <= 0) return s;
        s.weight = eval(wo, s.wi) * (s.wi.z / p);
        s.pdf = p; s.valid = true;
        return s;
    }
};

// A shadow ray that may pass through glass (Scene::glassShadows). Returns the transmittance along the ray from `o` in direction `d`
// for `dist` metres: 0 when something that is not glass is in the way, otherwise the product over the glass surfaces crossed of
// base colour * transmission * (1 - Fresnel reflectance). Each surface counts once, the way a refracted path would; the sideways
// shift a pane gives the ray is ignored, so this is right for thin flat glass (windows) and only an approximation for a ball.
// `crossed` is set when any glass was crossed.
inline Vec3 shadowThroughGlass(const Scene& sc, Vec3 o, const Vec3& d, float dist, bool& crossed) {
    Vec3 T(1, 1, 1);
    float remaining = dist;
    for (int i = 0; i < 8; i++) {
        Hit h;
        if (!sc.intersect(Ray{o, d}, remaining, h)) return T;
        Material mtex;
        const Material& m = materialAt(sc, h, mtex);
        if (m.transmission <= 0.0f) return Vec3(0, 0, 0);
        // Every face is treated as one entered from air at the angle the straight shadow ray has. For the face a ray leaves through the
        // transmittance is the same by symmetry (and using the inside-the-glass formula with the outside angle would call a ray beyond
        // 42 degrees totally reflected, which a straight ray that really passes the pane is not).
        float cosI = std::min(1.0f, std::fabs(dot(d, h.ng)));
        float F = fresnelDielectric(cosI, m.ior);
        T = T * m.base * (m.transmission * (1.0f - F));
        crossed = true;
        o = h.p + d * (EPS * 10);
        remaining -= h.t + EPS * 10;
        if (remaining <= 0) return T;
    }
    return Vec3(0, 0, 0);                                         // too many layers: call it blocked
}

// Direct light at x for the PBR model: like sampleDirectLight, with the BSDF in place of albedo / pi.
//     f(wo, wi) * cos(theta at x) * Le / pdfLight * (MIS weight)
inline Vec3 sampleDirectLightPbr(const Scene& sc, const Vec3& x, const Vec3& N, const Vec3& Ng, const Frame& fr, const Vec3& woLocal,
                                 const PbrBsdf& bsdf, Sampler& smp, bool mis) {
    const AreaLight& a = sc.area;
    float u = smp.next1D(), v = smp.next1D();
    Vec3 p = a.center + a.edgeU * (u - 0.5f) + a.edgeV * (v - 0.5f);
    Vec3 d = p - x;
    float dist2 = std::max(dot(d, d), 1e-6f), dist = std::sqrt(dist2);
    Vec3 wi = d * (1.0f / dist);
    float cosX = dot(N, wi), cosGeo = dot(Ng, wi), cosL = dot(a.normal, wi * -1.0f);
    if (cosX <= 0 || cosGeo <= 0 || cosL <= 0) return Vec3(0, 0, 0);
    Vec3 wiLocal = fr.toLocal(wi);
    Vec3 f = bsdf.eval(woLocal, wiLocal);
    if (f.x + f.y + f.z <= 0) return Vec3(0, 0, 0);
    tlStats.shadowRays++;
    float pdfLight = lightPdfSolidAngle(a, dist2, cosL);
    if (sc.glassShadows) {
        bool viaGlass = false;
        Vec3 vis = shadowThroughGlass(sc, x + Ng * (EPS * 10), wi, dist - EPS * 10, viaGlass);
        if (vis.x + vis.y + vis.z <= 0) return Vec3(0, 0, 0);
        // light seen through glass is not produced by BSDF sampling in this mode (the path tracer skips it), so no MIS weight here
        float weight = (mis && !viaGlass) ? powerHeuristic(pdfLight, bsdf.pdf(woLocal, wiLocal)) : 1.0f;
        return f * (cosX * a.radiance / pdfLight * weight) * vis;
    }
    if (sc.occluded(Ray{x + Ng * (EPS * 10), wi}, dist - EPS * 10)) return Vec3(0, 0, 0);
    float weight = mis ? powerHeuristic(pdfLight, bsdf.pdf(woLocal, wiLocal)) : 1.0f;
    return f * (cosX * a.radiance / pdfLight * weight);
}

// Path tracing with the PBR model. Same structure as pathTraceLambert (NEE, MIS, Russian roulette), plus:
//  - the BSDF decides the next direction (diffuse lobe, GGX lobe by visible-normal sampling, or a mirror);
//  - a glass surface (transmission > 0), chosen with probability = transmission, is a smooth dielectric: reflect with the exact
//    Fresnel probability, else refract (tinted by the base colour, radiance scaled by (n_i / n_t)^2, which cancels between
//    entering and leaving). Total internal reflection is handled. Rough glass is not modelled: it is treated as smooth.
//  - delta events (mirror, glass) cannot be found by light sampling, so no light sampling is done there, and an emitter
//    reached right after one counts with weight 1 (the MIS branch for delta lobes). Light that reaches a diffuse point through
//    glass is therefore found by BSDF sampling only (a caustic, noisy), because light sampling treats glass as opaque.
//
// Noise-for-accuracy switches (Scene::clampIndirect, glossyFloor, glassShadows; all off by default):
//  - clampIndirect: a contribution with two or more scatterings before the light (light sampling at the second or later vertex, an
//    emitter reached by the third or later ray) is scaled so that its largest component is at most the limit. Direct light and a
//    mirror or glossy highlight seen straight from the camera are never clamped.
//  - glossyFloor: once the path has had a non-delta (diffuse / glossy) bounce, surfaces get at least this roughness, so a nearly
//    mirror-like surface seen only by indirect light spreads its reflection (probability up, value per hit down).
//  - glassShadows: light sampling sends the shadow ray through glass (shadowThroughGlass). The emitter reached by BSDF sampling
//    after a refraction that followed a diffuse / glossy vertex is then not counted (that vertex's shadow ray covers it); a light
//    seen by the camera itself through glass, and reflections off glass, still count.
inline Vec3 pathTracePbr(const Scene& sc, Ray ray, Sampler& smp) {
    Vec3 L(0, 0, 0), throughput(1, 1, 1);
    const bool hasLight = sc.area.valid;
    float prevPdf = 0;
    bool prevDelta = false;
    bool scattered = false;         // a non-delta (diffuse / glossy) bounce has happened on this path
    bool refractedSince = false;    // a refraction happened since the last non-delta vertex
    auto add = [&](const Vec3& c, bool indirect) { L += indirect ? clampContribution(c, sc.clampIndirect) : c; };
    for (int bounce = 0;; bounce++) {
        Hit h;
        bool hit = sc.intersect(ray, INF, h);
        if (hasLight) {
            float tl = intersectAreaLight(sc.area, ray);
            if (tl > 0 && (!hit || tl < h.t)) {
                if (sc.glassShadows && sc.nee && prevDelta && refractedSince && scattered) return L;   // counted by the shadow ray through the glass
                if (!sc.nee || bounce == 0 || prevDelta) add(throughput * sc.area.radiance, bounce >= 2);
                else if (sc.mis) {
                    float cosL = dot(sc.area.normal, ray.d * -1.0f);
                    float w = powerHeuristic(prevPdf, lightPdfSolidAngle(sc.area, tl * tl, cosL));
                    add(throughput * (sc.area.radiance * w), bounce >= 2);
                }
                return L;
            }
        }
        if (!hit) {
            if (sc.envUniform >= 0) L += throughput * sc.envUniform;
            else if (sc.envScale > 0) L += throughput * background(ray) * sc.envScale;
            return L;
        }
        if (bounce >= sc.maxDepth) return L;

        Material mtex;
        const Material& m = materialAt(sc, h, mtex);
        applyNormalMap(sc, h);
        Vec3 V = ray.d * -1.0f;
        bool front = dot(ray.d, h.ng) < 0;
        Vec3 Ng = front ? h.ng : -h.ng;                // the side the ray came from
        Vec3 N = front ? h.n : -h.n;
        float c = dot(N, Ng);
        if (c < 0.01f) N = normalize(N + Ng * (0.01f - c));
        float cv = dot(N, V);
        if (cv < 0.01f) N = normalize(N + V * (0.01f - cv));      // near a silhouette the smooth normal can face away from the viewer

        Vec3 newDir, newOrigin;
        if (m.transmission > 0.0f && smp.next1D() < m.transmission) {
            float cosI = std::min(1.0f, std::max(0.0f, dot(N, V)));
            float etaRel = front ? m.ior : 1.0f / m.ior;           // n_transmitted / n_incident
            float F = fresnelDielectric(cosI, etaRel);
            if (smp.next1D() < F) {
                newDir = normalize(reflect(ray.d, N));
                newOrigin = h.p + Ng * (EPS * 10);
            } else {
                float eta = 1.0f / etaRel;                            // n_incident / n_transmitted
                if (!refractDirection(ray.d, N, eta, newDir)) return L;   // cannot happen: F = 1 then
                newOrigin = h.p - Ng * (EPS * 10);
                throughput = throughput * m.base * (eta * eta);
                refractedSince = true;
            }
            prevDelta = true;
            prevPdf = 0;
        } else {
            PbrBsdf bsdf = PbrBsdf::make(m, sc.multiScatter, scattered ? sc.glossyFloor : 0.0f);
            Frame fr(N);
            Vec3 wo = fr.toLocal(V);
            if (sc.nee && hasLight) add(throughput * sampleDirectLightPbr(sc, h.p, N, Ng, fr, wo, bsdf, smp, sc.mis), bounce >= 1);
            float uLobe = smp.next1D(), u1 = smp.next1D(), u2 = smp.next1D();
            BsdfSample bs = bsdf.sample(wo, uLobe, u1, u2);
            if (!bs.valid) return L;
            newDir = fr.toWorld(bs.wi);
            if (dot(newDir, Ng) <= 0) return L;                       // the smooth normal pushed the sample below the real surface
            newOrigin = h.p + Ng * (EPS * 10);
            throughput = throughput * bs.weight;
            prevDelta = bs.delta;
            prevPdf = bs.pdf;
            if (!bs.delta) { scattered = true; refractedSince = false; }
        }
        if (sc.russianRoulette && bounce >= sc.rrStartDepth) {
            float p = std::min(0.95f, std::max(0.05f, std::max(throughput.x, std::max(throughput.y, throughput.z))));
            if (smp.next1D() >= p) return L;
            throughput = throughput * (1.0f / p);
        }
        ray = Ray{newOrigin, newDir};
    }
}

// Radiance along `ray` by a random walk. At each hit: the albedo multiplies the throughput (Lambert BRDF albedo/pi,
// times cos / pdf = cos/pi, gives exactly albedo), then a cosine-sampled direction continues the path. Emission is
// added when the path reaches the light (or, scaled by envScale, escapes into the sky). Only `maxDepth` bounces are
// followed, so the result is the light that has bounced 0..maxDepth times (a biased-low but consistent estimate).
// The Classic direct-lighting code is not used here.
//
// Three ways to count the direct light (Scene::nee, Scene::mis), all with the same expectation at every bounce limit:
//   nee = false:  only BSDF sampling: a path that reaches the emitter adds its radiance.
//   nee, !mis:    light sampling at every vertex (sampleDirectLight); a BSDF-sampled ray that hits the emitter adds
//                 nothing (only the camera ray, bounce 0, sees it directly). Counted exactly once.
//   nee, mis:     both techniques contribute, each weighted by the power heuristic of the two densities (of the SAME
//                 direction, in solid angle), so weights of the two ways to produce a direction sum to 1: still counted once.
// NEE at vertex b (0-based) stands for the paths that reach the light with the (b+1)-th ray, which the BSDF-only
// version only follows when maxDepth >= b + 1.
//
// Russian roulette (Scene::russianRoulette): after rrStartDepth bounces, a path continues with probability p (the
// largest throughput component, clamped to [0.05, 0.95]) and its throughput is divided by p; otherwise it ends. The
// expectation is unchanged; dark paths, which matter little, are ended early. maxDepth stays a hard cap.
inline Vec3 pathTraceLambert(const Scene& sc, Ray ray, Sampler& smp) {
    Vec3 L(0, 0, 0), throughput(1, 1, 1);
    const bool hasLight = sc.area.valid;
    float prevBsdfPdf = 0;                              // density (solid angle) with which the current ray was chosen
    auto add = [&](const Vec3& c, bool indirect) { L += indirect ? clampContribution(c, sc.clampIndirect) : c; };   // Scene::clampIndirect, as in pathTracePbr
    for (int bounce = 0;; bounce++) {
        Hit h;
        bool hit = sc.intersect(ray, INF, h);
        if (hasLight) {
            float tl = intersectAreaLight(sc.area, ray);
            if (tl > 0 && (!hit || tl < h.t)) {                                                   // the emitter is opaque
                if (!sc.nee || bounce == 0) add(throughput * sc.area.radiance, bounce >= 2);
                else if (sc.mis) {
                    float cosL = dot(sc.area.normal, ray.d * -1.0f);
                    float w = powerHeuristic(prevBsdfPdf, lightPdfSolidAngle(sc.area, tl * tl, cosL));
                    add(throughput * (sc.area.radiance * w), bounce >= 2);
                }
                return L;
            }
        }
        if (!hit) { if (sc.envScale > 0) L += throughput * background(ray) * sc.envScale; return L; }
        if (bounce >= sc.maxDepth) return L;

        Material mtex;
        const Material& m = materialAt(sc, h, mtex);
        applyNormalMap(sc, h);
        bool front = dot(ray.d, h.ng) < 0;
        Vec3 Ng = front ? h.ng : -h.ng;                // the side the ray came from
        Vec3 N = front ? h.n : -h.n;
        float c = dot(N, Ng);
        if (c < 0.01f) N = normalize(N + Ng * (0.01f - c));
        if (sc.nee && hasLight) add(throughput * sampleDirectLight(sc, h.p, N, Ng, m.base, smp, sc.mis), bounce >= 1);
        throughput = throughput * m.base;               // metallic / transmission are ignored: everything is Lambertian
        if (sc.russianRoulette && bounce >= sc.rrStartDepth) {
            float p = std::min(0.95f, std::max(0.05f, std::max(throughput.x, std::max(throughput.y, throughput.z))));
            if (smp.next1D() >= p) return L;
            throughput = throughput * (1.0f / p);
        }
        Vec3 d = sampleCosineHemisphere(N, smp.next1D(), smp.next1D());
        if (dot(d, Ng) <= 0) return L;                  // smooth normal pushed the sample below the real surface
        prevBsdfPdf = bsdfPdfLambert(dot(N, d));
        ray = Ray{h.p + Ng * (EPS * 10), d};
    }
}

inline Vec3 pathTrace(const Scene& sc, Ray ray, Sampler& smp) {
    return sc.bsdfModel == BsdfModel::Pbr ? pathTracePbr(sc, ray, smp) : pathTraceLambert(sc, ray, smp);
}

// ---------------------------------------------------------------- camera
struct Camera {
    Vec3 pos, forward, right, up;
    float tanHalfFov;
    Camera(Vec3 eye, Vec3 target, float vfovDeg) {
        pos = eye;
        forward = normalize(target - eye);
        right = normalize(cross(forward, Vec3(0, 1, 0)));
        up = cross(right, forward);
        tanHalfFov = std::tan(vfovDeg * 0.5f * 3.14159265f / 180.0f);
    }
    // glTF camera: looks down -Z, +Y up. The horizontal field of view of the file is kept,
    // so a 16:9 camera still frames the same width in the 4:3 framebuffer.
    explicit Camera(const CameraInfo& c) {
        pos = c.world.point(Vec3(0, 0, 0));
        Vec3 f = c.world.point(Vec3(0, 0, -1)) - pos;
        Vec3 u = c.world.point(Vec3(0, 1, 0)) - pos;
        forward = normalize(f);
        right = normalize(cross(forward, u));
        up = cross(right, forward);
        tanHalfFov = std::tan(c.yfov * 0.5f) * c.aspect / ((float)WIDTH / HEIGHT);
    }
    Ray ray(float px, float py) const {  // pixel coords, (0,0) = top-left
        float aspect = (float)WIDTH / HEIGHT;
        float sx = (2 * px / WIDTH - 1) * aspect * tanHalfFov;
        float sy = (1 - 2 * py / HEIGHT) * tanHalfFov;
        return {pos, normalize(forward + right * sx + up * sy)};
    }
};

// ---------------------------------------------------------------- film
// The film stores LINEAR radiance (float, may exceed 1) and the number of samples summed per pixel.
// Everything that depends on how the image is viewed (exposure, tone mapping, gamma, 8-bit) lives in
// Display below, so the same film can be shown differently without re-rendering.
struct Film {
    int w, h;
    std::vector<Vec3> sum;
    std::vector<int> count;
    Film(int w, int h) : w(w), h(h), sum((size_t)w * h), count((size_t)w * h, 0) {}
    void add(int x, int y, const Vec3& radiance) { sum[(size_t)y * w + x] += radiance; count[(size_t)y * w + x]++; }
    // Mean of the samples, still linear. (Average here, never after gamma.)
    Vec3 pixel(int x, int y) const {
        size_t i = (size_t)y * w + x;
        return count[i] ? sum[i] * (1.0f / count[i]) : Vec3(0, 0, 0);
    }
};

struct Display {
    float exposure = 1.0f;   // linear multiplier applied before encoding
    float gamma = 2.2f;      // simple power curve (not the exact sRGB piecewise function)
    uint8_t encode(float linear) const {
        float c = std::max(0.0f, std::min(1.0f, linear * exposure));   // clamp = no tone mapping yet
        c = std::pow(c, 1.0f / gamma);
        return (uint8_t)(c * 255.0f + 0.5f);
    }
};

// ---------------------------------------------------------------- scene defaults + render loop
// The glb carries a camera but no usable light (Blender area lights are not exported), so the light is set here.
// Position = where Blender's Area_Light sits: (-4, -2, 5) Z-up -> (-4, 5, 2) Y-up.
// M1 step 1: the area light is read from the glb, but shading still uses a point light at its centre,
// so images are unchanged until the area sampling is added (steps 2-3).
inline void applyDefaultLighting(Scene& scene) {
    scene.light = {scene.area.valid ? scene.area.center : Vec3(-4.0f, 5.0f, 2.0f), Vec3(1.0f, 1.0f, 1.0f)};
    scene.ambient = Vec3(0.12f, 0.12f, 0.12f);
}

inline Camera makeCamera(const Scene& scene) {
    return scene.camera.valid ? Camera(scene.camera)
                              : Camera(Vec3(1.25f, 2.6f, 6.6f), Vec3(1.25f, 1.0f, 0.0f), 60.0f);
}

struct RenderSettings {
    unsigned threads = 1;
    int spp = 1;            // number of samples per pixel taken by this call
    int firstSample = 0;    // index of the first of them: a render can be split into passes [0,a), [a,b), ... and the film
                            // then holds exactly what one call with spp = b would have produced (progressive rendering)
    bool jitter = false;    // false: every sample goes through the pixel centre; true: uniformly random point inside the pixel
    uint64_t seed = 1;      // global random seed
};

// Progressive schedule: the total number of samples per pixel after each pass is 1, 4, 16, 64, ... (x4), the last pass
// ending exactly at `target`. nextPassTotal(done, target) is the total after the next pass.
inline int nextPassTotal(int done, int target) {
    int t = 1;
    while (t <= done) t *= 4;
    return std::min(t, target);
}

// Renders samples [firstSample, firstSample + spp) of every pixel and ADDS them to `film`, row by row, on settings.threads threads.
// rowDone (optional, HEIGHT entries) is set to 1 when a row is finished; quit (optional) aborts early.
// Counters of all threads are added to `total`.
// Every (pixel, sample) has its own Sampler stream and a pixel's samples are summed in a fixed order, so the
// result does not depend on the number of threads.
inline void renderFrame(const Scene& scene, const Camera& cam, Film& film, const RenderSettings& rs,
                        std::atomic<uint8_t>* rowDone, const std::atomic<bool>* quit, Stats& total) {
    std::atomic<int> nextRow{0};
    std::atomic<uint64_t> rays{0}, nodes{0}, tris{0}, shadow{0};
    auto worker = [&]() {
        tlStats = Stats{};
        for (;;) {
            int y = nextRow.fetch_add(1);
            if (y >= film.h || (quit && *quit)) break;
            for (int x = 0; x < film.w; x++) {
                uint64_t pixel = (uint64_t)y * film.w + x;
                for (int k = 0; k < rs.spp; k++) {
                    Sampler smp(rs.seed, pixel, (uint32_t)(rs.firstSample + k));
                    float px = x + 0.5f, py = y + 0.5f;
                    if (rs.jitter) { px = x + smp.next1D(); py = y + smp.next1D(); }
                    Ray ray = cam.ray(px, py);
                    film.add(x, y, scene.integrator == Integrator::Path ? pathTrace(scene, ray, smp) : trace(scene, ray, 0, smp));
                }
            }
            if (rowDone) rowDone[y].store(1);
        }
        rays += tlStats.rays; nodes += tlStats.nodeVisits; tris += tlStats.triTests; shadow += tlStats.shadowRays;
    };
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < std::max(1u, rs.threads); i++) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    total.rays += rays; total.nodeVisits += nodes; total.triTests += tris; total.shadowRays += shadow;
}
