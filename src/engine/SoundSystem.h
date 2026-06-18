#pragma once

#include "engine/FileSystem.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace goldsrc
{
class SoundSystem
{
public:
    void setFileSystem(const FileSystem* fileSystem);
    bool playSound(const std::string& soundPath);
    bool playDoorMove(int soundId);
    bool playDoorStop(int soundId);
    std::size_t playedCount() const;
    std::size_t missingCount() const;

private:
    std::filesystem::path resolveSound(const std::string& soundPath);

    const FileSystem* fileSystem_ = nullptr;
    std::unordered_map<std::string, std::filesystem::path> resolvedSounds_;
    std::size_t playedCount_ = 0;
    std::size_t missingCount_ = 0;
};
}
