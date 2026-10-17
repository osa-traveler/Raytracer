// Named presets: the render settings (and the display exposure / gamma) saved to / loaded from a small JSON file, by default in presets/NAME.json.
// Used by the settings panel (Save preset / load preset) and by the command line (--preset NAME or --preset path/to/file.json).
//
// File format (all keys optional on load; a key that is missing leaves the current value alone, an unknown key is ignored):
//   { "preset_version": 1, "note": "free text", "scene": "Scene/Test/Beetle_Room_rear_tex.glb",
//     "spp": 1024, "progressive": true, "jitter": true, "seed": 1, "max_depth": 8, "light": "area", "light_size": 0,
//     "integrator": "path", "nee": true, "mis": true, "russian_roulette": true, "sky_light": 0, "bsdf": "pbr", "multiple_scattering": true,
//     "accel": "sah", "nearer_child_first": true, "any_hit_shadows": true, "legacy_normals": false, "cull_backfaces": true,
//     "textures": true, "normal_maps": true, "clamp_indirect": 0, "glossy_floor": 0, "glass_shadows": false, "exposure": 1, "gamma": 2.2 }
// Not stored: the number of threads (a property of the machine) and the white-furnace constant (a test setting).
// Loading is all or nothing: if the file cannot be read, is not valid JSON, or has a value of the wrong type / an unknown name, nothing changes.
#pragma once
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "imageio.h"
#include "render_job.h"

namespace preset {

inline const char* kDir = "presets";

// A name made safe for a file name: path separators and characters Windows rejects become '_', leading dots and spaces go. "" = nothing usable.
inline std::string sanitizeName(std::string n) {
    for (char& c : n) if (std::string("/\\:*?\"<>|").find(c) != std::string::npos || (unsigned char)c < 32) c = '_';
    size_t a = n.find_first_not_of(" .");
    if (a == std::string::npos) return "";
    size_t b = n.find_last_not_of(" ");
    return n.substr(a, b - a + 1);
}

// "beetle-final" -> presets/beetle-final.json; anything with a path separator, or ending in .json, is taken as a path.
inline std::string pathFor(const std::string& nameOrPath, const std::string& dir = kDir) {
    bool isPath = nameOrPath.find('/') != std::string::npos || nameOrPath.find('\\') != std::string::npos ||
                  (nameOrPath.size() > 5 && nameOrPath.compare(nameOrPath.size() - 5, 5, ".json") == 0);
    return isPath ? nameOrPath : dir + "/" + sanitizeName(nameOrPath) + ".json";
}

// The names (file names without .json) of the presets in `dir`, sorted.
inline std::vector<std::string> list(const std::string& dir = kDir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".json") out.push_back(e.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}

inline std::string jsonString(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if ((unsigned char)c < 32) o += ' ';
        else o += c;
    }
    return o + "\"";
}

inline bool save(const std::string& path, const RenderParams& p, const Display& d, const std::string& note, std::string* err = nullptr) {
    std::error_code ec;
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { if (err) *err = "cannot write " + path; return false; }
    auto b = [](bool v) { return v ? "true" : "false"; };
    std::fprintf(f, "{\n  \"preset_version\": 1,\n  \"note\": %s,\n  \"scene\": %s,\n", jsonString(note).c_str(), jsonString(p.scene).c_str());
    std::fprintf(f, "  \"spp\": %d,\n  \"progressive\": %s,\n  \"jitter\": %s,\n  \"seed\": %llu,\n  \"max_depth\": %d,\n", p.spp, b(p.progressive), b(p.jitter),
                 (unsigned long long)p.seed, p.maxDepth);
    std::fprintf(f, "  \"light\": \"%s\",\n  \"light_size\": %g,\n  \"integrator\": \"%s\",\n", p.lightMode == LightMode::Area ? "area" : "point", p.lightSize,
                 p.integrator == Integrator::Path ? "path" : "classic");
    std::fprintf(f, "  \"nee\": %s,\n  \"mis\": %s,\n  \"russian_roulette\": %s,\n  \"sky_light\": %g,\n  \"bsdf\": \"%s\",\n  \"multiple_scattering\": %s,\n",
                 b(p.nee), b(p.mis), b(p.russianRoulette), p.envScale, p.bsdf == BsdfModel::Pbr ? "pbr" : "lambert", b(p.multiScatter));
    std::fprintf(f, "  \"accel\": \"%s\",\n  \"nearer_child_first\": %s,\n  \"any_hit_shadows\": %s,\n  \"legacy_normals\": %s,\n  \"cull_backfaces\": %s,\n",
                 p.accel == Accel::None ? "none" : p.accel == Accel::Median ? "median" : "sah", b(p.orderedTraversal), b(p.anyHitShadows), b(p.legacyNormals), b(p.cullBackfaces));
    std::fprintf(f, "  \"textures\": %s,\n  \"normal_maps\": %s,\n  \"clamp_indirect\": %g,\n  \"glossy_floor\": %g,\n  \"glass_shadows\": %s,\n",
                 b(p.textures), b(p.normalMaps), p.clampIndirect, p.glossyFloor, b(p.glassShadows));
    std::fprintf(f, "  \"exposure\": %g,\n  \"gamma\": %g\n}\n", d.exposure, d.gamma);
    bool ok = std::fclose(f) == 0;
    if (!ok && err) *err = "error while writing " + path;
    return ok;
}

