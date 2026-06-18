#pragma once

#include "engine/BspMap.h"
#include "engine/WadFile.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace goldsrc
{
struct StudioVertex
{
    Vec3 position;
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t textureIndex = 0;
};

struct StudioTexture
{
    TextureImage image;
    bool masked = false;
};

struct StudioModel
{
    bool load(const std::filesystem::path& path, std::int32_t body, std::int32_t skin, std::int32_t sequence, std::string& error);

    std::string name;
    Vec3 mins;
    Vec3 maxs;
    float framesPerSecond = 10.0f;
    std::int32_t selectedSequence = 0;
    std::vector<std::vector<StudioVertex>> frames;
    std::vector<StudioTexture> textures;
};

struct StudioModelInstance
{
    std::string modelPath;
    Vec3 origin;
    Vec3 angles;
    std::int32_t body = 0;
    std::int32_t skin = 0;
    std::int32_t sequence = -1;
};

struct StudioModelSceneEntry
{
    StudioModel model;
    std::vector<StudioModelInstance> instances;
};
}
