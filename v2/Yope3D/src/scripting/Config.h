#pragma once
#include <string>

struct Config {
    // Required: path to the scene that yope3d loads on launch. The editor
    // opens this scene by default too; users can change it with File > Open.
    std::string startupScene = "";
    int         width        = 0;   // 0 = use primary monitor size
    int         height       = 0;

    // Engine-level hotkey opt-outs — see Window::setEscapeCloses/setTabPauses/
    // setF11Fullscreen. Games that want ESC/TAB/F11 as gameplay-bound actions
    // (pause menu, scoreboard, custom fullscreen UI) set the matching key to
    // false here; the key still reaches Input either way, so a script-side
    // action map can bind it once the built-in behavior is disabled.
    bool        escapeCloses   = true;
    bool        tabPauses      = true;
    bool        f11Fullscreen  = true;

    // Parses a key=value file at path. Missing keys keep their defaults.
    static Config load(const std::string& path);
};
