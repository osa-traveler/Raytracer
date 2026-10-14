// Whitted-style ray tracer: glTF(.glb) scene -> 640x480 framebuffer shown with SDL2.
#include <SDL.h>

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
static float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
static Vec3 normalize(const Vec3& a) { float l = length(a); return l > 0 ? a * (1.0f / l) : a; }
static Vec3 mix(const Vec3& a, const Vec3& b, float t) { return a * (1 - t) + b * t; }
static Vec3 reflect(const Vec3& d, const Vec3& n) { return d - n * (2 * dot(d, n)); }

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

static Mat4 makeTRS(const float t[3], const float q[4], const float s[3]) {
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

struct MeshInstance {
    Vec3 bmin{INF, INF, INF}, bmax{-INF, -INF, -INF};
    size_t first = 0, count = 0;
};

struct Ray { Vec3 o, d; };

struct Hit {
    float t = INF;
    Vec3 p, n;
    int material = -1;
};

struct PointLight { Vec3 pos; Vec3 color; };

struct Scene {
    std::vector<Material> materials;
    std::vector<Triangle> tris;
    std::vector<MeshInstance> meshes;
    PointLight light;
    Vec3 ambient;

    static bool hitAABB(const MeshInstance& m, const Ray& r, float tmax) {
        float t0 = 0, t1 = tmax;
        const float o[3] = {r.o.x, r.o.y, r.o.z}, d[3] = {r.d.x, r.d.y, r.d.z};
        const float lo[3] = {m.bmin.x, m.bmin.y, m.bmin.z}, hi[3] = {m.bmax.x, m.bmax.y, m.bmax.z};
        for (int a = 0; a < 3; a++) {
            float inv = 1.0f / d[a];
            float ta = (lo[a] - o[a]) * inv, tb = (hi[a] - o[a]) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        return true;
    }

    // Moller-Trumbore, double sided. Returns closest hit.
    bool intersect(const Ray& r, float tmax, Hit& hit) const {
        bool found = false;
        float best = tmax;
        const Triangle* bt = nullptr;
        float bu = 0, bv = 0;
        for (const auto& m : meshes) {
            if (!hitAABB(m, r, best)) continue;
            for (size_t i = m.first; i < m.first + m.count; i++) {
                const Triangle& tr = tris[i];
                Vec3 e1 = tr.p1 - tr.p0, e2 = tr.p2 - tr.p0;
                Vec3 pv = cross(r.d, e2);
                float det = dot(e1, pv);
                if (std::fabs(det) < 1e-12f) continue;
                float inv = 1.0f / det;
                Vec3 tv = r.o - tr.p0;
                float u = dot(tv, pv) * inv;
                if (u < 0 || u > 1) continue;
                Vec3 qv = cross(tv, e1);
                float v = dot(r.d, qv) * inv;
                if (v < 0 || u + v > 1) continue;
                float t = dot(e2, qv) * inv;
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

static Glb readGlb(const std::string& path) {
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
static std::vector<float> readFloats(const Glb& g, int acc, int comps) {
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

static std::vector<uint32_t> readIndices(const Glb& g, int acc) {
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

static void addMesh(Scene& sc, const Glb& g, int meshIdx, const Mat4& world) {
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

        MeshInstance mi;
        mi.first = sc.tris.size();
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            Triangle t;
            t.p0 = wp[idx[i]]; t.p1 = wp[idx[i + 1]]; t.p2 = wp[idx[i + 2]];
            if (nrm.empty()) t.n0 = t.n1 = t.n2 = normalize(cross(t.p1 - t.p0, t.p2 - t.p0));
            else { t.n0 = wn[idx[i]]; t.n1 = wn[idx[i + 1]]; t.n2 = wn[idx[i + 2]]; }
            t.material = mat;
            sc.tris.push_back(t);
            for (const Vec3* p : {&t.p0, &t.p1, &t.p2}) {
                mi.bmin = {std::min(mi.bmin.x, p->x), std::min(mi.bmin.y, p->y), std::min(mi.bmin.z, p->z)};
                mi.bmax = {std::max(mi.bmax.x, p->x), std::max(mi.bmax.y, p->y), std::max(mi.bmax.z, p->z)};
            }
        }
        mi.count = sc.tris.size() - mi.first;
        // pad flat/thin boxes so the slab test is robust
        mi.bmin = mi.bmin - Vec3(EPS, EPS, EPS);
        mi.bmax = mi.bmax + Vec3(EPS, EPS, EPS);
        sc.meshes.push_back(mi);
    }
}

static void walkNode(Scene& sc, const Glb& g, int ni, const Mat4& parent) {
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
    if (n.has("children"))
        for (size_t i = 0; i < n["children"].size(); i++) walkNode(sc, g, n["children"][i].i(), world);
}

static Scene loadScene(const std::string& path) {
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
    return sc;
}

// ---------------------------------------------------------------- shading
static float schlick(float cosTheta, float f0) {
    return f0 + (1 - f0) * std::pow(1 - cosTheta, 5.0f);
}

// Fraction of light reaching `p` from the light; glass casts a faint tinted shadow.
static Vec3 shadowTransmittance(const Scene& sc, const Vec3& p, const Vec3& n) {
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

static Vec3 background(const Ray& r) {
    float t = 0.5f * (r.d.y + 1.0f);
    return mix(Vec3(0.9f, 0.9f, 0.9f), Vec3(0.4f, 0.6f, 0.9f), t);
}

static Vec3 trace(const Scene& sc, const Ray& ray, int depth) {
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

    if (depth >= MAX_DEPTH) return color;

    if (m.transmission > 0.0f) {
        // Dielectric: Fresnel-weighted reflection + refraction (IOR 1.5)
        const float ior = 1.5f;
        float eta = front ? 1.0f / ior : ior;
        Vec3 rd = normalize(reflect(ray.d, N));
        float cosT2 = 1 - eta * eta * (1 - cosI * cosI);
        Vec3 reflected = trace(sc, {h.p + N * (EPS * 10), rd}, depth + 1);
        if (cosT2 < 0) {  // total internal reflection
            color += reflected;
        } else {
            float F = schlick(cosI, 0.04f);
            if (!front) F = schlick(std::sqrt(cosT2), 0.04f);
            Vec3 td = normalize(ray.d * eta + N * (eta * cosI - std::sqrt(cosT2)));
            Vec3 refracted = trace(sc, {h.p - N * (EPS * 10), td}, depth + 1);
            color += reflected * F + refracted * m.base * ((1 - F) * m.transmission);
        }
    } else if (m.roughness < 0.5f) {
        // Mirror-like reflection tinted by Fresnel (base color for metals)
        Vec3 f0 = mix(Vec3(0.04f, 0.04f, 0.04f), m.base, m.metallic);
        Vec3 F = f0 + (Vec3(1, 1, 1) - f0) * std::pow(1 - cosI, 5.0f);
        Vec3 rd = normalize(reflect(ray.d, N));
        color += F * trace(sc, {h.p + N * (EPS * 10), rd}, depth + 1) * (1 - m.roughness);
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
    Ray ray(float px, float py) const {  // pixel coords, (0,0) = top-left
        float aspect = (float)WIDTH / HEIGHT;
        float sx = (2 * px / WIDTH - 1) * aspect * tanHalfFov;
        float sy = (1 - 2 * py / HEIGHT) * tanHalfFov;
        return {pos, normalize(forward + right * sx + up * sy)};
    }
};

static uint8_t toByte(float c) {
    c = std::max(0.0f, std::min(1.0f, c));
    c = std::pow(c, 1.0f / 2.2f);
    return (uint8_t)(c * 255.0f + 0.5f);
}

// ---------------------------------------------------------------- main
int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "Scene/Test/Untitled.glb";
    Scene scene;
    try {
        scene = loadScene(path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed to load %s: %s\n", path.c_str(), e.what());
        return 1;
    }
    std::printf("Loaded %s: %zu meshes, %zu triangles, %zu materials\n", path.c_str(),
                scene.meshes.size(), scene.tris.size(), scene.materials.size());

    // The glb has no camera/lights, so they are defined here.
    scene.light = {Vec3(-1.5f, 5.5f, 2.0f), Vec3(1.0f, 1.0f, 1.0f)};
    scene.ambient = Vec3(0.12f, 0.12f, 0.12f);
    Camera cam(Vec3(1.25f, 2.6f, 6.6f), Vec3(1.25f, 1.0f, 0.0f), 60.0f);

    std::vector<uint32_t> fb(WIDTH * HEIGHT, 0xFF000000u);  // ARGB8888

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("Raytrace", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       WIDTH, HEIGHT, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    if (!win || !ren || !tex) {
        std::fprintf(stderr, "SDL error: %s\n", SDL_GetError());
        return 1;
    }

    // Render in worker threads, one scanline at a time.
    std::atomic<int> nextRow{0}, rowsDone{0};
    std::atomic<bool> quit{false};
    auto worker = [&]() {
        for (;;) {
            int y = nextRow.fetch_add(1);
            if (y >= HEIGHT || quit) return;
            for (int x = 0; x < WIDTH; x++) {
                Vec3 c = trace(scene, cam.ray(x + 0.5f, y + 0.5f), 0);
                fb[y * WIDTH + x] = 0xFF000000u | (toByte(c.x) << 16) | (toByte(c.y) << 8) | toByte(c.z);
            }
            rowsDone++;
        }
    };
    unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < nThreads; i++) pool.emplace_back(worker);

    bool saved = false;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        SDL_UpdateTexture(tex, nullptr, fb.data(), WIDTH * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        if (rowsDone >= HEIGHT && !saved) {
            saved = true;
            std::FILE* f = std::fopen("output.ppm", "wb");
            if (f) {
                std::fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
                for (uint32_t px : fb) {
                    uint8_t rgb[3] = {(uint8_t)(px >> 16), (uint8_t)(px >> 8), (uint8_t)px};
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
                std::printf("Done. Saved output.ppm (close window or press Esc to exit)\n");
            }
        }
        SDL_Delay(16);
    }
    quit = true;
    for (auto& t : pool) t.join();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
