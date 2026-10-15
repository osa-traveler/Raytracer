// Whitted-style ray tracer core: glTF(.glb) scene loading, BVH, shading, camera, film.
// (Window / command line handling lives in main.cpp.)
#pragma once
#include <algorithm>
#include <atomic>
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
struct Material {
    Vec3 base{1, 1, 1};
    float metallic = 1.0f;     // glTF default
    float roughness = 1.0f;    // glTF default
    float transmission = 0.0f;
    std::string name;
};

struct Triangle {
    Vec3 p0, p1, p2;
    Vec3 n0, n1, n2;
    int material;
};

struct Ray { Vec3 o, d; };

struct Hit {
    float t = INF;
    Vec3 p, n;
    int material = -1;
};

struct PointLight { Vec3 pos; Vec3 color; };

// Per-thread counters (summed by the caller after rendering). Cheap enough to leave on.
struct Stats {
    uint64_t rays = 0;          // calls to Scene::intersect (camera, shadow, reflection, refraction)
    uint64_t nodeVisits = 0;    // BVH nodes whose box was tested
    uint64_t triTests = 0;      // ray-triangle tests
    Stats& operator+=(const Stats& o) { rays += o.rays; nodeVisits += o.nodeVisits; triTests += o.triTests; return *this; }
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
    std::vector<Triangle> tris;
    std::vector<BvhNode> nodes;
    std::vector<int> triOrder;
    PointLight light;
    Vec3 ambient;
    CameraInfo camera;
    AreaLight area;       // valid only if the glb defines one
    int maxDepth = MAX_DEPTH;   // reflection/refraction recursion limit (runtime setting)

    void buildBvh() {
        size_t n = tris.size();
        triOrder.resize(n);
        std::vector<Vec3> cen(n);
        for (size_t i = 0; i < n; i++) {
            triOrder[i] = (int)i;
            cen[i] = (tris[i].p0 + tris[i].p1 + tris[i].p2) * (1.0f / 3.0f);
        }
        nodes.clear();
        nodes.reserve(2 * n / 2 + 1);
        build(0, (int)n, cen);
    }

