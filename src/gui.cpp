#include "gui.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "run_config.h"

namespace {

constexpr int kImageWindowW = 660, kPanelW = 390, kWindowH = 520;

const char* stateName(RenderJob::State s) {
    switch (s) {
        case RenderJob::State::Idle: return "idle";
        case RenderJob::State::Loading: return "loading scene...";
        case RenderJob::State::Rendering: return "rendering...";
        case RenderJob::State::Done: return "done";
        case RenderJob::State::Cancelled: return "cancelled";
        case RenderJob::State::Error: return "error";
    }
    return "";
}

std::vector<std::string> findScenes() {
    std::vector<std::string> out;
    for (const char* dir : {"Scene/Test", "Assets"}) {
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".glb") out.push_back(e.path().generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Reads the window back (after ImGui drew into the renderer) and writes it as PNG.
bool saveWindowPng(SDL_Renderer* ren, const std::string& path) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(ren, &w, &h);
    std::vector<uint8_t> argb((size_t)w * h * 4), rgb((size_t)w * h * 3);
    if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, argb.data(), w * 4) != 0) return false;
    for (size_t i = 0; i < (size_t)w * h; i++) {       // ARGB8888 is stored as B, G, R, A in memory on little endian
        rgb[i * 3 + 0] = argb[i * 4 + 2]; rgb[i * 3 + 1] = argb[i * 4 + 1]; rgb[i * 3 + 2] = argb[i * 4 + 0];
    }
    return writePNGRgb(path, w, h, rgb);
}

}  // namespace

