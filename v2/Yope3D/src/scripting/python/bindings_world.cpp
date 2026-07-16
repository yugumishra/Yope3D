#ifdef YOPE_PYTHON
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "world/World.h"
#include "physics/Joint.h"
#include "physics/KinematicQuery.h"
#include "physics/CompoundShape.h"
#include "rendering/Camera.h"
#include "platform/Input.h"
#include "platform/Window.h"
#include "audio/AudioSystem.h"
#include "audio/Source.h"
#include "scene/SceneManager.h"
#include "platform/BundlePaths.h"
#include "physics/CollisionLayers.h"
#include "rendering/Light.h"
#include "assets/Primitives.h"
#include "world/RenderMesh.h"
#include "ecs/Components.h"
#include "math/Mat3.h"
#include "debug/Profiler.h"
#include <GLFW/glfw3.h>
#include <unordered_set>
#include <atomic>
#include <filesystem>

namespace py = pybind11;

// Opaque handle to a live physics::Joint*. NOT a direct py::class_<physics::Joint>
// binding: physics::Joint is a bare std::variant<...>, and <pybind11/stl.h>
// (included for the rest of this file's vector/pair bindings) registers an
// automatic type_caster<std::variant<Ts...>> that, being a template
// specialization, is selected at compile time BEFORE pybind11 ever consults
// the py::class_ runtime registry for that exact type — so a direct
// py::class_<physics::Joint> binding is silently unreachable (confirmed:
// attempting it produced "unable to convert function return value", with
// pybind11 trying to describe the return type as one of the variant's
// alternatives instead of treating it as an opaque class). Wrapping the
// pointer in this plain (non-variant) struct sidesteps the collision entirely.
struct JointHandle { physics::Joint* ptr = nullptr; };

