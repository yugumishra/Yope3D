#include "editor/EditorApp.h"
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    // --scene <path>: startup scene override, same semantics as the runtime's
    // (relative paths resolve against the assets dir; replaces yope3d.cfg's
    // startupScene= key). The editor loads it into edit mode (scripts deferred
    // to Play), unlike the runtime which inits scripts immediately.
    std::string sceneOverride;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
            sceneOverride = argv[++i];
        } else {
            std::fprintf(stderr, "usage: %s [--scene <path>]\n", argv[0]);
            return -1;
        }
    }

    EditorApp app;
    if (!app.init(sceneOverride)) return -1;
    app.run();
    app.cleanup();
    return 0;
}
