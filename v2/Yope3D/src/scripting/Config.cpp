#include "Config.h"
#include <fstream>
#include <sstream>

namespace {
bool parseBool(const std::string& val) {
    return val == "1" || val == "true" || val == "True" || val == "TRUE";
}
}

Config Config::load(const std::string& path) {
    Config cfg;
    std::ifstream file(path);
    if (!file.is_open()) return cfg;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "startupScene") cfg.startupScene = val;
        else if (key == "width")   cfg.width  = std::stoi(val);
        else if (key == "height")  cfg.height = std::stoi(val);
        else if (key == "escapeCloses")  cfg.escapeCloses  = parseBool(val);
        else if (key == "tabPauses")     cfg.tabPauses     = parseBool(val);
        else if (key == "f11Fullscreen") cfg.f11Fullscreen = parseBool(val);
    }
    return cfg;
}