// Reads a preset into `p` and `d`. On any problem returns false, sets *err and leaves `p` and `d` untouched. Values are clamped to the ranges of the panel.
inline bool load(const std::string& path, RenderParams& p, Display& d, std::string* err = nullptr, std::string* note = nullptr) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("cannot open " + path);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    Json j;
    try { JsonParser jp{text.data(), text.data() + text.size()}; j = jp.parse(); }
    catch (const std::exception& e) { return fail(path + ": " + e.what()); }
    if (j.type != Json::Obj) return fail(path + ": not a JSON object");
    RenderParams q = p;
    Display e = d;
    std::string bad;
    auto num = [&](const char* key, auto lo, auto hi, auto& out) {
        const Json* v = j.find(key);
        if (!v) return;
        if (v->type != Json::Num) { bad = std::string("\"") + key + "\" must be a number"; return; }
        using T = std::remove_reference_t<decltype(out)>;
        out = (T)std::min((double)hi, std::max((double)lo, v->num));
    };
    auto flag = [&](const char* key, bool& out) {
        const Json* v = j.find(key);
        if (!v) return;
        if (v->type != Json::Bool) { bad = std::string("\"") + key + "\" must be true or false"; return; }
        out = v->b;
    };
    auto choice = [&](const char* key, std::initializer_list<const char*> names, int& out) {
        const Json* v = j.find(key);
        if (!v) return;
        int k = 0;
        for (const char* n : names) { if (v->type == Json::Str && v->str == n) { out = k; return; } k++; }
        bad = std::string("\"") + key + "\": unknown value";
    };
    if (const Json* v = j.find("scene")) { if (v->type == Json::Str) q.scene = v->str; else bad = "\"scene\" must be a string"; }
    num("spp", 1, 100000, q.spp);
    flag("progressive", q.progressive); flag("jitter", q.jitter);
    { double seed = (double)q.seed; num("seed", 0, 9007199254740992.0, seed); q.seed = (uint64_t)seed; }
    num("max_depth", 0, 16, q.maxDepth);
    { int k = q.lightMode == LightMode::Area ? 1 : 0; choice("light", {"point", "area"}, k); q.lightMode = k == 1 ? LightMode::Area : LightMode::Point; }
    num("light_size", 0.0, 100.0, q.lightSize);
    { int k = q.integrator == Integrator::Path ? 1 : 0; choice("integrator", {"classic", "path"}, k); q.integrator = k == 1 ? Integrator::Path : Integrator::Classic; }
    flag("nee", q.nee); flag("mis", q.mis); flag("russian_roulette", q.russianRoulette);
    num("sky_light", 0.0, 2.0, q.envScale);
    { int k = q.bsdf == BsdfModel::Pbr ? 1 : 0; choice("bsdf", {"lambert", "pbr"}, k); q.bsdf = k == 1 ? BsdfModel::Pbr : BsdfModel::Lambert; }
    flag("multiple_scattering", q.multiScatter);
    { int k = q.accel == Accel::None ? 0 : q.accel == Accel::Median ? 1 : 2; choice("accel", {"none", "median", "sah"}, k); q.accel = k == 0 ? Accel::None : k == 1 ? Accel::Median : Accel::Sah; }
    flag("nearer_child_first", q.orderedTraversal); flag("any_hit_shadows", q.anyHitShadows); flag("legacy_normals", q.legacyNormals); flag("cull_backfaces", q.cullBackfaces);
    flag("textures", q.textures); flag("normal_maps", q.normalMaps);
    num("clamp_indirect", 0.0, 1000.0, q.clampIndirect);
    num("glossy_floor", 0.0, 1.0, q.glossyFloor);
    flag("glass_shadows", q.glassShadows);
    num("exposure", 0.05, 16.0, e.exposure);
    num("gamma", 1.0, 3.0, e.gamma);
    if (!bad.empty()) return fail(path + ": " + bad);
    p = q; d = e;
    if (note) { const Json* v = j.find("note"); *note = v && v->type == Json::Str ? v->str : ""; }
    return true;
}

}  // namespace preset
