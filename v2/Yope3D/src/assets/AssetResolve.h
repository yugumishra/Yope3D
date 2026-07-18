#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Single entry point every runtime asset loader should go through to fetch
// raw bytes for a path relative to the assets/ root (e.g. "textures/tnail.png").
//
// Resolution order:
//   1. Embedded lookup (getEmbeddedAsset), if built with YOPE_EMBED_ASSETS.
//      Under YOPE_EMBED_SCOPE=FULL this always hits; under PARTIAL it only
//      hits for manifest-listed assets.
//   2. Filesystem: <bundle Resources>/assets/<path> (macOS .app bundle,
//      needed for PARTIAL scope's loose-file assets), falling back to
//      YOPE_ASSETS_DIR/<path> (dev builds / non-bundle runs).
//
// Returns an empty vector if the asset can't be found anywhere. Callers
// decide how to report that (throw, log, fall back to a placeholder) —
// this function never throws.
namespace assets {

std::vector<uint8_t> readBytes(const std::string& relPath);

// Convenience: same resolution, but returns the bytes as a std::string
// (e.g. for JSON parsing with parseJson(const char*)).
std::string readText(const std::string& relPath);

// For loaders that need an actual filesystem path rather than bytes (e.g.
// ObjLoader, which also reads a companion .mtl file relative to it, or
// third-party libraries that only take a path). Never consults the embedded
// lookup — these assets are inherently loose files. Resolves bundle
// Resources/assets first, then YOPE_ASSETS_DIR. Returns the YOPE_ASSETS_DIR
// path even if the file doesn't exist there (callers already handle missing
// files via their own load-failure path).
std::string resolveFilesystemPath(const std::string& relPath);

// Scene paths carry a mixed convention historically (absolute paths from
// Engine's startup-scene resolution, "assets/..."-prefixed paths from Python
// yope3d.load_scene(), or already assets/-root-relative paths). Normalizes
// any of those to a plain assets/-root-relative key suitable for readBytes()/
// readText() and embedded-asset lookup. Paths outside YOPE_ASSETS_DIR (e.g. an
// editor Save-As elsewhere) are returned unchanged — they simply won't match
// an embedded key, and readBytes()'s filesystem fallback still resolves them
// correctly (std::filesystem::path's / operator discards the LHS for an
// absolute RHS).
std::string normalizeToAssetsRelative(const std::string& path);

} // namespace assets
