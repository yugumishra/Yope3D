#pragma once
#include <string>

struct Config {
    std::string script = "Sandbox";
    int         width  = 0;   // 0 = use primary monitor size
    int         height = 0;

    // Parses a key=value file at path. Missing keys keep their defaults.
    static Config load(const std::string& path);
};
