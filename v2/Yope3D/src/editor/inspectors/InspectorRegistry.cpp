#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/TypeId.h"
#include "ecs/Components.h"
#include "world/Transform.h"

// Forward declarations of per-component draw functions.
void drawNameComponent                   (void*, EditorContext&, ecs::Entity);
void drawTransformComponent              (void*, EditorContext&, ecs::Entity);
void drawHullComponent                   (void*, EditorContext&, ecs::Entity);
void drawSphereFormComponent             (void*, EditorContext&, ecs::Entity);
void drawAABBFormComponent               (void*, EditorContext&, ecs::Entity);
void drawOBBFormComponent                (void*, EditorContext&, ecs::Entity);
void drawCapsuleFormComponent            (void*, EditorContext&, ecs::Entity);
void drawCylinderFormComponent           (void*, EditorContext&, ecs::Entity);
void drawCompoundColliderComponent       (void*, EditorContext&, ecs::Entity);
void drawMeshRendererComponent           (void*, EditorContext&, ecs::Entity);
void drawMaterialComponent               (void*, EditorContext&, ecs::Entity);
void drawLightSourceComponent            (void*, EditorContext&, ecs::Entity);
void drawAudioSourceComponent            (void*, EditorContext&, ecs::Entity);
void drawSpringConstraintComponent       (void*, EditorContext&, ecs::Entity);
void drawParentComponent                 (void*, EditorContext&, ecs::Entity);
void drawScriptComponent                 (void*, EditorContext&, ecs::Entity);
// UI component inspectors
void drawUITransformComponent            (void*, EditorContext&, ecs::Entity);
void drawUIBackgroundComponent           (void*, EditorContext&, ecs::Entity);
void drawUITexturedBackgroundComponent   (void*, EditorContext&, ecs::Entity);
void drawUICurvedBackgroundComponent     (void*, EditorContext&, ecs::Entity);
void drawUITextComponent                 (void*, EditorContext&, ecs::Entity);
void drawUIButtonComponent               (void*, EditorContext&, ecs::Entity);
void drawTextLabel3DComponent            (void*, EditorContext&, ecs::Entity);

std::vector<ComponentDrawer> g_drawers;

void registerAllInspectors() {
    g_drawers = {
        { ecs::typeId<ecs::Name>(),                  drawNameComponent                  },
        { ecs::typeId<Transform>(),                  drawTransformComponent             },
        { ecs::typeId<ecs::Hull>(),                  drawHullComponent                  },
        { ecs::typeId<ecs::SphereForm>(),            drawSphereFormComponent            },
        { ecs::typeId<ecs::AABBForm>(),              drawAABBFormComponent              },
        { ecs::typeId<ecs::OBBForm>(),               drawOBBFormComponent               },
        { ecs::typeId<ecs::CapsuleForm>(),           drawCapsuleFormComponent           },
        { ecs::typeId<ecs::CylinderForm>(),          drawCylinderFormComponent          },
        { ecs::typeId<ecs::CompoundCollider>(),      drawCompoundColliderComponent      },
        { ecs::typeId<ecs::MeshRenderer>(),          drawMeshRendererComponent          },
        { ecs::typeId<ecs::Material>(),              drawMaterialComponent              },
        { ecs::typeId<ecs::LightSource>(),           drawLightSourceComponent           },
        { ecs::typeId<ecs::AudioSource>(),           drawAudioSourceComponent           },
        { ecs::typeId<ecs::SpringConstraint>(),      drawSpringConstraintComponent      },
        { ecs::typeId<ecs::Parent>(),                drawParentComponent                },
        { ecs::typeId<ecs::ScriptComponent>(),       drawScriptComponent                },
        { ecs::typeId<ecs::UITransform>(),           drawUITransformComponent           },
        { ecs::typeId<ecs::UIBackground>(),          drawUIBackgroundComponent          },
        { ecs::typeId<ecs::UITexturedBackground>(),  drawUITexturedBackgroundComponent  },
        { ecs::typeId<ecs::UICurvedBackground>(),    drawUICurvedBackgroundComponent    },
        { ecs::typeId<ecs::UIText>(),                drawUITextComponent                },
        { ecs::typeId<ecs::UIButton>(),              drawUIButtonComponent              },
        { ecs::typeId<ecs::TextLabel3D>(),           drawTextLabel3DComponent           },
    };
}

bool drawComponent(ecs::TypeId tid, void* component, EditorContext& ctx, ecs::Entity e) {
    for (auto& d : g_drawers) {
        if (d.tid == tid) {
            d.draw(component, ctx, e);
            return true;
        }
    }
    return false;
}
#endif
