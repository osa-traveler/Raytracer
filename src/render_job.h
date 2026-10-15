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
    int spp = 1;
    bool jitter = false;
    uint64_t seed = 1;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    int maxDepth = MAX_DEPTH;
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
        { std::lock_guard<std::mutex> lk(mu_); error_.clear(); }
        runs_++;
        state_ = State::Loading;
        thread_ = std::thread([this]() { run(); });
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
        RenderSettings rs;
        rs.threads = params_.threads; rs.spp = params_.spp; rs.jitter = params_.jitter; rs.seed = params_.seed;
        state_ = State::Rendering;
        auto t0 = std::chrono::steady_clock::now();
        Stats st;
        renderFrame(*scene_, *camera_, *film_, rs, rowDone_.get(), &cancel_, st);
        renderMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        stats_ = st;
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
    mutable std::mutex mu_;
    std::string error_, loadedPath_;
    std::shared_ptr<Scene> scene_;
    std::shared_ptr<Camera> camera_;
    std::thread thread_;
};