int runGui(const GuiOptions& opt) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("Raytrace", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       kImageWindowW + kPanelW, kWindowH, 0);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture* tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT) : nullptr;
    if (!win || !ren || !tex) {
        std::fprintf(stderr, "SDL error: %s\n", SDL_GetError());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;                    // do not write imgui.ini
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer2_Init(ren);

    // ---- panel state
    RenderParams params = opt.params;
    Display disp = opt.display;
    char scenePath[512];
    std::snprintf(scenePath, sizeof scenePath, "%s", params.scene.c_str());
    char savePrefix[256] = "experiments/gui/render";
    std::vector<std::string> scenes = findScenes();
    int seedInput = (int)params.seed;
    std::string saveMessage;

    RenderJob job;
    std::vector<uint32_t> fb(WIDTH * HEIGHT, 0xFF101010u);     // what the image area shows (ARGB8888)
    std::vector<uint8_t> rowShown(HEIGHT, 0);
    int shownRun = 0;
    bool displayDirty = false;

    auto startRender = [&]() {
        params.scene = scenePath;
        params.seed = (uint64_t)std::max(0, seedInput);
        if (job.start(params)) {
            std::fill(fb.begin(), fb.end(), 0xFF101010u);
            std::fill(rowShown.begin(), rowShown.end(), 0);
            shownRun = job.runs();
            saveMessage.clear();
        }
    };
    startRender();     // like before: the window starts rendering right away

    // ---- scripted self test (for verifying the GUI without a person): see GuiOptions::selfTest
    enum class Stage { WaitFirst, Hover, Press, Release, WaitSecond, Exposure, ExposureWait, Finished };
    uint32_t pixelBefore = 0;
    double renderMsBefore = 0;
    Stage stage = Stage::WaitFirst;
    int stageFrames = 0;
    bool selfTestOk = true;
    ImVec2 renderButtonCentre(0, 0);
    bool pendingShot = false;
    std::string shotPath;
    bool screenshotDone = false;

    bool running = true;
    int exitCode = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !io.WantTextInput) running = false;
        }

        // ---- copy newly finished rows of the film into the image (display transform = exposure + gamma)
        if (job.runs() != shownRun) {                           // a new render started: restart the display
            std::fill(rowShown.begin(), rowShown.end(), 0);
            shownRun = job.runs();
        }
        if (displayDirty) { std::fill(rowShown.begin(), rowShown.end(), 0); displayDirty = false; }
        if (const Film* film = job.film()) {
            const std::atomic<uint8_t>* done = job.rowDone();
            for (int y = 0; y < HEIGHT; y++) {
                if (rowShown[y] || !done[y].load()) continue;
                for (int x = 0; x < WIDTH; x++) {
                    Vec3 c = film->pixel(x, y);
                    fb[(size_t)y * WIDTH + x] = 0xFF000000u | (disp.encode(c.x) << 16) | (disp.encode(c.y) << 8) | disp.encode(c.z);
                }
                rowShown[y] = 1;
            }
        }
        SDL_UpdateTexture(tex, nullptr, fb.data(), WIDTH * 4);

        // ---- UI
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(kImageWindowW, kWindowH));
        ImGui::Begin("Image", nullptr, fixed | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(WIDTH, HEIGHT));
        {
            int rows = job.rowsDone();
            ImGui::ProgressBar(rows / (float)HEIGHT, ImVec2(WIDTH, 0), job.state() == RenderJob::State::Rendering ? "rendering" : stateName(job.state()));
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(kImageWindowW, 0));
        ImGui::SetNextWindowSize(ImVec2(kPanelW, kWindowH));
        ImGui::Begin("Controls", nullptr, fixed);
        const bool busy = job.busy();

        ImGui::SeparatorText("Render settings (press Render to apply)");
        ImGui::InputText("scene", scenePath, sizeof scenePath);
        if (ImGui::BeginCombo("##scenes", "choose a scene...")) {
            for (const std::string& s : scenes)
                if (ImGui::Selectable(s.c_str())) std::snprintf(scenePath, sizeof scenePath, "%s", s.c_str());
            ImGui::EndCombo();
        }
        ImGui::InputInt("samples / pixel", &params.spp, 1, 16);
        params.spp = std::max(1, std::min(params.spp, 100000));
        ImGui::Checkbox("jitter (random point in pixel)", &params.jitter);
        ImGui::InputInt("seed", &seedInput);
        int threads = (int)params.threads;
        ImGui::SliderInt("threads", &threads, 1, (int)std::max(1u, std::thread::hardware_concurrency()));
        params.threads = (unsigned)threads;
        ImGui::SliderInt("max bounces", &params.maxDepth, 0, 16);

        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Render", ImVec2(120, 0))) startRender();
        renderButtonCentre = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                                    (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!busy);
        if (ImGui::Button("Cancel", ImVec2(120, 0))) job.cancel();
        ImGui::EndDisabled();

        ImGui::SeparatorText("Display (applies instantly, no re-render)");
        if (ImGui::SliderFloat("exposure", &disp.exposure, 0.05f, 16.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) displayDirty = true;
        if (ImGui::SliderFloat("gamma", &disp.gamma, 1.0f, 3.0f, "%.2f")) displayDirty = true;
        if (ImGui::Button("Reset display")) { disp.exposure = 1.0f; disp.gamma = 2.2f; displayDirty = true; }

        ImGui::SeparatorText("Status");
        ImGui::Text("state: %s", stateName(job.state()));
        if (job.state() == RenderJob::State::Error) ImGui::TextWrapped("%s", job.error().c_str());
        if (std::shared_ptr<Scene> sc = job.scene())
            ImGui::Text("scene: %zu triangles, area light %s", sc->tris.size(), sc->area.valid ? "found" : "none");
        if (job.state() == RenderJob::State::Done || job.state() == RenderJob::State::Cancelled) {
            Stats st = job.stats();
            ImGui::Text("render: %.1f ms   (%d spp, %s)", job.renderMs(), job.params().spp, job.params().jitter ? "jitter" : "centre");
            ImGui::Text("rays: %llu  (%.1f Mrays/s)", (unsigned long long)st.rays, job.renderMs() > 0 ? st.rays / (job.renderMs() * 1000.0) : 0.0);
        }

        ImGui::SeparatorText("Save");
        ImGui::InputText("file prefix", savePrefix, sizeof savePrefix);
        ImGui::BeginDisabled(busy || !job.film() || job.state() == RenderJob::State::Error || job.state() == RenderJob::State::Idle);
        if (ImGui::Button("Save PNG + PFM + JSON")) {
            auto sc = job.scene(); auto cam = job.camera();
            if (sc && cam) {
                bool ok = saveRender(savePrefix, job.params(), opt.note, *sc, *cam, *job.film(), disp, job.loadMs(), {job.renderMs()}, job.stats());
                saveMessage = ok ? std::string("saved ") + savePrefix + ".png / .pfm / .json" : "could not write the files";
            }
        }
        ImGui::EndDisabled();
        if (!saveMessage.empty()) ImGui::TextWrapped("%s", saveMessage.c_str());
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);

        if (pendingShot) {                                       // capture before presenting: this frame is what the window shows
            pendingShot = false;
            if (!saveWindowPng(ren, shotPath)) { std::fprintf(stderr, "could not save %s\n", shotPath.c_str()); selfTestOk = false; }
            else std::printf("saved %s\n", shotPath.c_str());
            screenshotDone = true;
        }
        SDL_RenderPresent(ren);

        // ---- scripted modes
        auto frameAfterDone = [&]() { return job.state() == RenderJob::State::Done && ++stageFrames > 2; };
        if (!opt.selfTest.empty()) {
            switch (stage) {
                case Stage::WaitFirst:
                    if (frameAfterDone()) {
                        shotPath = opt.selfTest + "_1.png"; pendingShot = true;
                        params.spp = 4; params.jitter = true;      // "the user edited the panel"
                        stage = Stage::Hover; stageFrames = 0;
                    }
                    break;
                case Stage::Hover:                                   // move the mouse onto the Render button
                    if (!pendingShot && screenshotDone) {
                        SDL_Event m{}; m.type = SDL_MOUSEMOTION; m.motion.windowID = SDL_GetWindowID(win);
                        m.motion.x = (int)renderButtonCentre.x; m.motion.y = (int)renderButtonCentre.y;
                        SDL_PushEvent(&m);
                        if (++stageFrames > 2) { stage = Stage::Press; stageFrames = 0; }
                    }
                    break;
                case Stage::Press: {
                    SDL_Event b{}; b.type = SDL_MOUSEBUTTONDOWN; b.button.windowID = SDL_GetWindowID(win);
                    b.button.button = SDL_BUTTON_LEFT; b.button.state = SDL_PRESSED; b.button.clicks = 1;
                    b.button.x = (int)renderButtonCentre.x; b.button.y = (int)renderButtonCentre.y;
                    SDL_PushEvent(&b);
                    stage = Stage::Release; stageFrames = 0;
                    break;
                }
                case Stage::Release: {
                    if (++stageFrames < 2) break;
                    SDL_Event b{}; b.type = SDL_MOUSEBUTTONUP; b.button.windowID = SDL_GetWindowID(win);
                    b.button.button = SDL_BUTTON_LEFT; b.button.state = SDL_RELEASED; b.button.clicks = 1;
                    b.button.x = (int)renderButtonCentre.x; b.button.y = (int)renderButtonCentre.y;
                    SDL_PushEvent(&b);
                    stage = Stage::WaitSecond; stageFrames = 0;
                    break;
                }
                case Stage::WaitSecond:
                    if (job.runs() >= 2 && frameAfterDone()) {
                        shotPath = opt.selfTest + "_2.png"; pendingShot = true; screenshotDone = false;
                        stage = Stage::Exposure; stageFrames = 0;
                    } else if (job.runs() < 2 && ++stageFrames > 400) {
                        std::fprintf(stderr, "self test FAILED: the Render button did not start a second render\n");
                        selfTestOk = false; running = false;
                    }
                    break;
                case Stage::Exposure:                                // change the display only: must not trigger a render
                    if (screenshotDone) {
                        pixelBefore = fb[(size_t)400 * WIDTH + 320];     // a floor pixel
                        renderMsBefore = job.renderMs();
                        disp.exposure = 0.25f; displayDirty = true;
                        stage = Stage::ExposureWait; stageFrames = 0;
                    }
                    break;
                case Stage::ExposureWait:
                    if (++stageFrames == 3) {
                        uint32_t after = fb[(size_t)400 * WIDTH + 320];
                        auto lum = [](uint32_t c) { return (int)((c >> 16) & 255) + (int)((c >> 8) & 255) + (int)(c & 255); };
                        bool darker = lum(after) < lum(pixelBefore) * 0.7;
                        bool noRerender = job.runs() == 2 && job.renderMs() == renderMsBefore && job.state() == RenderJob::State::Done;
                        std::printf("self test: exposure 1.0 -> 0.25: floor pixel brightness %d -> %d (%s), re-rendered: %s\n",
                                    lum(pixelBefore), lum(after), darker ? "darker" : "NOT darker", noRerender ? "no" : "YES");
                        if (!darker || !noRerender) selfTestOk = false;
                        shotPath = opt.selfTest + "_3.png"; pendingShot = true; screenshotDone = false;
                        stage = Stage::Finished; stageFrames = 0;
                    }
                    break;
                case Stage::Finished:
                    if (screenshotDone) {
                        const Film* f = job.film();
                        bool spp4 = f && f->count[0] == 4 && f->count[WIDTH * HEIGHT - 1] == 4;
                        bool oneFirst = job.params().spp == 4 && job.params().jitter;
                        std::printf("self test: runs=%d, samples per pixel in the film=%d (expected 4), params applied=%s\n",
                                    job.runs(), f ? f->count[0] : -1, oneFirst ? "yes" : "no");
                        if (!(job.runs() == 2 && spp4 && oneFirst)) selfTestOk = false;
                        std::printf("self test %s\n", selfTestOk ? "PASSED" : "FAILED");
                        exitCode = selfTestOk ? 0 : 1;
                        running = false;
                    }
                    break;
            }
        } else if (!opt.screenshot.empty() && !screenshotDone) {
            if (frameAfterDone()) { shotPath = opt.screenshot; pendingShot = true; }
        } else if (!opt.screenshot.empty() && screenshotDone) {
            running = false;
        }
        if (!opt.selfTest.empty() && !running && exitCode == 0 && !selfTestOk) exitCode = 1;
        SDL_Delay(opt.selfTest.empty() && opt.screenshot.empty() ? 8 : 4);
    }

    job.cancel();
    job.join();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return exitCode;
}
