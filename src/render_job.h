// Background rendering for the GUI (and testable without any window):
// loads a scene when its path changes, renders it on a worker thread, can be cancelled and restarted.
#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "raytracer.h"

// Everything the user can change between renders.
struct RenderParams {
    std::string scene = "Scene/Test/Untitled.glb";
    int spp = 1;                              // total samples per pixel to reach
    bool progressive = false;                 // render in passes (1, 4, 16, ... spp) and publish the image after each
    bool jitter = false;
    uint64_t seed = 1;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    int maxDepth = MAX_DEPTH;
    LightMode lightMode = LightMode::Area;   // Point = the M0 baseline light
    float lightSize = 0;                      // area light edge length in metres; 0 = as in the glb
    Integrator integrator = Integrator::Classic;
    bool nee = true;                          // Path: next event estimation
    float envScale = 0;                       // Path: sky brightness as environment light
    bool legacyNormals = false;               // true = M0 behaviour at silhouettes (see Scene::legacyNormals)
};

class RenderJob {
public:
    enum class State { Idle, Loading, Rendering, Done, Cancelled, Error };

    ~RenderJob() { cancel(); join(); }

    State state() const { return state_.load(); }
    bool busy() const { State s = state_.load(); return s == State::Loading || s == State::Rendering; }

    // Starts a render with a fresh film. Returns false (and does nothing) if one is still running.
    bool start(const RenderParams& p) {
        if (busy()) return false;
        join();
        params_ = p;
        film_ = std::make_unique<Film>(WIDTH, HEIGHT);
        rowDone_.reset(new std::atomic<uint8_t>[HEIGHT]);
        for (int y = 0; y < HEIGHT; y++) rowDone_[y] = 0;
        cancel_ = false;
        stats_ = Stats{};
        renderMs_ = 0;
        doneSpp_ = 0; targetSpp_ = p.spp; passes_ = 0; consistent_ = true;
        { std::lock_guard<std::mutex> lk(snapMu_); snapMean_.clear(); passLog_.clear(); }
        { std::lock_guard<std::mutex> lk(mu_); error_.clear(); }
        runs_++;
        state_ = State::Loading;
        thread_ = std::thread([this]() { run(); });
        return true;
    }

    // Adds `extraSpp` samples per pixel to a finished render, without resetting anything (same scene, camera, settings).
    // The film then equals a render that asked for the larger total from the start. Returns false if the job is busy,
    // was cancelled (the film is then uneven) or has not produced a film yet.
    bool extend(int extraSpp) {
        if (busy() || state_.load() != State::Done || !consistent_ || extraSpp < 1 || !film_) return false;
        join();
        targetSpp_ = doneSpp_.load() + extraSpp;
        params_.spp = targetSpp_;
        cancel_ = false;
        state_ = State::Rendering;
        thread_ = std::thread([this]() { renderPasses(); });
        return true;
    }

    void cancel() { cancel_ = true; }
    void join() { if (thread_.joinable()) thread_.join(); }
    // Blocks until the current render has finished, was cancelled or failed.
    void wait() { join(); }

    // The film and row flags of the latest start(). Rows flagged in rowDone() are complete and safe to read at any time.
    const Film* film() const { return film_.get(); }
    const std::atomic<uint8_t>* rowDone() const { return rowDone_.get(); }
    int rowsDone() const { int n = 0; if (rowDone_) for (int y = 0; y < HEIGHT; y++) n += rowDone_[y].load(); return n; }

    // Progressive rendering: samples per pixel finished so far / asked for, and the number of published passes.
    int doneSpp() const { return doneSpp_.load(); }
    int targetSpp() const { return targetSpp_.load(); }
    int passes() const { return passes_.load(); }
    // Copies the image published after the latest finished pass (mean radiance per pixel, row-major WIDTH x HEIGHT) and
    // returns the pass number (0 = nothing yet). Safe at any time; the film itself is only safe to read when idle.
    int copySnapshot(std::vector<Vec3>& out) const { std::lock_guard<std::mutex> lk(snapMu_); out = snapMean_; return snapPass_; }
    struct PassLog { int spp; double ms; };       // total samples per pixel after the pass, cumulative render time
    std::vector<PassLog> passLog() const { std::lock_guard<std::mutex> lk(snapMu_); return passLog_; }

