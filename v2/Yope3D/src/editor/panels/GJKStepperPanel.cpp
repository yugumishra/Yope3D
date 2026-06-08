#ifdef YOPE_EDITOR
#include "editor/panels/GJKStepperPanel.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "world/World.h"
#include "world/Transform.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "rendering/DebugLine.h"
#include <imgui.h>
#include <cmath>
#include <vector>
#include <algorithm>

namespace CD = physics::ColliderDiscrete;

namespace {
constexpr float kPi = 3.14159265358979f;

bool hasForm(ecs::Registry& reg, ecs::Entity e) {
    return reg.has<ecs::SphereForm>(e) || reg.has<ecs::AABBForm>(e) || reg.has<ecs::OBBForm>(e);
}

// Line-list builder in world space.
struct LineSink {
    std::vector<DebugLineVertex>& v;
    math::Vec3 origin;  // baked offset added to every point

    void seg(math::Vec3 a, math::Vec3 b, float r, float g, float bl, float al) {
        a = a + origin; b = b + origin;
        v.push_back({ a.x, a.y, a.z, r, g, bl, al });
        v.push_back({ b.x, b.y, b.z, r, g, bl, al });
    }
    void cross(math::Vec3 p, float s, float r, float g, float bl, float al) {
        seg({p.x - s, p.y, p.z}, {p.x + s, p.y, p.z}, r, g, bl, al);
        seg({p.x, p.y - s, p.z}, {p.x, p.y + s, p.z}, r, g, bl, al);
        seg({p.x, p.y, p.z - s}, {p.x, p.y, p.z + s}, r, g, bl, al);
    }
};

float len(math::Vec3 v) { return std::sqrt(v.dot(v)); }
}

void GJKStepperPanel::capturePair(EditorContext& ctx) {
    auto& reg = *ctx.registry;
    ecs::Entity picked[2]{};
    int n = 0;
    if (ctx.selection) {
        for (auto e : ctx.selection->get()) {
            if (n >= 2) break;
            if (reg.valid(e) && hasForm(reg, e)) picked[n++] = e;
        }
    }
    if (n < 2) { hasPair_ = false; ran_ = false; return; }
    a_ = picked[0];
    b_ = picked[1];
    hasPair_ = true;
    runTrace(ctx);
}

void GJKStepperPanel::runTrace(EditorContext& ctx) {
    auto& reg = *ctx.registry;
    if (!hasPair_ || !reg.valid(a_) || !reg.valid(b_)) { hasPair_ = false; ran_ = false; return; }

    // Support fn for the CSO cloud (re-captured so it tracks current transforms).
    support_ = CD::makeSupportFn(a_, b_, reg);
    // Run the REAL gjkIntersect with recording on; we scrub the frames it logs.
    hit_   = CD::gjkTrace(a_, b_, reg, trace_);
    frame_ = 0;
    ran_   = true;
}

