#include "editor/EditorApp.h"

int main() {
    EditorApp app;
    if (!app.init()) return -1;
    app.run();
    app.cleanup();
    return 0;
}
