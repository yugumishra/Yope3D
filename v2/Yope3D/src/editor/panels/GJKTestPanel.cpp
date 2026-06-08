#ifdef YOPE_EDITOR
#include "editor/panels/GJKTestPanel.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "world/World.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "physics/ColliderDiscrete.h"
#include "math/Vec3.h"
#include <imgui.h>
#include <unordered_map>
#include <string>

namespace {
constexpr ImVec4 kGreen{0.30f, 0.90f, 0.35f, 1.0f};
constexpr ImVec4 kRed  {0.95f, 0.30f, 0.30f, 1.0f};
constexpr ImVec4 kOrange{0.95f, 0.62f, 0.20f, 1.0f};
constexpr ImVec4 kGray {0.55f, 0.55f, 0.55f, 1.0f};

bool hasForm(ecs::Registry& reg, ecs::Entity e) {
    return reg.has<ecs::SphereForm>(e) || reg.has<ecs::AABBForm>(e) || reg.has<ecs::OBBForm>(e);
}
}

void GJKTestPanel::runOracle(EditorContext& ctx) {
    namespace CD = physics::ColliderDiscrete;
    auto& world = *ctx.world;
    auto& reg   = *ctx.registry;

    // ---- Gather: every Hull entity (for overlay sizing) and every collidable one. ----
    int                      hullCount = 0;
    std::vector<ecs::Entity> ents;
    for (auto [e, h] : reg.view<ecs::Hull>()) {
        (void)h;
        ++hullCount;
        if (hasForm(reg, e)) ents.push_back(e);
    }

    std::vector<ecs::Entity> primaries = ents;
    if (scope_ == Scope::SelectedVsAll && ctx.selection) {
        primaries.clear();
        for (auto e : ctx.selection->get())
            if (reg.valid(e) && hasForm(reg, e)) primaries.push_back(e);
    }

    auto nameOf = [&](ecs::Entity e) -> std::string {
        if (auto* n = reg.get<ecs::Name>(e)) if (n->value[0]) return std::string(n->value);
        return "Entity " + std::to_string(e.id);
    };

    mismatches_.clear();
    pairsTested_ = 0; disagreements_ = 0; agreeHits_ = 0;

    std::unordered_map<uint32_t, int>  severity;  // 0 disjoint, 1 hit, 2 disagreement
    std::unordered_map<uint32_t, bool> gjkHitAny;

    auto consider = [&](ecs::Entity a, ecs::Entity b) {
        bool g = CD::gjkBoolean(a, b, reg);
        bool s = CD::satBoolean(a, b, reg);
        ++pairsTested_;
        gjkHitAny[a.id] = gjkHitAny[a.id] || g;
        gjkHitAny[b.id] = gjkHitAny[b.id] || g;

        Verdict v = (g == s) ? (g ? Verdict::AgreeHit : Verdict::AgreeDisjoint)
                             : (g ? Verdict::FalsePositive : Verdict::FalseNegative);
        int sv = (v == Verdict::AgreeDisjoint) ? 0 : (v == Verdict::AgreeHit) ? 1 : 2;
        auto bump = [&](uint32_t id) { int& cur = severity[id]; if (sv > cur) cur = sv; };
        bump(a.id); bump(b.id);

        if (v == Verdict::AgreeHit) ++agreeHits_;
        if (v == Verdict::FalsePositive || v == Verdict::FalseNegative) {
            ++disagreements_;
            std::string label = nameOf(a) + "  vs  " + nameOf(b) +
                "   [GJK:" + (g ? "hit" : "miss") + "  SAT:" + (s ? "hit" : "miss") + "]";
            mismatches_.push_back({a, b, v, std::move(label)});
        }
    };

    if (scope_ == Scope::AllPairs) {
        for (size_t i = 0; i < ents.size(); ++i)
            for (size_t j = i + 1; j < ents.size(); ++j)
                consider(ents[i], ents[j]);
    } else {
        for (auto p : primaries)
            for (auto e : ents)
                if (p.id != e.id) consider(p, e);
    }

    // ---- Paint the debug overlay by verdict. ----
    // Rebuild only when the overlay is absent or out of sync with the hull set
    // (rebuild allocates GPU buffers + syncs the device — too costly per frame).
    if (!world.debugPhysics || world.debugMeshCount() != static_cast<size_t>(hullCount)) {
        world.debugPhysics = true;
        world.rebuildDebugMeshes();
    }
    world.clearDebugColors();
    for (auto e : ents) {
        math::Vec3 col;
        if (colorMode_ == ColorMode::RawGJK) {
            col = gjkHitAny[e.id] ? math::Vec3{1.0f, 0.1f, 0.1f}
                                  : math::Vec3{0.25f, 0.25f, 0.25f};
        } else {
            int sv = severity.count(e.id) ? severity[e.id] : 0;
            col = (sv == 2) ? math::Vec3{1.0f, 0.0f, 0.0f}
                : (sv == 1) ? math::Vec3{0.10f, 0.90f, 0.20f}
                            : math::Vec3{0.25f, 0.25f, 0.25f};
        }
        world.setDebugColor(e, col);
    }
    world.syncDebugMeshes();
}