    int build(int begin, int end, const std::vector<Vec3>& cen) {
        int id = (int)nodes.size();
        nodes.emplace_back();
        Vec3 lo(INF, INF, INF), hi(-INF, -INF, -INF), clo = lo, chi = hi;
        for (int i = begin; i < end; i++) {
            const Triangle& t = tris[triOrder[i]];
            for (const Vec3* p : {&t.p0, &t.p1, &t.p2}) {
                lo = {std::min(lo.x, p->x), std::min(lo.y, p->y), std::min(lo.z, p->z)};
                hi = {std::max(hi.x, p->x), std::max(hi.y, p->y), std::max(hi.z, p->z)};
            }
            const Vec3& c = cen[triOrder[i]];
            clo = {std::min(clo.x, c.x), std::min(clo.y, c.y), std::min(clo.z, c.z)};
            chi = {std::max(chi.x, c.x), std::max(chi.y, c.y), std::max(chi.z, c.z)};
        }
        nodes[id].bmin = lo - Vec3(EPS, EPS, EPS);
        nodes[id].bmax = hi + Vec3(EPS, EPS, EPS);
        int count = end - begin;
        Vec3 ext = chi - clo;
        if (count <= 4 || std::max(ext.x, std::max(ext.y, ext.z)) <= 0) {
            nodes[id].first = begin; nodes[id].count = count;
            return id;
        }
        int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
        auto key = [&](int t) { const Vec3& c = cen[t]; return axis == 0 ? c.x : axis == 1 ? c.y : c.z; };
        int mid = (begin + end) / 2;
        std::nth_element(triOrder.begin() + begin, triOrder.begin() + mid, triOrder.begin() + end,
                         [&](int a, int b) { return key(a) < key(b); });
        build(begin, mid, cen);
        int right = build(mid, end, cen);
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
    bool intersect(const Ray& r, float tmax, Hit& hit) const {
        if (nodes.empty()) return false;
        tlStats.rays++;
        bool found = false;
        float best = tmax;
        const Triangle* bt = nullptr;
        float bu = 0, bv = 0;
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
                if (!intersectTriangle(r, tr, t, u, v)) continue;
                if (t > EPS && t < best) { best = t; bt = &tr; bu = u; bv = v; found = true; }
            }
        }
        if (found) {
            hit.t = best;
            hit.p = r.o + r.d * best;
            hit.n = normalize(bt->n0 * (1 - bu - bv) + bt->n1 * bu + bt->n2 * bv);
            hit.material = bt->material;
        }
        return found;
    }
};

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

        std::vector<Vec3> wp(nv), wn(nv);
        for (size_t i = 0; i < nv; i++) {
            wp[i] = world.point({pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]});
            if (!nrm.empty()) wn[i] = world.normal({nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]});
        }
        int mat = pr.has("material") ? pr["material"].i() : -1;
        if (mat < 0) { mat = (int)sc.materials.size(); sc.materials.push_back(Material{}); }

        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            Triangle t;
            t.p0 = wp[idx[i]]; t.p1 = wp[idx[i + 1]]; t.p2 = wp[idx[i + 2]];
            if (nrm.empty()) t.n0 = t.n1 = t.n2 = normalize(cross(t.p1 - t.p0, t.p2 - t.p0));
            else { t.n0 = wn[idx[i]]; t.n1 = wn[idx[i + 1]]; t.n2 = wn[idx[i + 2]]; }
            t.material = mat;
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
    for (size_t i = 0; i < mats.size(); i++) {
        Material m;
        m.name = mats[i]["name"].str;
        const Json& pbr = mats[i]["pbrMetallicRoughness"];
        if (pbr.has("baseColorFactor"))
            m.base = {pbr["baseColorFactor"][0].f(), pbr["baseColorFactor"][1].f(), pbr["baseColorFactor"][2].f()};
        if (pbr.has("metallicFactor")) m.metallic = pbr["metallicFactor"].f();
        if (pbr.has("roughnessFactor")) m.roughness = pbr["roughnessFactor"].f();
        const Json& ext = mats[i]["extensions"];
        if (ext.has("KHR_materials_transmission")) m.transmission = ext["KHR_materials_transmission"]["transmissionFactor"].f(0);
        sc.materials.push_back(m);
    }
    int sceneIdx = g.json.has("scene") ? g.json["scene"].i() : 0;
    const Json& roots = g.json["scenes"][sceneIdx]["nodes"];
    for (size_t i = 0; i < roots.size(); i++) walkNode(sc, g, roots[i].i(), Mat4{});
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

// Fraction of light reaching `p` from the light; glass casts a faint tinted shadow.
inline Vec3 shadowTransmittance(const Scene& sc, const Vec3& p, const Vec3& n) {
    Vec3 toL = sc.light.pos - p;
    float dist = length(toL);
    Ray r{p + n * (EPS * 10), toL * (1.0f / dist)};
    Vec3 T(1, 1, 1);
    float remaining = dist - EPS * 10;
    for (int i = 0; i < 8; i++) {
        Hit h;
        if (!sc.intersect(r, remaining, h)) return T;
        const Material& m = sc.materials[h.material];
        if (m.transmission <= 0.0f) return Vec3(0, 0, 0);
        T = T * m.base * (0.9f * m.transmission);
        r.o = h.p + r.d * (EPS * 10);
        remaining -= h.t + EPS * 10;
        if (remaining <= 0) return T;
    }
    return T;
}

inline Vec3 background(const Ray& r) {
    float t = 0.5f * (r.d.y + 1.0f);
    return mix(Vec3(0.9f, 0.9f, 0.9f), Vec3(0.4f, 0.6f, 0.9f), t);
}

