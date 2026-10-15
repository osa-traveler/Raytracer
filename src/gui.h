// Interactive window: image on the left, parameter panel (Dear ImGui) on the right.
#pragma once
#include <string>

#include "raytracer.h"
#include "render_job.h"

struct GuiOptions {
    RenderParams params;       // initial values of the panel
    Display display;           // initial exposure / gamma
    std::string note;
    std::string screenshot;    // if set: save the whole window as PNG after the first render finished, then exit
    std::string selfTest;      // if set: scripted run (render, click the Render button with new settings, change the exposure, check), PNGs named PREFIX_1/2/3.png
};

// Returns a process exit code (the self test returns non-zero on failure).
int runGui(const GuiOptions& options);
