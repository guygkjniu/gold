#include "engine/SoundSystem.h"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif

namespace goldsrc
{
namespace
{
std::string normalizePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return path;
}

std::string doorMoveSound(int soundId)
{
    if (soundId <= 0 || soundId > 10)
    {
        return "common/null.wav";
    }
    return "doors/doormove" + std::to_string(soundId) + ".wav";
}

std::string doorStopSound(int soundId)
{
    if (soundId <= 0 || soundId > 8)
    {
        return "common/null.wav";
    }
    return "doors/doorstop" + std::to_string(soundId) + ".wav";
}
}

void SoundSystem::setFileSystem(const FileSystem* fileSystem)
{
    fileSystem_ = fileSystem;
    resolvedSounds_.clear();
}

bool SoundSystem::playDoorMove(int soundId)
{
    return playSound(doorMoveSound(soundId));
}

bool SoundSystem::playDoorStop(int soundId)
{
    return playSound(doorStopSound(soundId));
}

std::size_t SoundSystem::playedCount() const
{
    return playedCount_;
}

std::size_t SoundSystem::missingCount() const
{
    return missingCount_;
}

std::filesystem::path SoundSystem::resolveSound(const std::string& soundPath)
{
    if (!fileSystem_)
    {
        return {};
    }

    const std::string normalized = normalizePath(soundPath);
    const auto cached = resolvedSounds_.find(normalized);
    if (cached != resolvedSounds_.end())
    {
        return cached->second;
    }

    const std::filesystem::path relative = normalized.rfind("sound/", 0) == 0
                                               ? std::filesystem::path(normalized)
                                               : std::filesystem::path("sound") / normalized;
    std::filesystem::path resolved = fileSystem_->resolve(relative);
    resolvedSounds_[normalized] = resolved;
    return resolved;
}

bool SoundSystem::playSound(const std::string& soundPath)
{
    const std::string normalized = normalizePath(soundPath);
    if (normalized.empty() || normalized == "common/null.wav" || normalized == "sound/common/null.wav")
    {
        return false;
    }

    const std::filesystem::path resolved = resolveSound(normalized);
    if (resolved.empty())
    {
        ++missingCount_;
        return false;
    }

#ifdef _WIN32
    if (!PlaySoundW(resolved.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
    {
        ++missingCount_;
        return false;
    }
    ++playedCount_;
    return true;
#else
    (void)resolved;
    ++missingCount_;
    return false;
#endif
}
}
