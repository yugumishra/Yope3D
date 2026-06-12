#pragma once
#include <string>

// Returns the absolute path to Contents/Resources when running inside a macOS
// .app bundle. Returns an empty string on all other platforms or when the
// process is not inside a bundle (e.g. running from a build directory).
std::string bundleResourcesDir();