void bind_world(py::module_& m) {
    // No methods exposed: scripts only ever hold the handle World::addVehicle
    // returns and pass it back into set_wheel_drive/set_wheel_steer, never
    // inspect it directly.
    py::class_<JointHandle>(m, "JointHandle");

    py::class_<World::WheelSpec>(m, "WheelSpec")
        .def(py::init<>())
        .def_readwrite("local_pos",   &World::WheelSpec::localPos)
        .def_readwrite("local_up",    &World::WheelSpec::localUp)
        .def_readwrite("rest_length", &World::WheelSpec::restLength)
        .def_readwrite("max_travel",  &World::WheelSpec::maxTravel)
        .def_readwrite("stiffness",   &World::WheelSpec::stiffness)
        .def_readwrite("damping",     &World::WheelSpec::damping)
        .def_readwrite("radius",      &World::WheelSpec::radius)
        .def_readwrite("mu_long",     &World::WheelSpec::muLong)
        .def_readwrite("mu_lat",      &World::WheelSpec::muLat)
        .def_readwrite("driven",      &World::WheelSpec::driven);

    // World — factory and query methods exposed to Python scripts
    py::class_<World>(m, "World")
        .def("add_sphere",      &World::addSphere,
             py::arg("mass"), py::arg("radius"), py::arg("pos") = math::Vec3{})
        .def("add_obb",         &World::addOBB,
             py::arg("extent"), py::arg("mass"), py::arg("pos") = math::Vec3{})
        .def("add_aabb",        &World::addAABB,
             py::arg("extent"), py::arg("mass"), py::arg("pos") = math::Vec3{})
        .def("add_static_aabb", &World::addStaticAABB,
             py::arg("pos"), py::arg("extent"))
        .def("add_trigger_box", &World::addTriggerBox,
             py::arg("pos"), py::arg("extent"))
        .def("add_trigger_sphere", &World::addTriggerSphere,
             py::arg("pos"), py::arg("radius"))
        .def("remove_entity",   &World::removeEntity)
        .def("reset_physics",   &World::resetPhysics)
        .def("get_hull_count",  &World::getHullCount)
        .def("get_island_count",&World::getIslandCount)
        .def_readwrite("gravity", &World::gravity)
        .def("add_spring",
             [](World& w, ecs::Entity a, ecs::Entity b, float k, float rest) {
                 w.addSpring(a, b, k, rest);
             }, py::arg("a"), py::arg("b"), py::arg("k"), py::arg("rest"))
        .def("add_spring_with_proxies",
             [](World& w, ecs::Entity a, ecs::Entity b, float k, float rest,
                float proxyCount, float proxyRadius) {
                 w.addSpringWithProxies(a, b, k, rest, static_cast<int>(proxyCount), proxyRadius);
             })
        .def("get_registry", [](World& w) -> ecs::Registry& { return w.getRegistry(); },
             py::return_value_policy::reference)
        // Mesh-attachment helpers — call after add_sphere/add_obb/add_aabb
        .def("attach_sphere_mesh",
             [](World& w, ecs::Entity e, float r,
                float cr = 1.f, float cg = 1.f, float cb = 1.f) {
                 RenderMesh* m = w.attachMesh(e, Primitives::icosphere(r));
                 if (m) { m->color[0]=cr; m->color[1]=cg; m->color[2]=cb; m->state=0; }
             }, py::arg("entity"), py::arg("radius"),
                py::arg("r")=1.f, py::arg("g")=1.f, py::arg("b")=1.f)
        .def("attach_box_mesh",
             [](World& w, ecs::Entity e, math::Vec3 half,
                float cr = 1.f, float cg = 1.f, float cb = 1.f) {
                 RenderMesh* m = w.attachMesh(e, Primitives::rect(half));
                 if (m) { m->color[0]=cr; m->color[1]=cg; m->color[2]=cb; m->state=0; }
             }, py::arg("entity"), py::arg("half"),
                py::arg("r")=1.f, py::arg("g")=1.f, py::arg("b")=1.f)
        // Make an entity static (pins it in place for spring cloth anchors, etc.)
        .def("fix_entity", [](World& w, ecs::Entity e) {
            // Mutates entity composition (adds Fixed tag) → take the structure lock
            // so this migration can't race the physics thread's registry iteration.
            auto lock = w.lockStructure();
            auto& reg = w.getRegistry();
            if (auto* h = reg.get<ecs::Hull>(e)) {
                h->mass = 0.f;
                h->inverseMass = 0.f;
                h->inverseInertia = math::Mat3::zero();
                h->velocity = {};
                h->omega = {};
                h->gravity = false;
            }
            if (!reg.has<ecs::Fixed>(e)) reg.add<ecs::Fixed>(e);
        })
        .def("set_mesh_color",
             [](World& w, ecs::Entity e, float r, float g, float b) {
                 if (RenderMesh* m = w.getMesh(e))
                     { m->color[0]=r; m->color[1]=g; m->color[2]=b; }
             })
        // Kinematic capsule — Transform + CapsuleForm only, no Hull.
        // Physics sim ignores this entity; CharacterController drives its position directly.
        .def("add_kinematic_capsule",
             [](World& w, float r, float hh, math::Vec3 pos) {
                 return w.addKinematicCapsule(r, hh, pos);
             }, py::arg("radius"), py::arg("half_height"),
                py::arg("pos") = math::Vec3{})
        .def("attach_capsule_mesh",
             [](World& w, ecs::Entity e, float r, float hh,
                float cr=1.f, float cg=1.f, float cb=1.f) {
                 RenderMesh* m = w.attachMesh(e, Primitives::capsule(r, hh));
                 if (m) { m->color[0]=cr; m->color[1]=cg; m->color[2]=cb; m->state=0; }
             }, py::arg("entity"), py::arg("radius"), py::arg("half_height"),
                py::arg("r")=1.f, py::arg("g")=1.f, py::arg("b")=1.f)
        .def("attach_cylinder_mesh",
             [](World& w, ecs::Entity e, float r, float hh,
                float cr=1.f, float cg=1.f, float cb=1.f) {
                 RenderMesh* m = w.attachMesh(e, Primitives::cylinder(r, hh));
                 if (m) { m->color[0]=cr; m->color[1]=cg; m->color[2]=cb; m->state=0; }
             }, py::arg("entity"), py::arg("radius"), py::arg("half_height"),
                py::arg("r")=1.f, py::arg("g")=1.f, py::arg("b")=1.f)
        // ---- GJK-only physics primitives (no mesh — call attach_*_mesh after) ----
        .def("add_capsule",  &World::addCapsule,
             py::arg("radius"), py::arg("half_height"), py::arg("mass"), py::arg("pos") = math::Vec3{})
        .def("add_cylinder", &World::addCylinder,
             py::arg("radius"), py::arg("half_height"), py::arg("mass"), py::arg("pos") = math::Vec3{})
        // ---- Model loading (.obj / .gltf / .glb) ----
        // Returns the list of created entities (one per glTF primitive).
        .def("add_model", &World::addModel, py::arg("path"))
        // Attach a clip-only glTF's animation(s) (e.g. a keyframed Empty exported
        // from Blender, no mesh needed) directly to an existing entity's own
        // Transform — for reusable clips shared across many objects (see
        // ecs::AnimationPlayer / World::attachAnimation). Returns the attached
        // clip's key, or "" on failure.
        .def("attach_animation", &World::attachAnimation, py::arg("entity"), py::arg("path"))
        // ---- Cubemap skybox: 6 asset-relative faces (+X,-X,+Y,-Y,+Z,-Z) ----
        .def("set_skybox", [](World& w, const std::vector<std::string>& faces) {
            if (faces.size() != 6)
                throw std::runtime_error("set_skybox expects 6 face paths (+X,-X,+Y,-Y,+Z,-Z)");
            std::array<std::string, 6> a;
            for (int i = 0; i < 6; ++i) a[i] = faces[i];
            w.setSkybox(a);
        }, py::arg("faces"))
        // ---- Collider attach / detach (World methods lock internally) ----
        .def("attach_sphere_collider",   &World::attachSphereCollider,
             py::arg("entity"), py::arg("mass"), py::arg("radius"), py::arg("static_") = false)
        .def("attach_aabb_collider",     &World::attachAABBCollider,
             py::arg("entity"), py::arg("mass"), py::arg("extent"), py::arg("static_") = false)
        .def("attach_obb_collider",      &World::attachOBBCollider,
             py::arg("entity"), py::arg("mass"), py::arg("extent"), py::arg("static_") = false)
        .def("attach_capsule_collider",  &World::attachCapsuleCollider,
             py::arg("entity"), py::arg("mass"), py::arg("radius"), py::arg("half_height"), py::arg("static_") = false)
        .def("attach_cylinder_collider", &World::attachCylinderCollider,
             py::arg("entity"), py::arg("mass"), py::arg("radius"), py::arg("half_height"), py::arg("static_") = false)
        .def("detach_physics_body",      &World::detachPhysicsBody, py::arg("entity"))
        // ---- Programmatic sphere compound (synthesized, not baked from a mesh) ----
        // Builds one Sphere sub-shape per (center, radius) pair (in the new entity's
        // local frame), derives mass/COM/inertia via the same math the mesh baker
        // uses (physics::computeCompoundMassProperties), creates a fresh entity at
        // `pos` (Transform + Hull + CompoundCollider — same factory-method
        // convention as add_sphere/add_obb/etc.) and returns it. Returns NullEntity
        // if `spheres` is empty.
        .def("build_sphere_compound",
             [](World& w, const std::vector<std::pair<math::Vec3, float>>& spheres,
                float density, bool isStatic, math::Vec3 pos) -> ecs::Entity {
                 constexpr float kPi = 3.14159265358979323846f;
                 std::vector<physics::SubShape> shapes;
                 shapes.reserve(spheres.size());
                 for (const auto& [center, radius] : spheres) {
                     physics::SubShape s;
                     s.type     = physics::SubShapeType::Sphere;
                     s.localPos = center;
                     s.localRot = math::Mat3{};
                     s.extent   = {radius, 0.0f, 0.0f};
                     s.aabbMin  = center - math::Vec3{radius, radius, radius};
                     s.aabbMax  = center + math::Vec3{radius, radius, radius};
                     s.mass     = density * (4.0f / 3.0f) * kPi * radius * radius * radius;
                     shapes.push_back(s);
                 }
                 if (shapes.empty()) return ecs::NullEntity;

                 float totalMass = 0.0f;
                 math::Mat3 invI = math::Mat3::zero();
                 math::Vec3 pivotOffset = physics::computeCompoundMassProperties(shapes, totalMass, invI);

                 static std::atomic<int> s_syntheticKeyCounter{0};
                 std::string key = "synthetic_sphere_compound_" + std::to_string(s_syntheticKeyCounter++);
                 physics::CompiledCollider* compiled = w.buildCompoundCollider(key, shapes);
                 compiled->totalMass           = totalMass;
                 compiled->inverseInertiaLocal = invI;
                 compiled->pivotOffset         = pivotOffset;

                 auto lock = w.lockStructure();
                 ecs::Entity e = w.getRegistry().create();
                 w.getRegistry().add<Transform>(e, Transform{pos, {0, 0, 0, 1}, {1, 1, 1}});
                 lock.unlock();

                 w.attachCompoundCollider(e, compiled, "", isStatic ? 0.0f : totalMass, isStatic);
                 return e;
             }, py::arg("spheres"), py::arg("density") = 1.0f, py::arg("is_static") = false,
                py::arg("pos") = math::Vec3{})
        .def("set_mesh_visible", &World::setMeshVisible, py::arg("entity"), py::arg("visible"))
        .def("remove_light", [](World& w, ecs::Entity e) { w.removeLight(e); }, py::arg("entity"))
        // ---- Audio source entity ----
        .def("add_audio_source_entity", &World::addAudioSourceEntity, py::arg("pos") = math::Vec3{})
        // ---- Point light convenience (constructs a PointLight + addLight) ----
        .def("add_point_light",
             [](World& w, math::Vec3 pos, math::Vec3 color, float intensity) {
                 PointLight pl{};
                 pl.position[0]=pos.x; pl.position[1]=pos.y; pl.position[2]=pos.z;
                 pl.color[0]=color.x; pl.color[1]=color.y; pl.color[2]=color.z;
                 pl.intensity=intensity;
                 pl.constant=1.0f; pl.linear=0.09f; pl.quadratic=0.032f;
                 return w.addLight(Light{pl});
             }, py::arg("pos"), py::arg("color") = math::Vec3{1.f,1.f,1.f}, py::arg("intensity") = 1.0f)
        // ---- HUD / world text (coords in [0,1] screen percentage, top-left origin) ----
        .def("add_ui_background", &World::addUIBackground,
             py::arg("min"), py::arg("max"), py::arg("color"), py::arg("depth") = 0)
        .def("add_ui_curved_background", &World::addUICurvedBackground,
             py::arg("min"), py::arg("max"), py::arg("color"), py::arg("curvature") = 0.5f, py::arg("depth") = 0)
        .def("add_ui_textured_background",
             [](World& w, math::Vec2 min, math::Vec2 max, math::Vec4 tint,
                const std::string& texPath, int depth) {
                 return w.addUITexturedBackground(min, max, tint, texPath.c_str(), depth);
             }, py::arg("min"), py::arg("max"), py::arg("tint"), py::arg("path"), py::arg("depth") = 0)
        .def("add_ui_text", &World::addUIText,
             py::arg("font"), py::arg("text"), py::arg("min"), py::arg("max"), py::arg("depth") = 0)
        .def("add_ui_button", &World::addUIButton,
             py::arg("min"), py::arg("max"), py::arg("normal_color"), py::arg("depth") = 0)
        .def("add_text_label_3d", &World::addTextLabel3D,
             py::arg("font"), py::arg("text"), py::arg("pos"))
        // ---- UI input (polled) ----
        // Complements the on_ui_press/release/enter/leave Script callbacks: use
        // these when a global/menu-driver script wants to check pointer state
        // without every UI element carrying its own ScriptComponent.
        .def("ui_hit_test", [](World& w, float x, float y) -> py::object {
                 ecs::Entity e = w.uiHitTest(x, y);
                 return (e == ecs::NullEntity) ? py::none() : py::cast(e);
             }, py::arg("x"), py::arg("y"))
        .def("ui_hovered", [](World& w) -> py::object {
                 ecs::Entity e = w.uiHovered();
                 return (e == ecs::NullEntity) ? py::none() : py::cast(e);
             })
        .def("ui_consumed_click", &World::uiConsumedClick)
        .def("set_ui_focus", &World::setUIFocus, py::arg("entity"))
        .def("get_ui_focus", [](World& w) -> py::object {
                 ecs::Entity e = w.uiFocused();
                 return (e == ecs::NullEntity) ? py::none() : py::cast(e);
             })
        // ---- UI hierarchy / panel grouping ----
        .def("set_ui_parent", &World::setUIParent, py::arg("child"), py::arg("parent"))
        .def("set_ui_group_visible", &World::setUIGroupVisible, py::arg("root"), py::arg("visible"))
        .def("set_ui_group_opacity", &World::setUIGroupOpacity, py::arg("root"), py::arg("opacity"))
        .def("tween_ui_opacity", &World::tweenUIOpacity,
             py::arg("root"), py::arg("target"), py::arg("duration"), py::arg("ease") = 0)
        // ---- Springs / misc ----
        .def("remove_spring_between", &World::removeSpringBetween, py::arg("a"), py::arg("b"))
        // ---- Joints (bilateral — see physics/Joint.h) ----
        // persist=True also writes the serializable ECS mirror component
        // (Point/Hinge/ConeTwistJointConstraint) from the joint's just-computed
        // body-local anchors/axes, so the joint survives Save Scene / reload
        // (SceneSerializer rebuilds the live joint from it). Default False keeps
        // transient joints (e.g. the mouse-drag grab) mirror-free — those are
        // created/destroyed every grab and must not leave a stale component.
        .def("add_point_joint",
             [](World& w, ecs::Entity a, ecs::Entity b, math::Vec3 anchor, bool persist) {
                 physics::Joint* j = w.addPointJoint(a, b, anchor);
                 if (persist && j && !w.getRegistry().has<ecs::PointJointConstraint>(a)) {
                     auto& pj = std::get<physics::PointToPointJoint>(*j);
                     w.getRegistry().add<ecs::PointJointConstraint>(a, {b, pj.localAnchorA, pj.localAnchorB});
                 }
             }, py::arg("a"), py::arg("b"), py::arg("anchor"), py::arg("persist") = false)
        .def("add_hinge_joint",
             [](World& w, ecs::Entity a, ecs::Entity b, math::Vec3 anchor, math::Vec3 axis,
                bool limit_enabled, float lower_angle, float upper_angle, bool persist) {
                 physics::Joint* j = w.addHingeJoint(a, b, anchor, axis, limit_enabled, lower_angle, upper_angle);
                 if (persist && j && !w.getRegistry().has<ecs::HingeJointConstraint>(a)) {
                     auto& hj = std::get<physics::HingeJoint>(*j);
                     w.getRegistry().add<ecs::HingeJointConstraint>(a,
                         {b, hj.localAnchorA, hj.localAnchorB, hj.localAxisA, hj.localAxisB,
                          hj.limitEnabled, hj.lowerAngle, hj.upperAngle});
                 }
             }, py::arg("a"), py::arg("b"), py::arg("anchor"), py::arg("axis"),
                py::arg("limit_enabled") = false, py::arg("lower_angle") = 0.0f, py::arg("upper_angle") = 0.0f,
                py::arg("persist") = false)
        .def("add_cone_twist_joint",
             [](World& w, ecs::Entity a, ecs::Entity b, math::Vec3 anchor, math::Vec3 twist_axis,
                float swing_limit, float twist_limit, bool persist) {
                 physics::Joint* j = w.addConeTwistJoint(a, b, anchor, twist_axis, swing_limit, twist_limit);
                 if (persist && j && !w.getRegistry().has<ecs::ConeTwistJointConstraint>(a)) {
                     auto& cj = std::get<physics::ConeTwistJoint>(*j);
                     w.getRegistry().add<ecs::ConeTwistJointConstraint>(a,
                         {b, cj.localAnchorA, cj.localAnchorB, cj.localTwistAxisA, cj.localTwistAxisB,
                          cj.swingLimit, cj.twistLimit});
                 }
             }, py::arg("a"), py::arg("b"), py::arg("anchor"), py::arg("twist_axis"),
                py::arg("swing_limit") = 0.785398f, py::arg("twist_limit") = 0.785398f,
                py::arg("persist") = false)
        .def("remove_joint_between", &World::removeJointBetween, py::arg("a"), py::arg("b"))
        // Give an entity a Name plus (in editor builds) EditorSelectable +
        // EditorPickable — the components the scene serializer's save loop and
        // editor picking need. The add_* factories already call this, so for
        // factory-spawned entities it just renames (idempotent); it's here for
        // renaming and for any entity that reached the registry without it.
        .def("finalize_entity", [](World& w, ecs::Entity e, const std::string& name) {
                 w.finalizeEntity(e, name.c_str());
             }, py::arg("entity"), py::arg("name"))
        // ---- Vehicles (raycast wheels) ---- see JointHandle's comment above
        // for why these wrap World::addVehicle's raw physics::Joint* instead
        // of binding it directly.
        .def("add_vehicle", [](World& w, ecs::Entity chassis, const std::vector<World::WheelSpec>& wheels) {
                 auto raw = w.addVehicle(chassis, wheels);
                 std::vector<JointHandle> handles;
                 handles.reserve(raw.size());
                 for (physics::Joint* p : raw) handles.push_back(JointHandle{p});
                 return handles;
             }, py::arg("chassis"), py::arg("wheels"))
        .def("set_wheel_drive", [](World& w, JointHandle h, float angularVel) {
                 w.setWheelDrive(h.ptr, angularVel);
             }, py::arg("wheel"), py::arg("angular_vel"))
        .def("set_wheel_steer", [](World& w, JointHandle h, float steerAngleRad) {
                 w.setWheelSteer(h.ptr, steerAngleRad);
             }, py::arg("wheel"), py::arg("steer_angle"))
        // ---- Scene shadow caster (single caster; radio behavior) ----
        // set_shadow_caster flags this light's LightSource.casts_shadow and clears
        // it on every other light. Pass a spot/directional light entity; point
        // lights aren't a supported caster type. clear_shadow_caster disables all.
        // get_shadow_caster returns the current caster entity, or None if unset.
        .def("set_shadow_caster",   &World::setShadowCaster, py::arg("entity"))
        .def("clear_shadow_caster", &World::clearShadowCaster)
        .def("get_shadow_caster",   [](World& w) -> py::object {
            ecs::Entity e = w.getShadowCaster();
            return (e == ecs::NullEntity) ? py::none() : py::cast(e);
        })
        // ---- World Settings: shadow tuning + exposure (see World.h for field docs) ----
        .def_readwrite("exposure",                 &World::exposure)
        .def_readwrite("shadow_bias",              &World::shadowBias)
        .def_readwrite("shadow_normal_bias",       &World::shadowNormalBias)
        .def_readwrite("shadow_pcf_radius",        &World::shadowPcfRadius)
        .def_readwrite("shadow_ortho_half_extent", &World::shadowOrthoHalfExtent)
        .def_readwrite("shadow_ortho_far",         &World::shadowOrthoFar)
        .def_readwrite("shadow_spot_near",         &World::shadowSpotNear)
        .def_readwrite("shadow_spot_far",          &World::shadowSpotFar)
        .def_readwrite("shadow_point_near",        &World::shadowPointNear)
        .def_readwrite("shadow_point_far",         &World::shadowPointFar)
        .def_readwrite("debug_physics", &World::debugPhysics)
        // ---- Solver instrumentation ----
        .def_readwrite("debug_contacts", &World::debugContacts)
        .def_property("warm_start", &World::getWarmStart, &World::setWarmStart)
        .def_property("time_scale", &World::getTimeScale, &World::setTimeScale)
        .def("get_pair_count",          &World::getPairCount)
        .def("get_contact_count",       &World::getContactCount)
        .def("get_contact_point_count", &World::getContactPointCount)
        .def("set_paused", &World::setPaused, py::arg("paused"))
        .def_property("paused",
            [](World& w) { return w.paused_.load(std::memory_order_acquire); },
            [](World& w, bool p) { w.setPaused(p); })
        // ---- Impulse / wake (own the sleeping-body correctness) ----
        .def("apply_impulse",    &World::applyImpulse,   py::arg("entity"), py::arg("impulse"))
        .def("apply_impulse_at", &World::applyImpulseAt, py::arg("entity"), py::arg("impulse"), py::arg("point"))
        .def("wake",             &World::wake,           py::arg("entity"))
        .def_property_readonly("tick_count", &World::getTickCount)
        .def_property_readonly("layers",
            [](World& w) -> physics::CollisionLayers& { return w.layers; },
            py::return_value_policy::reference);

    // SceneManager — scene loading / transitions
    py::class_<SceneManager>(m, "SceneManager")
        .def("load_scene", &SceneManager::queueLoad);

    // Camera
    py::class_<Camera>(m, "Camera")
        .def("set_position", &Camera::setPosition)
        .def("set_rotation", &Camera::setRotation)
        .def("set_fov",      &Camera::setFOV)
        .def("get_forward",  &Camera::getForward)
        .def("look_at",      &Camera::lookAt, py::arg("target"))
        .def("screen_to_ray", [](const Camera& c, float px, float py) {
            math::Vec3 o, d;
            c.screenToRay(px, py, o, d);
            return py::make_tuple(o, d);
        }, py::arg("px"), py::arg("py"))
        .def_property("position", &Camera::getPosition, &Camera::setPosition)
        .def_property("rotation", &Camera::getRotation, &Camera::setRotation);

    // Window — pixel dimensions + cursor position (for screen_to_ray / picking).
    py::class_<Window>(m, "Window")
        .def("get_width",  [](Window& w) { return w.getWidth();  })
        .def("get_height", [](Window& w) { return w.getHeight(); })
        .def("get_cursor_pos", [](Window& w) {
            double x, y;
            glfwGetCursorPos(w.getHandle(), &x, &y);
            return std::make_pair(x, y);
        })
        // Lock = hidden + captured (FPS mouselook, the default). Unlock to show a
        // visible cursor for menus / screen_to_ray picking. Routed through
        // pause()/unpause() (paused == cursor visible) rather than poking GLFW's
        // cursor mode directly — those also keep Window::paused in sync, which
        // Window::cursorPosCallback uses to gate mouse-delta accumulation and to
        // avoid a delta spike on the unlock->lock transition (firstMouse reset).
        // A direct glfwSetInputMode call here would desync that bookkeeping:
        // paused stays false forever, so a later unpause()/pause() (e.g. the
        // engine's built-in TAB toggle) silently overrides whatever a script set.
        .def("set_cursor_locked", [](Window& w, bool locked) {
            if (locked) w.unpause(); else w.pause();
        }, py::arg("locked"))
        .def("is_cursor_locked", [](Window& w) {
            return glfwGetInputMode(w.getHandle(), GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
        });

    // Input
    py::class_<Input>(m, "Input")
        .def("is_key_down",     &Input::isKeyDown)
        .def("is_key_pressed",  &Input::isKeyPressed)
        .def("is_key_released", &Input::isKeyReleased)
        .def("is_lmb_down",     &Input::isLMBDown)
        .def("is_rmb_down",     &Input::isRMBDown)
        .def("is_mmb_down",     &Input::isMMBDown)
        .def("is_forward_mb_down",  &Input::isForwardMBDown)
        .def("is_backward_mb_down", &Input::isBackwardMBDown)
        .def("is_mouse_pressed",  &Input::isMousePressed,  py::arg("button"))
        .def("is_mouse_released", &Input::isMouseReleased, py::arg("button"))
        .def("get_scroll_x", &Input::getScrollX)
        .def("get_scroll_y", &Input::getScrollY)
        .def("get_mouse_delta", [](const Input& inp) {
            auto d = inp.getMouseDelta();
            return std::make_pair(d.x, d.y);
        })
        .def("get_cursor_pos", [](const Input& inp) {
            return std::make_pair(inp.getCursorX(), inp.getCursorY());
        })
        // Codepoints typed this frame (shift/layout/IME applied — unlike raw key
        // events). Manual alternative to the on_text_input Script callback.
        .def("get_typed_chars", &Input::getTypedChars);

    // AudioSystem
    py::class_<AudioSystem>(m, "AudioSystem")
        .def("pause_all",  &AudioSystem::pauseAll)
        .def("resume_all", &AudioSystem::resumeAll)
        .def("stop_all",   &AudioSystem::stopAll)
        .def("load_sound", &AudioSystem::loadSound, py::arg("path"),
             py::return_value_policy::reference)
        .def("create_source", &AudioSystem::createSource, py::arg("buffer"),
             py::return_value_policy::reference)
        .def("set_bus_gain", [](AudioSystem& a, int bus, float gain) {
                a.setBusGain(static_cast<Source::Bus>(bus), gain);
             }, py::arg("bus"), py::arg("gain"))
        .def("set_master_gain", &AudioSystem::setMasterGain, py::arg("gain"))
        .def("fade_gain", [](AudioSystem& a, Source* src, float target, float duration, int ease) {
                a.fadeGain(src, target, duration, static_cast<ui::Ease>(ease));
             }, py::arg("source"), py::arg("target"), py::arg("duration"),
                py::arg("ease") = static_cast<int>(ui::Ease::Linear));

    // AudioSystem::SoundBuffer — opaque handle returned by load_sound
    py::class_<AudioSystem::SoundBuffer>(m, "SoundBuffer");

    // Source — one OpenAL voice (owned by AudioSystem; this is a non-owning view)
    py::class_<Source>(m, "Source")
        .def("play",   &Source::play)
        .def("pause",  &Source::pause)
        .def("stop",   &Source::stop)
        .def("rewind", &Source::rewind)
        .def("set_gain",     &Source::setGain,     py::arg("gain"))
        .def("set_pitch",    &Source::setPitch,    py::arg("pitch"))
        .def("set_position", &Source::setPosition, py::arg("pos"))
        .def("set_velocity", &Source::setVelocity, py::arg("vel"))
        .def("set_reference_distance", &Source::setReferenceDistance, py::arg("dist"))
        .def("enable_looping", &Source::enableLooping, py::arg("loop"))
        .def("is_playing", &Source::isPlaying)
        .def("set_relative", &Source::setRelative, py::arg("relative"))
        .def("set_bus", [](Source& s, int bus) { s.setBus(static_cast<Source::Bus>(bus)); },
             py::arg("bus"));

    // CollisionLayers — named 32-bit layer registry (yope3d.world.layers)
    py::class_<physics::CollisionLayers>(m, "CollisionLayers")
        .def("add",   &physics::CollisionLayers::add, py::arg("name"))
        .def("has",   &physics::CollisionLayers::has, py::arg("name"))
        .def("count", &physics::CollisionLayers::count)
        .def("__getitem__", [](const physics::CollisionLayers& l, const std::string& n) { return l[n]; })
        .def_property_readonly_static("ALL",  [](py::object) { return physics::CollisionLayers::ALL;  })
        .def_property_readonly_static("NONE", [](py::object) { return physics::CollisionLayers::NONE; });

    // Registry (also accessed through view/reg_get at module level)
    py::class_<ecs::Registry>(m, "Registry");

    // Common GLFW key constants for scripts
    m.attr("KEY_W")      = GLFW_KEY_W;
    m.attr("KEY_A")      = GLFW_KEY_A;
    m.attr("KEY_S")      = GLFW_KEY_S;
    m.attr("KEY_D")      = GLFW_KEY_D;
    m.attr("KEY_Q")      = GLFW_KEY_Q;
    m.attr("KEY_E")      = GLFW_KEY_E;
    m.attr("KEY_SPACE")  = GLFW_KEY_SPACE;
    m.attr("KEY_LEFT")   = GLFW_KEY_LEFT;
    m.attr("KEY_RIGHT")  = GLFW_KEY_RIGHT;
    m.attr("KEY_UP")     = GLFW_KEY_UP;
    m.attr("KEY_DOWN")   = GLFW_KEY_DOWN;
    m.attr("KEY_R")      = GLFW_KEY_R;
    m.attr("KEY_F")      = GLFW_KEY_F;
    m.attr("KEY_H")      = GLFW_KEY_H;
    m.attr("KEY_P")      = GLFW_KEY_P;
    m.attr("KEY_ESCAPE") = GLFW_KEY_ESCAPE;
    m.attr("KEY_ENTER")  = GLFW_KEY_ENTER;
    m.attr("KEY_LEFT_SHIFT")   = GLFW_KEY_LEFT_SHIFT;
    m.attr("KEY_LEFT_CONTROL") = GLFW_KEY_LEFT_CONTROL;
    m.attr("KEY_V")            = GLFW_KEY_V;
    m.attr("KEY_BACKSPACE")    = GLFW_KEY_BACKSPACE;

    // Mouse button constants (for is_mouse_pressed / is_mouse_released)
    m.attr("MOUSE_LEFT")   = GLFW_MOUSE_BUTTON_LEFT;
    m.attr("MOUSE_RIGHT")  = GLFW_MOUSE_BUTTON_RIGHT;
    m.attr("MOUSE_MIDDLE") = GLFW_MOUSE_BUTTON_MIDDLE;

    // Easing curves for World.tween_ui_opacity's `ease` argument.
    m.attr("EASE_LINEAR")       = static_cast<int>(ui::Ease::Linear);
    m.attr("EASE_QUAD_IN")      = static_cast<int>(ui::Ease::QuadIn);
    m.attr("EASE_QUAD_OUT")     = static_cast<int>(ui::Ease::QuadOut);
    m.attr("EASE_QUAD_IN_OUT")  = static_cast<int>(ui::Ease::QuadInOut);
    m.attr("EASE_CUBIC_IN")     = static_cast<int>(ui::Ease::CubicIn);
    m.attr("EASE_CUBIC_OUT")    = static_cast<int>(ui::Ease::CubicOut);
    m.attr("EASE_CUBIC_IN_OUT") = static_cast<int>(ui::Ease::CubicInOut);

    // Mixer bus tags for AudioSource.bus / audio.set_bus_gain.
    m.attr("BUS_MUSIC") = static_cast<int>(Source::Bus::Music);
    m.attr("BUS_SFX")   = static_cast<int>(Source::Bus::SFX);
    m.attr("BUS_VOICE") = static_cast<int>(Source::Bus::Voice);

    // One-shot audio convenience: load + create + play in a single call.
    // Returns the Source (reuse it to stop/reposition) or None if audio isn't bound.
    m.def("play_sound",
        [](const std::string& path, py::object pos_obj, float gain, bool loop) -> Source* {
            auto audioObj = py::module_::import("yope3d").attr("audio");
            if (audioObj.is_none()) return nullptr;
            auto* audio = audioObj.cast<AudioSystem*>();
            auto* buf = audio->loadSound(path);
            if (!buf) return nullptr;
            Source* s = audio->playTransient(buf);   // reuses a finished one-shot voice
            if (!s) return nullptr;
            s->setGain(gain);
            s->enableLooping(loop);
            if (!pos_obj.is_none()) s->setPosition(pos_obj.cast<math::Vec3>());
            s->play();
            return s;
        }, py::arg("path"), py::arg("pos") = py::none(),
           py::arg("gain") = 1.0f, py::arg("loop") = false,
           py::return_value_policy::reference);

    // Non-spatial ("2D") stereo music playback — a soundtrack path distinct
    // from play_sound's mono/pooled/positional one-shots.
    // stream=True: incremental decode (MusicStream), for long tracks.
    // stream=False: full-decode stereo buffer, for short stingers/jingles.
    m.def("play_music",
        [](const std::string& path, bool loop, float fade_in, bool stream) -> Source* {
            auto audioObj = py::module_::import("yope3d").attr("audio");
            if (audioObj.is_none()) return nullptr;
            auto* audio = audioObj.cast<AudioSystem*>();
            return audio->playMusic(path, loop, fade_in, stream);
        }, py::arg("path"), py::arg("loop") = false,
           py::arg("fade_in") = 0.0f, py::arg("stream") = true,
           py::return_value_policy::reference);

    // Capsule overlap test against all tangible world geometry.
    // Returns list of (normal: Vec3, depth: float) tuples — one per overlapping entity.
    // exclude: pass the player entity (or None) to skip self-collision.
    m.def("capsule_overlap",
        [](math::Vec3 pos, float r, float hh, py::object exclude_obj) -> py::list {
            ecs::Entity exclude = ecs::NullEntity;
            if (!exclude_obj.is_none())
                exclude = exclude_obj.cast<ecs::Entity>();
            auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
            auto lock = world->lockStructure();   // physics may be mid-advance()
            auto results = physics::KinematicQuery::capsuleOverlap(
                pos, r, hh, world->getRegistry(), exclude);
            py::list out;
            for (auto& res : results)
                out.append(py::make_tuple(res.normal, res.depth));
            return out;
        }, py::arg("pos"), py::arg("radius"), py::arg("half_height"),
           py::arg("exclude") = py::none());

    // Capsule cast from endpoint sphere center along `dir` (normalized).
    // Returns (t: float, hit: bool, normal: Vec3) — t is the contact distance from the endpoint.
    // exclude: pass the player entity (or None) to skip self-collision.
    m.def("capsule_cast",
        [](math::Vec3 pos, float r, float hh,
           math::Vec3 dir, float maxDist, py::object exclude_obj) {
            ecs::Entity exclude = ecs::NullEntity;
            if (!exclude_obj.is_none())
                exclude = exclude_obj.cast<ecs::Entity>();
            auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
            auto lock = world->lockStructure();   // physics may be mid-advance()
            auto res = physics::KinematicQuery::capsuleCast(
                pos, r, hh, dir, maxDist, world->getRegistry(), exclude);
            return py::make_tuple(res.t, res.hit, res.normal);
        }, py::arg("pos"), py::arg("radius"), py::arg("half_height"),
           py::arg("dir"), py::arg("max_dist"),
           py::arg("exclude") = py::none());

    // Thin-ray query. Returns (hit: bool, entity: Entity|None, point: Vec3, normal: Vec3, t: float).
    // `dir` need not be normalized; `t` is in world meters. Pass an entity (or None) to exclude.
    m.def("raycast",
        [](math::Vec3 origin, math::Vec3 dir, float maxDist, py::object exclude_obj) {
            ecs::Entity exclude = ecs::NullEntity;
            if (!exclude_obj.is_none())
                exclude = exclude_obj.cast<ecs::Entity>();
            auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
            auto lock = world->lockStructure();   // physics may be mid-advance()
            auto hit = physics::KinematicQuery::raycast(
                origin, dir, maxDist, world->getRegistry(), exclude);
            py::object ent = hit.hit ? py::cast(hit.entity) : py::none();
            return py::make_tuple(hit.hit, ent, hit.point, hit.normal, hit.t);
        }, py::arg("origin"), py::arg("dir"), py::arg("max_dist"),
           py::arg("exclude") = py::none());

    // Wall-clock seconds since engine startup (GLFW timer).
    m.def("time", []() { return glfwGetTime(); });

    // Stamp the profiler CSV's `scene` column (opt-in via YOPE_PROF_ENABLED;
    // no-op otherwise). Profiler::setScene stores the pointer directly
    // (string-literal contract), so Python strings are interned here to give
    // them static lifetime.
    m.def("set_profile_scene", [](const std::string& name) {
#ifdef YOPE_PROF_ENABLED
        static std::unordered_set<std::string> interned;
        Profiler::setScene(interned.insert(name).first->c_str());
#else
        (void)name;
#endif
    }, py::arg("name"));

    // Per-frame debug overlay: accumulate a world-space segment / ray (drawn always-on-top).
    // Cleared automatically each frame before scripts run, so call from update().
    m.def("draw_line",
        [](math::Vec3 a, math::Vec3 b, py::object color_obj) {
            math::Vec3 c{1.f, 1.f, 0.f};
            if (!color_obj.is_none()) c = color_obj.cast<math::Vec3>();
            auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
            world->addDebugLine(a, b, c);
        }, py::arg("a"), py::arg("b"), py::arg("color") = py::none());
    m.def("draw_ray",
        [](math::Vec3 origin, math::Vec3 dir, float length, py::object color_obj) {
            math::Vec3 c{1.f, 1.f, 0.f};
            if (!color_obj.is_none()) c = color_obj.cast<math::Vec3>();
            auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
            world->addDebugLine(origin, origin + dir.normalize() * length, c);
        }, py::arg("origin"), py::arg("dir"), py::arg("length") = 1.0f,
           py::arg("color") = py::none());

    // Module-level singletons — set to None until bindContext() is called
    m.attr("world")         = py::none();
    m.attr("camera")        = py::none();
    m.attr("input")         = py::none();
    m.attr("audio")         = py::none();
    m.attr("scene_manager") = py::none();
    m.attr("window")        = py::none();

    // Convenience: yope3d.load_scene(path) → scene_manager.load_scene(path)
    m.def("load_scene", [](const std::string& path) {
        auto sm = py::module_::import("yope3d").attr("scene_manager");
        if (sm.is_none()) throw std::runtime_error("scene_manager not bound");
        sm.cast<SceneManager*>()->queueLoad(path);
    });
    // Resolve a writable path for `name` inside the sanctioned per-platform
    // save directory, creating any needed subdirectories. Throws if no
    // writable directory could be resolved (e.g. HOME/APPDATA unset).
    m.def("save_path", [](const std::string& name) -> std::string {
        std::string base = writableDataDir();
        if (base.empty())
            throw std::runtime_error("save_path: could not resolve a writable data directory");
        std::filesystem::path full = std::filesystem::path(base) / name;
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        return full.string();
    }, py::arg("name"));
}
#endif // YOPE_PYTHON
