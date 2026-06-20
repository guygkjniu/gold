#pragma once

#include "engine/FileSystem.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace goldsrc
{
struct PlaybackVoice;

class SoundSystem
{
public:
    ~SoundSystem();

    void setFileSystem(const FileSystem* fileSystem);
    bool playSound(const std::string& soundPath);
    bool playSound(const std::string& soundPath, float volume);
    bool playEntitySound(const std::string& soundPath, std::int32_t entityIndex, float volume);
    bool playDoorMove(int soundId, std::int32_t entityIndex, float volume);
    bool playDoorStop(int soundId, std::int32_t entityIndex, float volume);
    void stopEntityChannel(std::int32_t entityIndex);
    void stopMapSounds();
    std::size_t playedCount() const;
    std::size_t missingCount() const;

private:
    struct SoundData
    {
        std::uint16_t formatTag = 0;
        std::uint16_t channels = 0;
        std::uint32_t samplesPerSecond = 0;
        std::uint32_t averageBytesPerSecond = 0;
        std::uint16_t blockAlign = 0;
        std::uint16_t bitsPerSample = 0;
        std::vector<std::uint8_t> pcm;
    };

    std::filesystem::path resolveSound(const std::string& soundPath);
    const SoundData* loadSound(const std::string& normalizedSoundPath);
    static bool parseWaveFile(const std::filesystem::path& path, SoundData& sound);
    bool playSoundOnEntityChannel(const std::string& soundPath, std::int32_t entityIndex, float volume);
    bool startPlayback(const SoundData& sound,
                       const std::string& normalizedSoundPath,
                       float volume,
                       std::int32_t* entityIndex);
    void stopAllEntityChannels();

    const FileSystem* fileSystem_ = nullptr;
    std::unordered_map<std::string, std::filesystem::path> resolvedSounds_;
    std::unordered_map<std::string, SoundData> loadedSounds_;
    std::unordered_map<std::int32_t, std::shared_ptr<PlaybackVoice>> entityChannels_;
    std::mutex entityChannelsMutex_;
    std::size_t playedCount_ = 0;
    std::size_t missingCount_ = 0;
};
}
