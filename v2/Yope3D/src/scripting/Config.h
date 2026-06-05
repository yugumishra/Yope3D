#pragma once
#include <string>

struct Config {
    // Required: path to the scene that yope3d loads on launch. The editor
    // opens this scene by default too; users can change it with File > Open.
    std::string startupScene = "";
    int         width        = 0;   // 0 = use primary monitor size
    int         height       = 0;

    // Parses a key=value file at path. Missing keys keep their defaults.
    static Config load(const std::string& path);
};