void GJKTestPanel::draw(EditorContext& ctx) {
    if (!visible) return;
    ImGui::Begin("GJK Test", &visible);

    bool inPlayMode = ctx.playMode && *ctx.playMode;
    if (inPlayMode) {
        ImGui::TextColored(kOrange, "Stop play mode to run the GJK oracle.");
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(
        "Diffs the in-progress gjkIntersect() against the proven SAT routines "
        "(ground truth) over collidable pairs, and paints the physics overlay.");
    ImGui::Separator();

    ImGui::TextUnformatted("Color mode:"); ImGui::SameLine();
    if (ImGui::RadioButton("Oracle (vs SAT)", colorMode_ == ColorMode::Oracle)) colorMode_ = ColorMode::Oracle;
    ImGui::SameLine();
    if (ImGui::RadioButton("Raw GJK", colorMode_ == ColorMode::RawGJK)) colorMode_ = ColorMode::RawGJK;

    ImGui::TextUnformatted("Scope:"); ImGui::SameLine();
    if (ImGui::RadioButton("All pairs", scope_ == Scope::AllPairs)) scope_ = Scope::AllPairs;
    ImGui::SameLine();
    if (ImGui::RadioButton("Selected vs all", scope_ == Scope::SelectedVsAll)) scope_ = Scope::SelectedVsAll;

    ImGui::Checkbox("Live (re-run every frame)", &live_);
    ImGui::SameLine();
    if (ImGui::Button("Run Once")) runOracle(ctx);
    if (live_) runOracle(ctx);

    ImGui::Separator();

    // Legend
    if (colorMode_ == ColorMode::Oracle) {
        ImGui::TextColored(kRed,   "[red]");   ImGui::SameLine(); ImGui::TextUnformatted("GJK / SAT disagree (bug)");
        ImGui::TextColored(kGreen, "[green]"); ImGui::SameLine(); ImGui::TextUnformatted("both intersecting");
        ImGui::TextColored(kGray,  "[gray]");  ImGui::SameLine(); ImGui::TextUnformatted("both disjoint");
    } else {
        ImGui::TextColored(kRed,  "[red]");  ImGui::SameLine(); ImGui::TextUnformatted("gjkIntersect() == true");
        ImGui::TextColored(kGray, "[gray]"); ImGui::SameLine(); ImGui::TextUnformatted("gjkIntersect() == false");
    }

    ImGui::Separator();
    ImGui::Text("Pairs tested: %d", pairsTested_);
    ImGui::TextColored(kGreen, "Agree-intersecting: %d", agreeHits_);
    ImGui::TextColored(disagreements_ ? kRed : kGray, "Disagreements: %d", disagreements_);

    ImGui::Separator();
    ImGui::TextUnformatted("Disagreements (click to select the pair):");
    ImGui::BeginChild("gjk_mismatch_list", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < mismatches_.size(); ++i) {
        const auto& pr = mismatches_[i];
        ImVec4 c = (pr.verdict == Verdict::FalsePositive) ? kRed : kOrange;
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        if (ImGui::Selectable(pr.label.c_str()) && ctx.selection) {
            ctx.selection->set(pr.a);
            ctx.selection->add(pr.b);
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (mismatches_.empty() && pairsTested_ > 0)
        ImGui::TextColored(kGreen, "None — GJK matches SAT on every tested pair.");
    ImGui::EndChild();

    ImGui::End();
}
#endif // YOPE_EDITOR
