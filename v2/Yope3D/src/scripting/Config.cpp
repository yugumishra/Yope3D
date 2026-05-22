#include "Config.h"
#include <fstream>
#include <sstream>

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
        if (key == "script") cfg.script = val;
        else if (key == "width")  cfg.width  = std::stoi(val);
        else if (key == "height") cfg.height = std::stoi(val);
    }
    return cfg;
}
