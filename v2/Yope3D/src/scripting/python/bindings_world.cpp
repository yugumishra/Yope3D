#ifdef YOPE_PYTHON
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "world/World.h"
#include "rendering/Camera.h"
#include "platform/Input.h"
#include "audio/AudioSystem.h"
#include "scene/SceneManager.h"
#include "rendering/Light.h"
#include "assets/Primitives.h"
#include "world/RenderMesh.h"
#include "ecs/Components.h"
#include "math/Mat3.h"
#include <GLFW/glfw3.h>

namespace py = pybind11;

void bind_world(py::module_& m) {
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
        .def("remove_entity",   &World::removeEntity)
        .def("reset_physics",   &World::resetPhysics)
        .def("get_hull_count",  &World::getHullCount)
        .def("get_island_count",&World::getIslandCount)
        .def_readwrite("gravity", &World::gravity)
        .def("add_spring", &World::addSpring,
             py::arg("a"), py::arg("b"), py::arg("k"), py::arg("rest"),
             py::return_value_policy::reference)
        .def("add_spring_with_proxies",
             [](World& w, ecs::Entity a, ecs::Entity b, float k, float rest,
                int proxyCount, float proxyRadius) {
                 return w.addSpringWithProxies(a, b, k, rest, proxyCount, proxyRadius);
             }, py::return_value_policy::reference)
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
             });

    // SceneManager — scene loading / transitions
    py::class_<SceneManager>(m, "SceneManager")
        .def("load_scene", &SceneManager::queueLoad);

    // Camera
    py::class_<Camera>(m, "Camera")
        .def("set_position", &Camera::setPosition)
        .def("set_rotation", &Camera::setRotation)
        .def("set_fov",      &Camera::setFOV)
        .def("get_forward",  &Camera::getForward)
        .def_property("position", &Camera::getPosition, &Camera::setPosition)
        .def_property("rotation", &Camera::getRotation, &Camera::setRotation);

    // Input
    py::class_<Input>(m, "Input")
        .def("is_key_down",     &Input::isKeyDown)
        .def("is_key_pressed",  &Input::isKeyPressed)
        .def("is_key_released", &Input::isKeyReleased)
        .def("is_lmb_down",     &Input::isLMBDown)
        .def("is_rmb_down",     &Input::isRMBDown)
        .def("get_mouse_delta", [](const Input& inp) {
            auto d = inp.getMouseDelta();
            return std::make_pair(d.x, d.y);
        });

    // AudioSystem
    py::class_<AudioSystem>(m, "AudioSystem")
        .def("pause_all",  &AudioSystem::pauseAll)
        .def("resume_all", &AudioSystem::resumeAll)
        .def("stop_all",   &AudioSystem::stopAll);

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

    // Module-level singletons — set to None until bindContext() is called
    m.attr("world")         = py::none();
    m.attr("camera")        = py::none();
    m.attr("input")         = py::none();
    m.attr("audio")         = py::none();
    m.attr("scene_manager") = py::none();

    // Convenience: yope.load_scene(path) → scene_manager.load_scene(path)
    m.def("load_scene", [](const std::string& path) {
        auto sm = py::module_::import("yope").attr("scene_manager");
        if (sm.is_none()) throw std::runtime_error("scene_manager not bound");
        sm.cast<SceneManager*>()->queueLoad(path);
    });
}
#endif // YOPE_PYTHON
