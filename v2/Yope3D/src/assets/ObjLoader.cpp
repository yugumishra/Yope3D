#include "ObjLoader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <stdexcept>
#include "algorithm"

namespace {
    // Split a string by whitespace.
    std::vector<std::string> split(const std::string& s) {
        std::istringstream iss(s);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    // Parse "12/7/3" format: posIdx/uvIdx/normIdx (1-based OBJ indices).
    // Returns {posIdx, uvIdx, normIdx} as 0-based indices, or {-1, -1, -1} if invalid.
    struct FaceVertex {
        int posIdx  = -1;
        int uvIdx   = -1;
        int normIdx = -1;
    };

    FaceVertex parseFaceVertex(const std::string& faceToken) {
        FaceVertex fv;
        std::string token = faceToken;
        std::replace(token.begin(), token.end(), '/', ' ');
        std::istringstream iss(token);
        int pos, uv = 0, norm = 0;
        if (!(iss >> pos)) return fv;  // position is required
        iss >> uv;  // optional
        iss >> norm;  // optional
        fv.posIdx  = pos - 1;   // convert to 0-based
        fv.uvIdx   = (uv > 0) ? (uv - 1) : -1;
        fv.normIdx = (norm > 0) ? (norm - 1) : -1;
        return fv;
    }

    // Create a dedup key for smooth shading: "posIdx/uvIdx"
    // This allows different normals at the same pos+uv to be merged.
    std::string makeSmoothedKey(int posIdx, int uvIdx) {
        return std::to_string(posIdx) + "/" + std::to_string(uvIdx);
    }

    // Create a dedup key for flat shading: "posIdx/uvIdx/normIdx"
    // This keeps each unique triplet separate for hard edges.
    std::string makeFlatKey(int posIdx, int uvIdx, int normIdx) {
        return std::to_string(posIdx) + "/" + std::to_string(uvIdx) + "/" + std::to_string(normIdx);
    }

    // Load and parse an MTL file, extract Kd and map_Kd for a given material.
    MaterialData loadMTL(const std::string& mtlPath, const std::string& materialName) {
        MaterialData mtl;
        std::ifstream file(mtlPath);
        if (!file.is_open())
            return mtl;  // return defaults if file doesn't exist

        std::string line;
        bool inMaterial = false;

        while (std::getline(file, line)) {
            // Trim leading/trailing whitespace.
            auto start = line.find_first_not_of(" \t");
            auto end = line.find_last_not_of(" \t");
            if (start == std::string::npos) continue;
            line = line.substr(start, end - start + 1);

            auto tokens = split(line);
            if (tokens.empty()) continue;

            if (tokens[0] == "newmtl") {
                if (inMaterial && !materialName.empty()) {
                    break;  // We found a different material, stop searching
                }
                inMaterial = (tokens.size() > 1 && tokens[1] == materialName);
            } else if (inMaterial) {
                if (tokens[0] == "Kd" && tokens.size() >= 4) {
                    mtl.diffuseColor.x = std::stof(tokens[1]);
                    mtl.diffuseColor.y = std::stof(tokens[2]);
                    mtl.diffuseColor.z = std::stof(tokens[3]);
                } else if (tokens[0] == "map_Kd" && tokens.size() >= 2) {
                    mtl.diffuseTexturePath = tokens[1];
                }
            }
        }

        return mtl;
    }
}

LoadedMesh ObjLoader::load(const std::string& absPath) {
    LoadedMesh result;

    std::ifstream file(absPath);
    if (!file.is_open())
        throw std::runtime_error("Failed to open OBJ file: " + absPath);

    // Temporary CPU-side data.
    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals;
    struct UV { float u = 0.0f, v = 0.0f; };
    std::vector<UV> uvs;

    std::string mtllibPath;
    std::string currentMaterial;
    bool smooth = true;

    // Vertex deduplication map: key → index in result.vertices.
    std::unordered_map<std::string, uint32_t> vertsToInds;

    std::string line;
    while (std::getline(file, line)) {
        // Trim leading/trailing whitespace.
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;  // empty line
        auto end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);

        // Skip comments.
        if (line[0] == '#') continue;

        auto tokens = split(line);
        if (tokens.empty()) continue;

        const auto& cmd = tokens[0];

        if (cmd == "v" && tokens.size() >= 4) {
            positions.emplace_back(std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3]));
        } else if (cmd == "vn" && tokens.size() >= 4) {
            normals.emplace_back(std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3]));
        } else if (cmd == "vt" && tokens.size() >= 3) {
            uvs.emplace_back(std::stof(tokens[1]), std::stof(tokens[2]));
        } else if (cmd == "s") {
            // Parse smooth shading flag: "s 1" or "s on" → smooth=true, "s off" or "s 0" → smooth=false
            if (tokens.size() >= 2) {
                const auto& flag = tokens[1];
                smooth = (flag == "1" || flag == "on");
            }
        } else if (cmd == "mtllib" && tokens.size() >= 2) {
            // Store the MTL filename relative to the OBJ directory.
            size_t lastSlash = absPath.find_last_of("/\\");
            std::string objDir = (lastSlash != std::string::npos) ? absPath.substr(0, lastSlash + 1) : "";
            mtllibPath = objDir + tokens[1];
        } else if (cmd == "usemtl" && tokens.size() >= 2) {
            currentMaterial = tokens[1];
        } else if (cmd == "o" && tokens.size() >= 2) {
            result.name = tokens[1];
        } else if (cmd == "g" && tokens.size() >= 2 && result.name.empty()) {
            result.name = tokens[1];
        } else if (cmd == "f" && tokens.size() >= 4) {
            // Parse face: always triangulate (even if it's a quad or n-gon).
            std::vector<FaceVertex> faceVerts;
            for (size_t i = 1; i < tokens.size(); ++i) {
                faceVerts.push_back(parseFaceVertex(tokens[i]));
            }

            // Fan triangulation: split polygon into triangles using the first vertex.
            for (size_t i = 1; i + 1 < faceVerts.size(); ++i) {
                FaceVertex fv0 = faceVerts[0];
                FaceVertex fv1 = faceVerts[i];
                FaceVertex fv2 = faceVerts[i + 1];

                // Process each vertex of the triangle.
                for (const auto& fv : {fv0, fv1, fv2}) {
                    if (fv.posIdx < 0 || fv.posIdx >= (int)positions.size()) {
                        throw std::runtime_error("Invalid position index in face: " + std::to_string(fv.posIdx));
                    }

                    // Create a dedup key.
                    std::string key;
                    if (smooth) {
                        key = makeSmoothedKey(fv.posIdx, fv.uvIdx);
                    } else {
                        key = makeFlatKey(fv.posIdx, fv.uvIdx, fv.normIdx);
                    }

                    // Check if this vertex already exists.
                    auto it = vertsToInds.find(key);
                    uint32_t vertIdx;

                    if (it != vertsToInds.end()) {
                        vertIdx = it->second;

                        // In smooth mode, accumulate normals.
                        if (smooth && fv.normIdx >= 0 && fv.normIdx < (int)normals.size()) {
                            // Add the normal to the existing vertex.
                            result.vertices[vertIdx].normal[0] += normals[fv.normIdx].x;
                            result.vertices[vertIdx].normal[1] += normals[fv.normIdx].y;
                            result.vertices[vertIdx].normal[2] += normals[fv.normIdx].z;
                        }
                    } else {
                        // Create a new vertex.
                        Vertex v{};
                        const auto& pos = positions[fv.posIdx];
                        v.position[0] = pos.x;
                        v.position[1] = pos.y;
                        v.position[2] = pos.z;

                        // Normal: use the specified one, or default to (0, 0, 1) if missing.
                        if (fv.normIdx >= 0 && fv.normIdx < (int)normals.size()) {
                            const auto& norm = normals[fv.normIdx];
                            v.normal[0] = norm.x;
                            v.normal[1] = norm.y;
                            v.normal[2] = norm.z;
                        } else {
                            v.normal[0] = 0.0f;
                            v.normal[1] = 0.0f;
                            v.normal[2] = 1.0f;
                        }

                        // UV: use the specified one, or default to (0, 0) if missing.
                        if (fv.uvIdx >= 0 && fv.uvIdx < (int)uvs.size()) {
                            const auto& uv = uvs[fv.uvIdx];
                            v.uv[0] = uv.u;
                            v.uv[1] = uv.v;
                        } else {
                            v.uv[0] = 0.0f;
                            v.uv[1] = 0.0f;
                        }

                        vertIdx = result.vertices.size();
                        result.vertices.push_back(v);
                        vertsToInds[key] = vertIdx;
                    }

                    result.indices.push_back(vertIdx);
                }
            }
        }
    }

    // Normalize all normals.
    for (auto& vert : result.vertices) {
        math::Vec3 norm{vert.normal[0], vert.normal[1], vert.normal[2]};
        norm = norm.normalize();
        vert.normal[0] = norm.x;
        vert.normal[1] = norm.y;
        vert.normal[2] = norm.z;
    }

    // Load MTL if referenced.
    if (!mtllibPath.empty() && !currentMaterial.empty()) {
        result.material = loadMTL(mtllibPath, currentMaterial);
    }

    return result;
}
