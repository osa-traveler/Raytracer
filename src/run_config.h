// Writes the settings, timings and counters of a render next to its images (PREFIX.json), and saves a film.
#pragma once
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "imageio.h"
#include "render_job.h"

inline std::string jsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) { if (c == '"' || c == '\\') r += '\\'; r += c; }
    return r;
}

inline void writeRunConfig(const std::string& path, const RenderParams& p, const std::string& note, const Scene& scene,
                           const Camera& cam, const Display& disp, double loadMs, const std::vector<double>& renderMs,
                           const Stats& st) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::vector<double> sorted = renderMs;
    if (sorted.empty()) sorted.push_back(0);
    std::sort(sorted.begin(), sorted.end());
    double median = sorted[sorted.size() / 2];
#ifdef NDEBUG
    const char* build = "release (NDEBUG)";
#else
    const char* build = "debug";
#endif
    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"scene\": \"%s\",\n", jsonEscape(p.scene).c_str());
    std::fprintf(f, "  \"note\": \"%s\",\n", jsonEscape(note).c_str());
    std::fprintf(f, "  \"resolution\": [%d, %d],\n", WIDTH, HEIGHT);
    std::fprintf(f, "  \"samples_per_pixel\": %d,\n", p.spp);
    std::fprintf(f, "  \"pixel_sampling\": \"%s\",\n", p.jitter ? "uniform random point inside the pixel (jitter)" : "pixel centre");
    std::fprintf(f, "  \"rng\": {\"generator\": \"PCG32, one stream per (pixel, sample)\", \"seed\": %llu},\n", (unsigned long long)p.seed);
    std::fprintf(f, "  \"max_depth\": %d,\n", scene.maxDepth);
    std::fprintf(f, "  \"threads\": %u,\n", p.threads);
    std::fprintf(f, "  \"display\": {\"exposure\": %g, \"gamma\": %g, \"tone_mapping\": \"none (clamp)\"},\n", disp.exposure, disp.gamma);
    std::fprintf(f, "  \"light\": {\"position\": [%g, %g, %g], \"color\": [%g, %g, %g], \"falloff\": \"none\"},\n",
                 scene.light.pos.x, scene.light.pos.y, scene.light.pos.z, scene.light.color.x, scene.light.color.y, scene.light.color.z);
    std::fprintf(f, "  \"ambient\": [%g, %g, %g],\n", scene.ambient.x, scene.ambient.y, scene.ambient.z);
    if (scene.area.valid) {
        const AreaLight& al = scene.area;
        std::fprintf(f, "  \"area_light\": {\"center\": [%g, %g, %g], \"edge_u\": [%g, %g, %g], \"edge_v\": [%g, %g, %g], \"normal\": [%g, %g, %g], \"area\": %g, \"radiance\": %g, \"used_for_shading\": false},\n",
                     al.center.x, al.center.y, al.center.z, al.edgeU.x, al.edgeU.y, al.edgeU.z, al.edgeV.x, al.edgeV.y, al.edgeV.z,
                     al.normal.x, al.normal.y, al.normal.z, al.area, al.radiance);
    }
    std::fprintf(f, "  \"camera\": {\"from_file\": %s, \"position\": [%g, %g, %g], \"forward\": [%g, %g, %g], \"tan_half_fov_y\": %g},\n",
                 scene.camera.valid ? "true" : "false", cam.pos.x, cam.pos.y, cam.pos.z,
                 cam.forward.x, cam.forward.y, cam.forward.z, cam.tanHalfFov);
    std::fprintf(f, "  \"triangles\": %zu,\n  \"materials\": %zu,\n  \"bvh_nodes\": %zu,\n", scene.tris.size(), scene.materials.size(), scene.nodes.size());
    std::fprintf(f, "  \"build\": \"%s\",\n  \"compiler\": \"%s\",\n", build, jsonEscape(__VERSION__).c_str());
    std::fprintf(f, "  \"timing_ms\": {\"load_and_bvh\": %.2f, \"render_runs\": [", loadMs);
    for (size_t i = 0; i < renderMs.size(); i++) std::fprintf(f, "%s%.2f", i ? ", " : "", renderMs[i]);
    std::fprintf(f, "], \"render_min\": %.2f, \"render_median\": %.2f},\n", sorted.front(), median);
    std::fprintf(f, "  \"counters_per_frame\": {\"rays\": %llu, \"bvh_node_visits\": %llu, \"triangle_tests\": %llu}\n",
                 (unsigned long long)st.rays, (unsigned long long)st.nodeVisits, (unsigned long long)st.triTests);
    std::fprintf(f, "}\n");
    std::fclose(f);
}

// Saves PREFIX.png (display image), PREFIX.pfm (linear film) and PREFIX.json. Creates the folder if needed.
inline bool saveRender(const std::string& prefix, const RenderParams& p, const std::string& note, const Scene& scene,
                       const Camera& cam, const Film& film, const Display& disp, double loadMs,
                       const std::vector<double>& renderMs, const Stats& st) {
    std::filesystem::path dir = std::filesystem::path(prefix).parent_path();
    if (!dir.empty()) { std::error_code ec; std::filesystem::create_directories(dir, ec); }
    bool ok = writePNG(prefix + ".png", film, disp) && writePFM(prefix + ".pfm", film);
    writeRunConfig(prefix + ".json", p, note, scene, cam, disp, loadMs, renderMs, st);
    return ok;
}