inline Vec3 trace(const Scene& sc, const Ray& ray, int depth, Sampler& smp) {
    (void)smp;   // not used yet: area-light sampling (M1 step 3) will draw its random numbers from here
    Hit h;
    if (!sc.intersect(ray, INF, h)) return background(ray);
    const Material& m = sc.materials[h.material];

    bool front = dot(ray.d, h.n) < 0;
    Vec3 N = front ? h.n : -h.n;
    Vec3 V = -ray.d;
    float cosI = std::max(0.0f, dot(N, V));
    Vec3 color(0, 0, 0);

    // Local lighting (diffuse + Blinn-Phong) for non-transmissive surfaces
    if (m.transmission < 1.0f) {
        Vec3 diffuseColor = m.base * (1 - m.metallic) * (1 - m.transmission);
        color += diffuseColor * sc.ambient;
        Vec3 L = normalize(sc.light.pos - h.p);
        float nl = dot(N, L);
        if (nl > 0) {
            Vec3 vis = shadowTransmittance(sc, h.p, N);
            Vec3 Hv = normalize(L + V);
            float shininess = 2.0f / std::max(0.002f, m.roughness * m.roughness * m.roughness * m.roughness) - 2.0f;
            shininess = std::min(shininess, 2000.0f);
            float spec = std::pow(std::max(0.0f, dot(N, Hv)), shininess);
            Vec3 f0 = mix(Vec3(0.04f, 0.04f, 0.04f), m.base, m.metallic);
            Vec3 lc = sc.light.color * vis;
            color += diffuseColor * lc * nl;
            color += f0 * lc * (spec * nl * (shininess + 8) / 25.0f);
        }
    }
    // Specular highlight on glass
    if (m.transmission > 0.0f) {
        Vec3 L = normalize(sc.light.pos - h.p);
        Vec3 Hv = normalize(L + V);
        float spec = std::pow(std::max(0.0f, dot(N, Hv)), 800.0f);
        if (spec > 0) color += shadowTransmittance(sc, h.p, N) * sc.light.color * (spec * 2.0f);
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
    int spp = 1;            // samples per pixel
    bool jitter = false;    // false: every sample goes through the pixel centre; true: uniformly random point inside the pixel
    uint64_t seed = 1;      // global random seed
};

// Renders settings.spp samples per pixel into `film`, row by row, on settings.threads threads.
// rowDone (optional, HEIGHT entries) is set to 1 when a row is finished; quit (optional) aborts early.
// Counters of all threads are added to `total`.
// Every (pixel, sample) has its own Sampler stream and a pixel's samples are summed in a fixed order, so the
// result does not depend on the number of threads.
inline void renderFrame(const Scene& scene, const Camera& cam, Film& film, const RenderSettings& rs,
                        std::atomic<uint8_t>* rowDone, const std::atomic<bool>* quit, Stats& total) {
    std::atomic<int> nextRow{0};
    std::atomic<uint64_t> rays{0}, nodes{0}, tris{0};
    auto worker = [&]() {
        tlStats = Stats{};
        for (;;) {
            int y = nextRow.fetch_add(1);
            if (y >= film.h || (quit && *quit)) break;
            for (int x = 0; x < film.w; x++) {
                uint64_t pixel = (uint64_t)y * film.w + x;
                for (int k = 0; k < rs.spp; k++) {
                    Sampler smp(rs.seed, pixel, (uint32_t)k);
                    float px = x + 0.5f, py = y + 0.5f;
                    if (rs.jitter) { px = x + smp.next1D(); py = y + smp.next1D(); }
                    film.add(x, y, trace(scene, cam.ray(px, py), 0, smp));
                }
            }
            if (rowDone) rowDone[y].store(1);
        }
        rays += tlStats.rays; nodes += tlStats.nodeVisits; tris += tlStats.triTests;
    };
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < std::max(1u, rs.threads); i++) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    total.rays += rays; total.nodeVisits += nodes; total.triTests += tris;
}
