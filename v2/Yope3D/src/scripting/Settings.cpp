#include "Settings.h"
#include "Config.h"
#include "platform/BundlePaths.h"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

bool parseBool(const std::string& val) {
    return val == "1" || val == "true" || val == "True" || val == "TRUE";
}

// Trim ASCII spaces/tabs/CR from both ends. yope3d.cfg is hand-authored and
// settings.cfg may be too (players edit these), so tolerate stray whitespace
// and the CRLF a Windows editor leaves behind.
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

} // namespace

bool Settings::load() {
    values_.clear();

    std::string base = writableDataDir();
    if (base.empty()) { path_.clear(); return false; }
    path_ = (std::filesystem::path(base) / "settings.cfg").string();

    std::ifstream file(path_);
    if (!file.is_open()) return true;   // first launch — empty store, not a failure

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        if (key.empty()) continue;
        values_[key] = trim(line.substr(eq + 1));
    }
    return true;
}

bool Settings::save() {
    if (path_.empty()) {
        std::string base = writableDataDir();
        if (base.empty()) return false;
        path_ = (std::filesystem::path(base) / "settings.cfg").string();
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);

    std::ofstream f(path_, std::ios::trunc);
    if (!f.is_open()) return false;

    f << "# Yope3D user settings - written by Settings::save().\n"
      << "# Overrides the matching keys in yope3d.cfg; delete to restore defaults.\n";
    for (const auto& [k, v] : values_) f << k << '=' << v << '\n';
    return f.good();
}

bool Settings::has(const std::string& key) const { return values_.count(key) != 0; }
void Settings::remove(const std::string& key)    { values_.erase(key); }
void Settings::clear()                            { values_.clear(); }

std::vector<std::string> Settings::keys() const {
    std::vector<std::string> out;
    out.reserve(values_.size());
    for (const auto& [k, _] : values_) out.push_back(k);
    return out;
}

std::string Settings::getString(const std::string& key, const std::string& def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : it->second;
}

// The get*() numeric paths swallow parse errors and fall back to `def` rather
// than throwing: a hand-edited or truncated settings file must never take the
// game down on launch. A bad value silently reverts to the default.
float Settings::getFloat(const std::string& key, float def) const {
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    try { return std::stof(it->second); } catch (...) { return def; }
}

int Settings::getInt(const std::string& key, int def) const {
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}

bool Settings::getBool(const std::string& key, bool def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : parseBool(it->second);
}

void Settings::setString(const std::string& key, const std::string& v) { values_[key] = v; }
void Settings::setInt   (const std::string& key, int v)  { values_[key] = std::to_string(v); }
void Settings::setBool  (const std::string& key, bool v) { values_[key] = v ? "true" : "false"; }

void Settings::setFloat(const std::string& key, float v) {
    // to_chars (shortest round-trip) rather than to_string/ostream: both of
    // those stop at 6 significant digits, which silently mangles anything
    // finer -- 3.14159274f would save as "3.14159" and reload as 3.14159012f,
    // and 1234567.0f as "1.23457e+06" -> 1234570. to_chars emits the shortest
    // string that reads back bit-identical, so it stays human-editable
    // ("0.002", not "0.00200000009") without being lossy.
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    values_[key] = (res.ec == std::errc{}) ? std::string(buf, res.ptr)
                                           : std::to_string(v);
}

void Settings::applyTo(Config& cfg) const {
    if (has("width"))         cfg.width         = getInt ("width",  cfg.width);
    if (has("height"))        cfg.height        = getInt ("height", cfg.height);
    if (has("escapeCloses"))  cfg.escapeCloses  = getBool("escapeCloses",  cfg.escapeCloses);
    if (has("tabPauses"))     cfg.tabPauses     = getBool("tabPauses",     cfg.tabPauses);
    if (has("f11Fullscreen")) cfg.f11Fullscreen = getBool("f11Fullscreen", cfg.f11Fullscreen);
}
