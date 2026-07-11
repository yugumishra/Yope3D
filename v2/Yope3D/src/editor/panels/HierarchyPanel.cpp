#include "editor/panels/HierarchyPanel.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "editor/CommandHistory.h"
#include "editor/FileDialog.h"
#include "editor/commands/EntityLifecycleCommands.h"
#include "editor/commands/ReparentCommand.h"
#include "editor/commands/SetUIParentCommand.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/World.h"
#include "world/Transform.h"
#include "assets/Primitives.h"
#include "rendering/Light.h"
#include "rendering/ViewportTarget.h"
#include <imgui.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <functional>
#include <unordered_map>
#include <unordered_set>

void HierarchyPanel::draw(EditorContext& ctx) {
    if (!visible) return;
    ImGui::Begin("Hierarchy", &visible);

    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_A))
            ImGui::OpenPopup("##add_object");
    }

    if (ImGui::BeginPopup("##add_object")) {
        ImGui::TextDisabled("Add Object");
        ImGui::Separator();

        if (ImGui::MenuItem("Empty Object")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::RenderObject, math::Vec3{0.f, 0.f, 0.f},
                    math::Vec3{1.f, 1.f, 1.f}));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Import Model...")) {
            if (ctx.world && ctx.history) {
                if (auto p = FileDialog::openFile({{"Model", "glb,gltf,obj"}}, YOPE_ASSETS_DIR))
                    ctx.history->execute(ctx, std::make_unique<ImportModelCommand>(*p));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Sphere")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::Sphere, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.5f, 0.5f, 0.5f}, 1.0f, 0.5f));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("OBB")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::OBB, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.5f, 0.5f, 0.5f}, 1.0f));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("AABB")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::AABB, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.5f, 0.5f, 0.5f}, 1.0f));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Trigger Box")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::TriggerBox, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.5f, 0.5f, 0.5f}, 1.0f));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Capsule")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::Capsule, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.4f, 0.8f, 0.4f}, 1.0f));  // ext.x=radius, ext.y=halfHeight
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Cylinder")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::Cylinder, math::Vec3{0.f, 2.f, 0.f},
                    math::Vec3{0.4f, 0.8f, 0.4f}, 1.0f));  // ext.x=radius, ext.y=halfHeight
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Point Light")) {
            ecs::LightSource lp{};
            lp.type = 0;
            lp.position[0] = 0.f; lp.position[1] = 4.f; lp.position[2] = 0.f;
            lp.color[0] = 1.f; lp.color[1] = 1.f; lp.color[2] = 1.f;
            lp.intensity = 1.5f; lp.constant = 1.f; lp.linear = 0.09f; lp.quadratic = 0.032f;
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(EntityKind::PointLight, lp));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Directional Light")) {
            ecs::LightSource lp{};
            lp.type = 1;
            lp.direction[0] = -0.4f; lp.direction[1] = -1.f; lp.direction[2] = -0.3f;
            lp.color[0] = 1.f; lp.color[1] = 0.95f; lp.color[2] = 0.85f; lp.intensity = 1.f;
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(EntityKind::DirLight, lp));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Spot Light")) {
            ecs::LightSource lp{};
            lp.type = 2;
            lp.position[0] = 0.f; lp.position[1] = 5.f; lp.position[2] = 0.f;
            lp.direction[0] = 0.f; lp.direction[1] = -1.f; lp.direction[2] = 0.f;
            lp.color[0] = 1.f; lp.color[1] = 0.95f; lp.color[2] = 0.8f;
            lp.intensity = 2.f; lp.constant = 1.f; lp.linear = 0.09f; lp.quadratic = 0.032f;
            lp.innerConeAngle = 0.2f; lp.outerConeAngle = 0.4f;
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(EntityKind::SpotLight, lp));
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Audio Source")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::AudioSource, math::Vec3{0.f, 1.f, 0.f},
                    math::Vec3{1.f, 1.f, 1.f}));
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Static Floor")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::StaticAABB, math::Vec3{0.f, -0.1f, 0.f},
                    math::Vec3{10.f, 0.1f, 10.f}));
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("UI Background")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::UIBackground));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("UI Textured Background")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::UITexturedBackground));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("UI Curved Background")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::UICurvedBackground));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("UI Text")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::UIText));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("UI Button")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::UIButton));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Text Label (3D)")) {
            if (ctx.world && ctx.history)
                ctx.history->execute(ctx, std::make_unique<CreateEntityCommand>(
                    EntityKind::TextLabel3D, math::Vec3{0.f, 1.f, 0.f},
                    math::Vec3{1.f, 1.f, 1.f}));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::TextDisabled("Shift+A to add  |  %d entities",
                        ctx.registry ? static_cast<int>(ctx.registry->entityCount()) : 0);
    ImGui::Separator();

    if (!ctx.registry) { ImGui::End(); return; }

    // Partition entities: non-UI form the 3D hierarchy tree; UI stays a flat,
    // depth-sorted list (UI uses its own layering, not the transform hierarchy).
    std::vector<ecs::Entity> nonUI, uiEnts;
    std::unordered_set<uint32_t> nonUISet;
    for (auto [e, _sel] : ctx.registry->view<ecs::EditorSelectable>()) {
        if (ctx.registry->has<ecs::UITransform>(e)) uiEnts.push_back(e);
        else { nonUI.push_back(e); nonUISet.insert(e.id); }
    }
    std::sort(uiEnts.begin(), uiEnts.end(), [&](ecs::Entity a, ecs::Entity b) {
        auto* ta = ctx.registry->get<ecs::UITransform>(a);
        auto* tb = ctx.registry->get<ecs::UITransform>(b);
        return (ta ? ta->depth : 0) < (tb ? tb->depth : 0);
    });

    // parent → children map + root list. An entity whose parent isn't a drawn
    // (EditorSelectable, non-UI) entity is treated as a root so nothing is lost.
    std::unordered_map<uint32_t, std::vector<ecs::Entity>> childrenOf;
    std::vector<ecs::Entity> roots;
    for (ecs::Entity e : nonUI) {
        ecs::Entity par = ecs::NullEntity;
        if (auto* p = ctx.registry->get<ecs::Parent>(e); p && ctx.registry->valid(p->parent))
            par = p->parent;
        if (par != ecs::NullEntity && nonUISet.count(par.id)) childrenOf[par.id].push_back(e);
        else                                                  roots.push_back(e);
    }

    // Same parent → children partition for UI entities, so parented UI groups
    // nest under their group like the 3D tree instead of a flat depth-sorted
    // list. uiEnts is already globally depth-sorted (above), and that relative
    // order is preserved when entities are bucketed below, so per-parent
    // sibling order stays depth-sorted too without a second sort pass.
    std::unordered_set<uint32_t> uiSet;
    for (ecs::Entity e : uiEnts) uiSet.insert(e.id);
    std::unordered_map<uint32_t, std::vector<ecs::Entity>> uiChildrenOf;
    std::vector<ecs::Entity> uiRoots;
    for (ecs::Entity e : uiEnts) {
        ecs::Entity par = ecs::NullEntity;
        if (auto* p = ctx.registry->get<ecs::Parent>(e); p && ctx.registry->valid(p->parent))
            par = p->parent;
        if (par != ecs::NullEntity && uiSet.count(par.id)) uiChildrenOf[par.id].push_back(e);
        else                                               uiRoots.push_back(e);
    }

    // Flattened DFS order (3D roots → children, then UI roots → children).
    // Used for Shift+range selection.
    std::vector<ecs::Entity> ordered;
    ordered.reserve(nonUI.size() + uiEnts.size());
    {
        std::function<void(ecs::Entity, decltype(childrenOf)&)> collect =
            [&](ecs::Entity e, decltype(childrenOf)& kids) {
            ordered.push_back(e);
            auto it = kids.find(e.id);
            if (it != kids.end()) for (ecs::Entity c : it->second) collect(c, kids);
        };
        for (ecs::Entity r : roots)   collect(r, childrenOf);
        for (ecs::Entity r : uiRoots) collect(r, uiChildrenOf);
    }

    auto indexOf = [&](ecs::Entity e) -> int {
        for (int i = 0; i < static_cast<int>(ordered.size()); ++i)
            if (ordered[i] == e) return i;
        return -1;
    };

    // Apply selection semantics for a click on entity e (Shift range / Ctrl toggle / set).
    auto handleSelect = [&](ecs::Entity e) {
        if (!ctx.selection) return;
        bool shift = ImGui::GetIO().KeyShift;
        bool ctrl  = ImGui::GetIO().KeyCtrl;
        int anchorIdx = indexOf(shiftAnchor_);
        if (shift && anchorIdx >= 0) {
            int target = indexOf(e);
            int lo = std::min(anchorIdx, target);
            int hi = std::max(anchorIdx, target);
            ctx.selection->clear();
            for (int i = lo; i <= hi; ++i) ctx.selection->add(ordered[i]);
        } else if (ctrl) {
            ctx.selection->add(e);
            shiftAnchor_ = e;
        } else {
            ctx.selection->set(e);
            shiftAnchor_ = e;
        }
    };

    // Drag-drop payload id used to reparent within the 3D tree.
    static constexpr const char* kDragType = "YOPE_ENTITY";
    auto dropReparentOnto = [&](ecs::Entity newParent) {
        if (auto* pl = ImGui::AcceptDragDropPayload(kDragType)) {
            ecs::Entity dragged = *static_cast<const ecs::Entity*>(pl->Data);
            if (ctx.history && ReparentCommand::canReparent(*ctx.registry, dragged, newParent))
                ctx.history->execute(ctx, std::make_unique<ReparentCommand>(dragged, newParent));
        }
    };

    // Separate payload type for UI drag-drop — distinct strings mean an
    // AcceptDragDropPayload call for one kind never matches a drag of the
    // other, so 3D/UI reparenting can never cross without any extra checks.
    static constexpr const char* kUIDragType = "YOPE_UI_ENTITY";
    auto dropUIReparentOnto = [&](ecs::Entity newParent) {
        if (auto* pl = ImGui::AcceptDragDropPayload(kUIDragType)) {
            ecs::Entity dragged = *static_cast<const ecs::Entity*>(pl->Data);
            float sw = ctx.viewportTarget ? static_cast<float>(ctx.viewportTarget->width())  : 0.0f;
            float sh = ctx.viewportTarget ? static_cast<float>(ctx.viewportTarget->height()) : 0.0f;
            if (ctx.history && SetUIParentCommand::canReparent(*ctx.registry, dragged, newParent))
                ctx.history->execute(ctx, std::make_unique<SetUIParentCommand>(dragged, newParent, sw, sh));
        }
    };

    // Recursive tree node draw.
    std::function<void(ecs::Entity)> drawNode = [&](ecs::Entity e) {
        auto childIt = childrenOf.find(e.id);
        bool hasChildren = childIt != childrenOf.end() && !childIt->second.empty();

        char name[80];
        if (auto* n = ctx.registry->get<ecs::Name>(e)) std::snprintf(name, sizeof(name), "%s", n->value);
        else                                            std::snprintf(name, sizeof(name), "Entity #%u", e.id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_DefaultOpen;
        if (ctx.selection && ctx.selection->contains(e)) flags |= ImGuiTreeNodeFlags_Selected;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(e.id)),
                                      flags, "%s", name);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            handleSelect(e);

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(kDragType, &e, sizeof(e));
            ImGui::Text("%s", name);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) { dropReparentOnto(e); ImGui::EndDragDropTarget(); }

        if (ImGui::BeginPopupContextItem()) {
            if (ctx.registry->has<ecs::Parent>(e) && ImGui::MenuItem("Detach to Root")) {
                if (ctx.history && ReparentCommand::canReparent(*ctx.registry, e, ecs::NullEntity))
                    ctx.history->execute(ctx, std::make_unique<ReparentCommand>(e, ecs::NullEntity));
            }
            if (ImGui::MenuItem("Delete")) {
                if (ctx.history)
                    ctx.history->execute(ctx, std::make_unique<DeleteEntityCommand>(e));
            }
            ImGui::EndPopup();
        }

        if (hasChildren && open) {
            for (ecs::Entity c : childIt->second) drawNode(c);
            ImGui::TreePop();
        }
    };

    for (ecs::Entity r : roots) drawNode(r);

    // Recursive UI tree node draw — same TreeNodeEx/drag-drop/context-menu
    // shape as drawNode above (3D), just against uiChildrenOf/kUIDragType so
    // parented UI groups nest visually instead of a flat depth-sorted list.
    std::function<void(ecs::Entity)> drawUINode = [&](ecs::Entity e) {
        auto childIt = uiChildrenOf.find(e.id);
        bool hasChildren = childIt != uiChildrenOf.end() && !childIt->second.empty();

        char name[96];
        if (auto* n = ctx.registry->get<ecs::Name>(e))
            std::snprintf(name, sizeof(name), "[UI] %s##%u", n->value, e.id);
        else
            std::snprintf(name, sizeof(name), "[UI] Entity #%u##%u", e.id, e.id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_DefaultOpen;
        if (ctx.selection && ctx.selection->contains(e)) flags |= ImGuiTreeNodeFlags_Selected;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(e.id)),
                                      flags, "%s", name);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            handleSelect(e);

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(kUIDragType, &e, sizeof(e));
            ImGui::Text("%s", name);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) { dropUIReparentOnto(e); ImGui::EndDragDropTarget(); }

        if (ImGui::BeginPopupContextItem()) {
            if (ctx.registry->has<ecs::Parent>(e) && ImGui::MenuItem("Detach to Root")) {
                float sw = ctx.viewportTarget ? static_cast<float>(ctx.viewportTarget->width())  : 0.0f;
                float sh = ctx.viewportTarget ? static_cast<float>(ctx.viewportTarget->height()) : 0.0f;
                if (ctx.history && SetUIParentCommand::canReparent(*ctx.registry, e, ecs::NullEntity))
                    ctx.history->execute(ctx, std::make_unique<SetUIParentCommand>(e, ecs::NullEntity, sw, sh));
            }
            if (ImGui::MenuItem("Delete")) {
                if (ctx.history)
                    ctx.history->execute(ctx, std::make_unique<DeleteEntityCommand>(e));
            }
            ImGui::EndPopup();
        }

        if (hasChildren && open) {
            for (ecs::Entity c : childIt->second) drawUINode(c);
            ImGui::TreePop();
        }
    };

    // Drawn immediately after the 3D tree (not after the space-filling dummy
    // below) — otherwise a mostly-empty 3D tree leaves a huge invisible Dummy
    // that pushes this section below the visible window, making UI entities
    // look entirely absent from the Hierarchy on small/sparse scenes.
    if (!uiEnts.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("  UI Elements");
        for (ecs::Entity r : uiRoots) drawUINode(r);
    }

    // Empty space below everything is a drop target that detaches to root
    // (3D or UI, whichever payload type is actually being dragged).
    float avail = ImGui::GetContentRegionAvail().y;
    if (avail > 4.0f) {
        ImGui::Dummy(ImVec2(-1.0f, avail));
        if (ImGui::BeginDragDropTarget()) {
            dropReparentOnto(ecs::NullEntity);
            dropUIReparentOnto(ecs::NullEntity);
            ImGui::EndDragDropTarget();
        }
    }

    ImGui::End();
}
