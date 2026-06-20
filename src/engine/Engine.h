#pragma once

#include "engine/BspMap.h"
#include "engine/Console.h"
#include "engine/FileSystem.h"
#include "engine/LaunchOptions.h"
#include "engine/Renderer.h"
#include "engine/SoundSystem.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace goldsrc
{
class Engine
{
public:
    explicit Engine(LaunchOptions options);

    int run();
    void frame();
    std::string versionString() const;

private:
    void registerBuiltins();

    LaunchOptions options_;
    FileSystem fileSystem_;
    SoundSystem soundSystem_;
    Renderer renderer_;
    Console console_;
    BspMap loadedMap_;
    std::optional<Renderer::MapChangeRequest> pendingMapTransition_;
    std::unordered_map<std::string, Renderer::MapRuntimeState> mapRuntimeStates_;
    std::string currentMapName_;
    bool running_ = false;
    std::uint64_t frameNumber_ = 0;
};
}