void GJKStepperPanel::buildLines(EditorContext& ctx) {
    auto& world = *ctx.world;
    auto& reg   = *ctx.registry;

    if (!hasPair_ || !support_ || !reg.valid(a_) || !reg.valid(b_)) {
        world.clearDebugLines();
        return;
    }

    math::Vec3 offset{0.0f, 0.0f, 0.0f};
    if (offsetToPair_) {
        auto* tfa = reg.get<Transform>(a_);
        auto* tfb = reg.get<Transform>(b_);
        if (tfa && tfb) offset = (tfa->position + tfb->position) * 0.5f;
    }

    std::vector<DebugLineVertex> lines;
    lines.reserve(static_cast<size_t>(csoSamples_) * 6 + 256);
    LineSink sink{lines, offset};

    // ---- CSO point cloud (sample the real support over a Fibonacci sphere) ----
    float maxR = 1e-4f;
    std::vector<math::Vec3> cso;
    cso.reserve(csoSamples_);
    const float ga = kPi * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < csoSamples_; ++i) {
        float k     = i + 0.5f;
        float cosP  = 1.0f - 2.0f * k / csoSamples_;
        float sinP  = std::sqrt(std::max(0.0f, 1.0f - cosP * cosP));
        float theta = ga * i;
        math::Vec3 d{ sinP * std::cos(theta), sinP * std::sin(theta), cosP };
        math::Vec3 p = support_(d);
        cso.push_back(p);
        maxR = std::max(maxR, len(p));
    }

    // Current frame's recorded state (if any).
    const CD::GJKTraceFrame* f = nullptr;
    if (ran_ && !trace_.empty()) {
        frame_ = std::clamp(frame_, 0, static_cast<int>(trace_.size()) - 1);
        f = &trace_[frame_];
        for (int i = 0; i < f->simplexN; ++i) maxR = std::max(maxR, len(f->pts[i]));
        maxR = std::max(maxR, len(f->support));
    }

    float crossS = 0.012f * maxR;
    float vertS  = 0.040f * maxR;

    if (showCSO_)
        for (auto& p : cso) sink.cross(p, crossS, 0.25f, 0.65f, 0.80f, 0.5f);

    if (showOrigin_)
        sink.cross({0, 0, 0}, vertS * 1.3f, 1.0f, 1.0f, 1.0f, 1.0f);

    if (f && showSimplex_ && f->simplexN > 0) {
        const float ageCol[4][3] = {
            {1.0f, 1.0f, 0.1f},   // newest (a)
            {1.0f, 0.55f, 0.1f},  // b
            {1.0f, 0.2f, 0.2f},   // c
            {0.7f, 0.3f, 1.0f},   // d
        };
        for (int i = 0; i < f->simplexN; ++i) {
            int age = (f->simplexN - 1) - i;
            const float* c = ageCol[age < 4 ? age : 3];
            sink.cross(f->pts[i], vertS, c[0], c[1], c[2], 1.0f);
        }
        auto edge = [&](int i, int j) {
            sink.seg(f->pts[i], f->pts[j], 0.8f, 0.8f, 0.85f, 0.7f);
        };
        if (f->simplexN == 2) { edge(0, 1); }
        else if (f->simplexN == 3) { edge(0, 1); edge(1, 2); edge(2, 0); }
        else if (f->simplexN == 4) {
            edge(0, 1); edge(0, 2); edge(0, 3); edge(1, 2); edge(1, 3); edge(2, 3);
        }
    }

    if (f && showDir_ && f->dir.dot(f->dir) > 1e-12f) {
        math::Vec3 nd  = f->dir * (1.0f / len(f->dir));
        math::Vec3 tip = nd * maxR;
        sink.seg({0, 0, 0}, tip, 1.0f, 0.1f, 1.0f, 1.0f);
        math::Vec3 ref  = (std::abs(nd.y) < 0.9f) ? math::Vec3{0, 1, 0} : math::Vec3{1, 0, 0};
        math::Vec3 perp = nd.cross(ref);
        float pl = len(perp);
        if (pl > 1e-6f) {
            perp = perp * (1.0f / pl);
            float hl = 0.08f * maxR;
            sink.seg(tip, tip - nd * hl + perp * hl, 1.0f, 0.1f, 1.0f, 1.0f);
            sink.seg(tip, tip - nd * hl - perp * hl, 1.0f, 0.1f, 1.0f, 1.0f);
        }
    }

    if (f && showSupport_)
        sink.cross(f->support, vertS * 0.8f, 0.1f, 1.0f, 0.35f, 1.0f);

    world.setDebugLines(std::move(lines));
}