    int runs() const { return runs_.load(); }
    double renderMs() const { return renderMs_.load(); }
    double loadMs() const { return loadMs_.load(); }
    Stats stats() const { return stats_; }               // valid once the render has finished
    RenderParams params() const { return params_; }
    std::string error() const { std::lock_guard<std::mutex> lk(mu_); return error_; }
    std::shared_ptr<Scene> scene() const { std::lock_guard<std::mutex> lk(mu_); return scene_; }
    std::shared_ptr<Camera> camera() const { std::lock_guard<std::mutex> lk(mu_); return camera_; }

private:
    void run() {
        if (!scene_ || loadedPath_ != params_.scene) {
            auto t0 = std::chrono::steady_clock::now();
            try {
                auto sc = std::make_shared<Scene>(loadScene(params_.scene));
                applyDefaultLighting(*sc);
                auto cam = std::make_shared<Camera>(makeCamera(*sc));
                std::lock_guard<std::mutex> lk(mu_);
                scene_ = sc; camera_ = cam; loadedPath_ = params_.scene;
            } catch (const std::exception& e) {
                { std::lock_guard<std::mutex> lk(mu_); error_ = std::string("cannot load ") + params_.scene + ": " + e.what(); }
                state_ = State::Error;
                return;
            }
            loadMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        scene_->maxDepth = params_.maxDepth;
        configureLight(*scene_, params_.lightMode, params_.lightSize);
        scene_->legacyNormals = params_.legacyNormals;
        scene_->integrator = params_.integrator;
        scene_->envScale = params_.envScale;
        scene_->nee = params_.nee;
        RenderSettings rs;
        rs.threads = params_.threads; rs.spp = params_.spp; rs.jitter = params_.jitter; rs.seed = params_.seed;
        state_ = State::Rendering;
        renderPasses();
    }

    // Renders from doneSpp_ up to targetSpp_ samples per pixel: in one pass, or (progressive) in passes of 1, 4, 16, ...
    // samples, publishing the image after each. A pixel's samples always get the indices 0, 1, 2, ... in order, so the
    // result is the same however the render is split into passes.
    void renderPasses() {
        RenderSettings rs;
        rs.threads = params_.threads; rs.jitter = params_.jitter; rs.seed = params_.seed;
        auto t0 = std::chrono::steady_clock::now();
        const double msBefore = renderMs_.load();
        auto elapsed = [&]() { return msBefore + std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
        while (doneSpp_.load() < targetSpp_.load() && !cancel_.load()) {
            int done = doneSpp_.load(), total = params_.progressive ? nextPassTotal(done, targetSpp_.load()) : targetSpp_.load();
            rs.firstSample = done; rs.spp = total - done;
            Stats st;
            renderFrame(*scene_, *camera_, *film_, rs, params_.progressive ? nullptr : rowDone_.get(), &cancel_, st);
            stats_.rays += st.rays; stats_.nodeVisits += st.nodeVisits; stats_.triTests += st.triTests;
            if (cancel_.load()) { consistent_ = false; break; }
            doneSpp_ = total;
            renderMs_ = elapsed();
            if (params_.progressive) {
                std::vector<Vec3> mean((size_t)WIDTH * HEIGHT);
                for (int y = 0; y < HEIGHT; y++) for (int x = 0; x < WIDTH; x++) mean[(size_t)y * WIDTH + x] = film_->pixel(x, y);
                std::lock_guard<std::mutex> lk(snapMu_);
                snapMean_ = std::move(mean);
                snapPass_ = ++passesCount_;
                passLog_.push_back({total, renderMs_.load()});
                passes_ = snapPass_;
            }
        }
        renderMs_ = elapsed();
        state_ = cancel_.load() ? State::Cancelled : State::Done;
    }

    RenderParams params_;
    std::unique_ptr<Film> film_;
    std::unique_ptr<std::atomic<uint8_t>[]> rowDone_;
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> cancel_{false};
    std::atomic<int> runs_{0};
    std::atomic<double> renderMs_{0}, loadMs_{0};
    Stats stats_;
    std::atomic<int> doneSpp_{0}, targetSpp_{0}, passes_{0};
    bool consistent_ = true;                     // false after a cancel: some pixels got more samples than others
    mutable std::mutex snapMu_;
    std::vector<Vec3> snapMean_;
    int snapPass_ = 0, passesCount_ = 0;
    std::vector<PassLog> passLog_;
    mutable std::mutex mu_;
    std::string error_, loadedPath_;
    std::shared_ptr<Scene> scene_;
    std::shared_ptr<Camera> camera_;
    std::thread thread_;
};
