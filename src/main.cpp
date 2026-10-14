// Whitted-style ray tracer: glTF(.glb) scene -> 640x480 image, shown with SDL2 or written headless.
//
//   raytrace [scene.glb] [--headless] [--out PREFIX] [--repeat N] [--threads N] [--exposure X] [--note TEXT]
//            [--spp N] [--jitter] [--seed N]
//   raytrace --rmse A.pfm B.pfm      (root-mean-square difference of two linear images)
//
// Without --out the finished image is saved as output.ppm. With --out PREFIX it writes
// PREFIX.png (display image), PREFIX.pfm (linear float film) and PREFIX.json (settings, timings, counters).
#include <SDL.h>

#include <chrono>
#include <cstdlib>

#include "imageio.h"
#include "raytracer.h"

struct Options {
    std::string scene = "Scene/Test/Untitled.glb";
    std::string out;
    std::string note;
    bool headless = false;
    int repeat = 1;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    float exposure = 1.0f;
    int spp = 1;
    bool jitter = false;
    uint64_t seed = 1;
    RenderSettings settings() const { RenderSettings r; r.threads = threads; r.spp = spp; r.jitter = jitter; r.seed = seed; return r; }
};

static bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (a == "--headless") o.headless = true;
        else if (a == "--out" && (v = next())) o.out = v;
        else if (a == "--note" && (v = next())) o.note = v;
        else if (a == "--repeat" && (v = next())) o.repeat = std::max(1, std::atoi(v));
        else if (a == "--threads" && (v = next())) o.threads = (unsigned)std::max(1, std::atoi(v));
        else if (a == "--exposure" && (v = next())) o.exposure = (float)std::atof(v);
        else if (a == "--spp" && (v = next())) o.spp = std::max(1, std::atoi(v));
        else if (a == "--jitter") o.jitter = true;
        else if (a == "--seed" && (v = next())) o.seed = (uint64_t)std::strtoull(v, nullptr, 10);
        else if (a.rfind("--", 0) != 0) o.scene = a;
        else { std::fprintf(stderr, "unknown or incomplete option: %s\n", a.c_str()); return false; }
    }
    return true;
}

static double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static std::string jsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) { if (c == '"' || c == '\\') r += '\\'; r += c; }
    return r;
}

static void writeConfig(const std::string& path, const Options& o, const Scene& scene, const Camera& cam,
                        const Display& disp, double loadMs, const std::vector<double>& renderMs, const Stats& st) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::vector<double> sorted = renderMs;
    std::sort(sorted.begin(), sorted.end());
    double median = sorted[sorted.size() / 2];
#ifdef NDEBUG
    const char* build = "release (NDEBUG)";
#else
    const char* build = "debug";
#endif
    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"scene\": \"%s\",\n", jsonEscape(o.scene).c_str());
    std::fprintf(f, "  \"note\": \"%s\",\n", jsonEscape(o.note).c_str());
    std::fprintf(f, "  \"resolution\": [%d, %d],\n", WIDTH, HEIGHT);
    std::fprintf(f, "  \"samples_per_pixel\": %d,\n", o.spp);
    std::fprintf(f, "  \"pixel_sampling\": \"%s\",\n", o.jitter ? "uniform random point inside the pixel (jitter)" : "pixel centre");
    std::fprintf(f, "  \"rng\": {\"generator\": \"PCG32, one stream per (pixel, sample)\", \"seed\": %llu},\n", (unsigned long long)o.seed);
    std::fprintf(f, "  \"max_depth\": %d,\n", MAX_DEPTH);
    std::fprintf(f, "  \"threads\": %u,\n", o.threads);
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

