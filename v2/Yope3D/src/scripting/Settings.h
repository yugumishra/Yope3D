#pragma once
#include <map>
#include <string>
#include <vector>

struct Config;

// Settings — persisted user preferences (volume, sensitivity, resolution, key
// bindings, ...). Same key=value format Config parses, but the file lives in
// the per-platform writable data dir (BundlePaths' writableDataDir()) rather
// than next to yope3d.cfg: a shipped .app bundle cannot write beside its own
// binary.
//
// Precedence at startup is yope3d.cfg (developer / ship default) < settings.cfg
// (what the player chose) — see applyTo(). Keys the engine doesn't know are
// left untouched for scripts to own; the store is a generic string map, so
// gameplay code can persist whatever it likes without an engine change.
//
// Writes are explicit (save()), not per-set — a volume slider dragged across a
// frame would otherwise hit the disk every tick.
class Settings {
public:
    // Resolve the path and read the file. A missing file is not an error (it's
    // first launch) — the store is simply empty. Returns false only if no
    // writable data dir could be resolved at all.
    bool load();

    // Write every key back, sorted. Returns false if the data dir is
    // unresolvable or the file could not be opened.
    bool save();

    bool has   (const std::string& key) const;
    void remove(const std::string& key);
    void clear();
    std::vector<std::string> keys() const;

    std::string getString(const std::string& key, const std::string& def = "") const;
    float       getFloat (const std::string& key, float def = 0.0f) const;
    int         getInt   (const std::string& key, int   def = 0)    const;
    bool        getBool  (const std::string& key, bool  def = false) const;

    void setString(const std::string& key, const std::string& v);
    void setFloat (const std::string& key, float v);
    void setInt   (const std::string& key, int v);
    void setBool  (const std::string& key, bool v);

    // Overlay the engine-known keys (width/height/escapeCloses/tabPauses/
    // f11Fullscreen) onto cfg. startupScene is deliberately NOT overlayable:
    // it's a developer/ship key, and letting a settings file redirect it turns
    // a corrupt preference into an unlaunchable game.
    void applyTo(Config& cfg) const;

    // Absolute path of the backing file — empty until load() resolves it.
    const std::string& path() const { return path_; }

private:
    std::map<std::string, std::string> values_;
    std::string                        path_;
};
