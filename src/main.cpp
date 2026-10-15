// Whitted-style ray tracer: glTF(.glb) scene -> 640x480 image.
//
//   raytrace [scene.glb]                 interactive window (image + parameter panel)
//   raytrace [scene.glb] --headless      no window: render, save, exit
//
// Options (initial values of the panel, or the settings of a headless render):
//   --spp N  --jitter  --seed N  --threads N  --max-depth N  --exposure X  --gamma X
//   --integrator classic|path  --env X  --no-nee  --no-mis  --rr   (path = Monte Carlo path tracing, --max-depth = bounces; --env = sky as light, default 0)
//   --legacy-normals   (M0 behaviour at silhouettes: reproduces the M0 baseline images bit for bit)
//   --light point|area  --light-size METRES   (area = soft shadows; point = the M0 baseline; size 0 = as in the glb)
// Headless only:
//   --progressive / --no-progressive   (the window defaults to progressive, headless to not)
//   --progressive   render in passes of 1, 4, 16, ... spp up to --spp; with --out also writes PREFIX_sppN.png after each pass
//   --out PREFIX    write PREFIX.png (display image), PREFIX.pfm (linear float film), PREFIX.json (settings, timings, counters)
//                   (without --out the image is saved as output.ppm)
//   --repeat N      render N times to record timings
//   --note TEXT     free text stored in the json
// Window only:
//   --screenshot FILE.png      save the whole window after the first render finished, then exit
//   --gui-selftest PREFIX      scripted check of the Render button and the exposure slider (writes PREFIX_1/2/3.png, exit code 0 = pass)
//   raytrace --rmse A.pfm B.pfm   root-mean-square difference of two linear images
#include <chrono>
#include <cstdlib>

#include "gui.h"
#include "run_config.h"

struct Options {
    GuiOptions gui;
    std::string out;
    bool headless = false;
    int repeat = 1;
    bool progressiveSet = false;   // the window renders progressively unless told otherwise; headless renders do not
};

static bool parseArgs(int argc, char** argv, Options& o) {
    RenderParams& p = o.gui.params;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (a == "--headless") o.headless = true;
        else if (a == "--out" && (v = next())) o.out = v;
        else if (a == "--note" && (v = next())) o.gui.note = v;
        else if (a == "--repeat" && (v = next())) o.repeat = std::max(1, std::atoi(v));
        else if (a == "--threads" && (v = next())) p.threads = (unsigned)std::max(1, std::atoi(v));
        else if (a == "--exposure" && (v = next())) o.gui.display.exposure = (float)std::atof(v);
        else if (a == "--gamma" && (v = next())) o.gui.display.gamma = (float)std::atof(v);
        else if (a == "--spp" && (v = next())) p.spp = std::max(1, std::atoi(v));
        else if (a == "--jitter") p.jitter = true;
        else if (a == "--seed" && (v = next())) p.seed = (uint64_t)std::strtoull(v, nullptr, 10);
        else if (a == "--max-depth" && (v = next())) p.maxDepth = std::max(0, std::atoi(v));
        else if (a == "--light" && (v = next())) p.lightMode = std::string(v) == "point" ? LightMode::Point : LightMode::Area;
        else if (a == "--light-size" && (v = next())) p.lightSize = (float)std::atof(v);
        else if (a == "--integrator" && (v = next())) p.integrator = std::string(v) == "path" ? Integrator::Path : Integrator::Classic;
        else if (a == "--progressive") { p.progressive = true; o.progressiveSet = true; }
        else if (a == "--no-progressive") { p.progressive = false; o.progressiveSet = true; }
        else if (a == "--no-nee") p.nee = false;
        else if (a == "--mis") p.mis = true;
        else if (a == "--no-mis") p.mis = false;
        else if (a == "--rr") p.russianRoulette = true;
        else if (a == "--env" && (v = next())) p.envScale = (float)std::atof(v);
        else if (a == "--legacy-normals") p.legacyNormals = true;
        else if (a == "--screenshot" && (v = next())) o.gui.screenshot = v;
        else if (a == "--gui-selftest" && (v = next())) o.gui.selfTest = v;
        else if (a.rfind("--", 0) != 0) p.scene = a;
        else { std::fprintf(stderr, "unknown or incomplete option: %s\n", a.c_str()); return false; }
    }
    return true;
}

static double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static int runHeadless(const Options& opt) {
    const RenderParams& p = opt.gui.params;
    Scene scene;
    auto t0 = std::chrono::steady_clock::now();
    try {
        scene = loadScene(p.scene);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed to load %s: %s\n", p.scene.c_str(), e.what());
        return 1;
    }
    double loadMs = ms(t0, std::chrono::steady_clock::now());
    applyDefaultLighting(scene);
    scene.maxDepth = p.maxDepth;
    configureLight(scene, p.lightMode, p.lightSize);
    scene.legacyNormals = p.legacyNormals;
    scene.integrator = p.integrator;
    scene.envScale = p.envScale;
    scene.nee = p.nee;
    scene.mis = p.mis;
    scene.russianRoulette = p.russianRoulette;
    Camera cam = makeCamera(scene);
    std::printf("Loaded %s: %zu triangles, %zu materials, %zu BVH nodes, camera %s (%.0f ms)\n", p.scene.c_str(),
                scene.tris.size(), scene.materials.size(), scene.nodes.size(), scene.camera.valid ? "from file" : "default", loadMs);
    std::fflush(stdout);

    RenderSettings rs;
    rs.threads = p.threads; rs.spp = p.spp; rs.jitter = p.jitter; rs.seed = p.seed;
    std::vector<double> times;
    Stats stats;
    Film film(WIDTH, HEIGHT);
    for (int r = 0; r < opt.repeat; r++) {
        Film f(WIDTH, HEIGHT);
        Stats s;
        auto a = std::chrono::steady_clock::now();
        if (p.progressive) {       // passes of 1, 4, 16, ... spp into one film; the result equals a single pass of p.spp
            for (int done = 0; done < p.spp;) {
                int total = nextPassTotal(done, p.spp);
                RenderSettings pass = rs; pass.firstSample = done; pass.spp = total - done;
                renderFrame(scene, cam, f, pass, nullptr, nullptr, s);
                done = total;
                std::printf("  pass: %d spp  (%.1f ms)\n", done, ms(a, std::chrono::steady_clock::now()));
                if (!opt.out.empty()) writePNG(opt.out + "_spp" + std::to_string(done) + ".png", f, opt.gui.display);
            }
        } else {
            renderFrame(scene, cam, f, rs, nullptr, nullptr, s);
        }
        times.push_back(ms(a, std::chrono::steady_clock::now()));
        film = std::move(f); stats = s;
        std::printf("render %d/%d: %.1f ms\n", r + 1, opt.repeat, times.back());
    }
    if (opt.out.empty()) {
        if (writePPM("output.ppm", film, opt.gui.display)) std::printf("Saved output.ppm\n");
    } else {
        saveRender(opt.out, p, opt.gui.note, scene, cam, film, opt.gui.display, loadMs, times, stats);
        std::printf("Saved %s.png / .pfm / .json\n", opt.out.c_str());
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--rmse") {
        double r;
        if (!rmsePFM(argv[2], argv[3], r)) { std::fprintf(stderr, "cannot compare %s and %s\n", argv[2], argv[3]); return 1; }
        std::printf("%.6f\n", r);
        return 0;
    }
    Options opt;
    if (!parseArgs(argc, argv, opt)) return 1;
    if (!opt.headless && !opt.progressiveSet) opt.gui.params.progressive = true;
    return opt.headless ? runHeadless(opt) : runGui(opt.gui);
}
