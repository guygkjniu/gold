#pragma once

#include "engine/BspMap.h"
#include "engine/Console.h"
#include "engine/FileSystem.h"
#include "engine/LaunchOptions.h"
#include "engine/Renderer.h"
#include "engine/SoundSystem.h"

#include <cstdint>
#include <string>

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
    bool running_ = false;
    std::uint64_t frameNumber_ = 0;
};
}