void GJKStepperPanel::draw(EditorContext& ctx) {
    if (!visible) {
        if (ctx.world && !ctx.world->getDebugLines().empty()) ctx.world->clearDebugLines();
        return;
    }
    ImGui::Begin("GJK Stepper", &visible);

    bool inPlayMode = ctx.playMode && *ctx.playMode;
    if (inPlayMode) {
        ImGui::TextColored(ImVec4(0.95f, 0.62f, 0.20f, 1.0f), "Stop play mode to use the stepper.");
        ctx.world->clearDebugLines();
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(
        "Replays the REAL gjkIntersect run (recorded frames). Capture two selected "
        "collidable entities, then scrub. The viewport shows the CSO (A-B), the "
        "simplex at this frame, the origin, and the search direction.");
    ImGui::Separator();

    // Pair capture
    if (ImGui::Button("Capture Selected Pair")) capturePair(ctx);
    ImGui::SameLine();
    if (hasPair_ && ctx.registry->valid(a_) && ctx.registry->valid(b_)) {
        auto nameOf = [&](ecs::Entity e) -> std::string {
            if (auto* n = ctx.registry->get<ecs::Name>(e)) if (n->value[0]) return n->value;
            return "Entity " + std::to_string(e.id);
        };
        ImGui::Text("Pair: %s  vs  %s", nameOf(a_).c_str(), nameOf(b_).c_str());
    } else {
        ImGui::TextDisabled("select two collidable entities, then Capture");
    }

    ImGui::Separator();

    // Scrub controls
    int frameCount = static_cast<int>(trace_.size());
    if (!hasPair_) ImGui::BeginDisabled();
    if (ImGui::Button("Re-run")) runTrace(ctx);
    ImGui::SameLine();
    if (ImGui::Button("|< First")) frame_ = 0;
    ImGui::SameLine();
    if (ImGui::Button("< Back")) frame_ = std::max(0, frame_ - 1);
    ImGui::SameLine();
    if (ImGui::Button("Step >")) frame_ = std::min(frameCount - 1, frame_ + 1);
    ImGui::SameLine();
    if (ImGui::Button("End >|")) frame_ = frameCount - 1;
    if (!hasPair_) ImGui::EndDisabled();

    if (frameCount > 0) {
        ImGui::SetNextItemWidth(-1);
        int f1 = frame_;
        if (ImGui::SliderInt("##frame", &f1, 0, frameCount - 1, "frame %d")) frame_ = f1;
    }

    ImGui::Checkbox("Auto-play", &autoPlay_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("interval (s)", &autoInterval_, 0.05f, 1.5f, "%.2f");
    if (autoPlay_ && frameCount > 0 && frame_ < frameCount - 1) {
        autoTimer_ += ImGui::GetIO().DeltaTime;
        if (autoTimer_ >= autoInterval_) { autoTimer_ = 0.0f; ++frame_; }
    } else {
        autoTimer_ = 0.0f;
    }

    ImGui::Separator();

    // Overall verdict + current-frame readout
    if (ran_) {
        ImVec4 verdictCol = hit_ ? ImVec4(0.30f, 0.90f, 0.35f, 1.0f)
                                 : ImVec4(0.95f, 0.62f, 0.20f, 1.0f);
        ImGui::TextColored(verdictCol, "Final verdict: %s   (%d frames recorded)",
                           hit_ ? "INTERSECTION" : "no intersection", frameCount);

        if (frameCount > 0) {
            frame_ = std::clamp(frame_, 0, frameCount - 1);
            const auto& f = trace_[frame_];
            const char* tag = f.terminated ? "  [terminating probe: support did not pass origin]"
                            : f.early      ? "  [updateSimplex: origin contained — early-out]"
                                           : "";
            ImGui::Text("Frame %d / %d   simplex n=%d%s", frame_, frameCount - 1, f.simplexN, tag);
            ImGui::Text("dir = (% .3f, % .3f, % .3f)", f.dir.x, f.dir.y, f.dir.z);
            ImGui::Text("support = (% .3f, % .3f, % .3f)   support.dir = % .5f   (terminate if < %.5f)",
                        f.support.x, f.support.y, f.support.z, f.dotSD, physics::GJK_EPS);
            for (int i = 0; i < f.simplexN; ++i) {
                const char* atag = (i == f.simplexN - 1) ? " (a, newest)" : "";
                ImGui::Text("  v%d = (% .3f, % .3f, % .3f)%s", i,
                            f.pts[i].x, f.pts[i].y, f.pts[i].z, atag);
            }
        }
    } else {
        ImGui::TextDisabled("no run yet");
    }

    ImGui::Separator();

    ImGui::TextUnformatted("Show:");
    ImGui::SameLine(); ImGui::Checkbox("CSO", &showCSO_);
    ImGui::SameLine(); ImGui::Checkbox("Simplex", &showSimplex_);
    ImGui::SameLine(); ImGui::Checkbox("Origin", &showOrigin_);
    ImGui::SameLine(); ImGui::Checkbox("Direction", &showDir_);
    ImGui::SameLine(); ImGui::Checkbox("Support", &showSupport_);
    ImGui::Checkbox("Offset gizmo to pair midpoint", &offsetToPair_);
    ImGui::SetNextItemWidth(160);
    ImGui::SliderInt("CSO samples", &csoSamples_, 32, 1024);

    ImGui::TextDisabled("origin=white  a=yellow b=orange c=red d=purple  dir=magenta  support=green");

    buildLines(ctx);

    ImGui::End();
}
#endif // YOPE_EDITOR