static void saveOutputs(const Options& o, const Film& film, const Display& disp, const Scene& scene, const Camera& cam,
                        double loadMs, const std::vector<double>& renderMs, const Stats& st) {
    if (o.out.empty()) {
        if (writePPM("output.ppm", film, disp)) std::printf("Saved output.ppm\n");
        return;
    }
    writePNG(o.out + ".png", film, disp);
    writePFM(o.out + ".pfm", film);
    writeConfig(o.out + ".json", o, scene, cam, disp, loadMs, renderMs, st);
    std::printf("Saved %s.png / .pfm / .json\n", o.out.c_str());
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--rmse") {      // raytrace --rmse A.pfm B.pfm : difference of two linear images
        double r;
        if (!rmsePFM(argv[2], argv[3], r)) { std::fprintf(stderr, "cannot compare %s and %s\n", argv[2], argv[3]); return 1; }
        std::printf("%.6f\n", r);
        return 0;
    }
    Options opt;
    if (!parseArgs(argc, argv, opt)) return 1;

    Scene scene;
    auto t0 = std::chrono::steady_clock::now();
    try {
        scene = loadScene(opt.scene);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed to load %s: %s\n", opt.scene.c_str(), e.what());
        return 1;
    }
    double loadMs = ms(t0, std::chrono::steady_clock::now());
    applyDefaultLighting(scene);
    Camera cam = makeCamera(scene);
    Display disp;
    disp.exposure = opt.exposure;
    std::printf("Loaded %s: %zu triangles, %zu materials, %zu BVH nodes, camera %s (%.0f ms)\n", opt.scene.c_str(),
                scene.tris.size(), scene.materials.size(), scene.nodes.size(),
                scene.camera.valid ? "from file" : "default", loadMs);
    std::fflush(stdout);

    // ---------------- headless: render (possibly several times for timing), save, exit
    if (opt.headless) {
        std::vector<double> times;
        Stats stats;
        Film film(WIDTH, HEIGHT);
        for (int r = 0; r < opt.repeat; r++) {
            Film f(WIDTH, HEIGHT);
            Stats s;
            auto a = std::chrono::steady_clock::now();
            renderFrame(scene, cam, f, opt.settings(), nullptr, nullptr, s);
            times.push_back(ms(a, std::chrono::steady_clock::now()));
            film = std::move(f); stats = s;
            std::printf("render %d/%d: %.1f ms\n", r + 1, opt.repeat, times.back());
        }
        saveOutputs(opt, film, disp, scene, cam, loadMs, times, stats);
        return 0;
    }

    // ---------------- window: render in the background and show finished rows as they appear
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("Raytrace", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    if (!win || !ren || !tex) {
        std::fprintf(stderr, "SDL error: %s\n", SDL_GetError());
        return 1;
    }

    Film film(WIDTH, HEIGHT);
    Stats stats;
    std::vector<uint32_t> fb(WIDTH * HEIGHT, 0xFF000000u);  // ARGB8888, filled from the film for display only
    std::vector<std::atomic<uint8_t>> rowDone(HEIGHT);
    for (auto& r : rowDone) r = 0;
    std::vector<uint8_t> rowShown(HEIGHT, 0);
    std::atomic<bool> quit{false};
    auto start = std::chrono::steady_clock::now();
    double renderMs = 0;
    std::thread renderer([&]() {
        renderFrame(scene, cam, film, opt.settings(), rowDone.data(), &quit, stats);
        renderMs = ms(start, std::chrono::steady_clock::now());
    });

    bool saved = false, running = true;
    int rowsShown = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        for (int y = 0; y < HEIGHT; y++) {      // convert rows that the workers have finished
            if (rowShown[y] || !rowDone[y].load()) continue;
            for (int x = 0; x < WIDTH; x++) {
                Vec3 c = film.pixel(x, y);
                fb[y * WIDTH + x] = 0xFF000000u | (disp.encode(c.x) << 16) | (disp.encode(c.y) << 8) | disp.encode(c.z);
            }
            rowShown[y] = 1; rowsShown++;
        }
        SDL_UpdateTexture(tex, nullptr, fb.data(), WIDTH * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        if (rowsShown >= HEIGHT && !saved) {
            saved = true;
            renderer.join();  // all rows are done, so this returns right away
            std::printf("Rendered in %.1f ms (close window or press Esc to exit)\n", renderMs);
            saveOutputs(opt, film, disp, scene, cam, loadMs, {renderMs}, stats);
        }
        SDL_Delay(16);
    }
    quit = true;
    if (renderer.joinable()) renderer.join();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
