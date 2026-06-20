#include "engine/SoundSystem.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif

namespace goldsrc
{
struct PlaybackVoice
{
#ifdef _WIN32
    HWAVEOUT handle = nullptr;
    WAVEHDR header = {};
    HANDLE doneEvent = nullptr;
    std::vector<std::uint8_t> pcm;
    std::mutex mutex;
#endif
};

namespace
{
std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    if (offset + 2u > bytes.size())
    {
        return 0;
    }
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1u] << 8u));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    if (offset + 4u > bytes.size())
    {
        return 0;
    }
    return static_cast<std::uint32_t>(bytes[offset] |
                                      (bytes[offset + 1u] << 8u) |
                                      (bytes[offset + 2u] << 16u) |
                                      (bytes[offset + 3u] << 24u));
}

bool fourccEquals(const std::vector<std::uint8_t>& bytes, std::size_t offset, const char* text)
{
    return offset + 4u <= bytes.size() && std::memcmp(bytes.data() + offset, text, 4u) == 0;
}

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

#ifdef _WIN32
void cleanupPlaybackVoice(const std::shared_ptr<PlaybackVoice>& voice)
{
    for (;;)
    {
        WaitForSingleObject(voice->doneEvent, INFINITE);
        std::lock_guard<std::mutex> lock(voice->mutex);
        if (!voice->handle)
        {
            return;
        }
        if ((voice->header.dwFlags & WHDR_DONE) == 0)
        {
            continue;
        }

        waveOutUnprepareHeader(voice->handle, &voice->header, sizeof(voice->header));
        waveOutClose(voice->handle);
        CloseHandle(voice->doneEvent);
        voice->handle = nullptr;
        voice->doneEvent = nullptr;
        voice->pcm.clear();
        return;
    }
}
#endif
}

SoundSystem::~SoundSystem()
{
    stopAllEntityChannels();
}

void SoundSystem::setFileSystem(const FileSystem* fileSystem)
{
    stopAllEntityChannels();
    fileSystem_ = fileSystem;
    resolvedSounds_.clear();
    loadedSounds_.clear();
}

bool SoundSystem::playDoorMove(int soundId, std::int32_t entityIndex, float volume)
{
    return playSoundOnEntityChannel(doorMoveSound(soundId), entityIndex, volume);
}

bool SoundSystem::playEntitySound(const std::string& soundPath, std::int32_t entityIndex, float volume)
{
    return playSoundOnEntityChannel(soundPath, entityIndex, volume);
}

bool SoundSystem::playDoorStop(int soundId, std::int32_t entityIndex, float volume)
{
    return playSoundOnEntityChannel(doorStopSound(soundId), entityIndex, volume);
}

void SoundSystem::stopEntityChannel(std::int32_t entityIndex)
{
    std::shared_ptr<PlaybackVoice> voice;
    {
        std::lock_guard<std::mutex> lock(entityChannelsMutex_);
        const auto found = entityChannels_.find(entityIndex);
        if (found == entityChannels_.end())
        {
            return;
        }
        voice = found->second;
        entityChannels_.erase(found);
    }

#ifdef _WIN32
    std::lock_guard<std::mutex> voiceLock(voice->mutex);
    if (voice->handle)
    {
        waveOutReset(voice->handle);
    }
#else
    (void)voice;
#endif
}

void SoundSystem::stopMapSounds()
{
    stopAllEntityChannels();
}

void SoundSystem::stopAllEntityChannels()
{
    std::unordered_map<std::int32_t, std::shared_ptr<PlaybackVoice>> channels;
    {
        std::lock_guard<std::mutex> lock(entityChannelsMutex_);
        channels.swap(entityChannels_);
    }

#ifdef _WIN32
    for (const auto& channel : channels)
    {
        const std::shared_ptr<PlaybackVoice>& voice = channel.second;
        std::lock_guard<std::mutex> voiceLock(voice->mutex);
        if (voice->handle)
        {
            waveOutReset(voice->handle);
        }
    }
#endif
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

bool SoundSystem::parseWaveFile(const std::filesystem::path& path, SoundData& sound)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12u || !fourccEquals(bytes, 0, "RIFF") || !fourccEquals(bytes, 8, "WAVE"))
    {
        return false;
    }

    bool hasFormat = false;
    bool hasData = false;
    for (std::size_t offset = 12u; offset + 8u <= bytes.size();)
    {
        const std::uint32_t chunkSize = readU32(bytes, offset + 4u);
        const std::size_t chunkData = offset + 8u;
        const std::size_t chunkEnd = chunkData + chunkSize;
        if (chunkEnd > bytes.size())
        {
            break;
        }

        if (fourccEquals(bytes, offset, "fmt ") && chunkSize >= 16u)
        {
            sound.formatTag = readU16(bytes, chunkData + 0u);
            sound.channels = readU16(bytes, chunkData + 2u);
            sound.samplesPerSecond = readU32(bytes, chunkData + 4u);
            sound.averageBytesPerSecond = readU32(bytes, chunkData + 8u);
            sound.blockAlign = readU16(bytes, chunkData + 12u);
            sound.bitsPerSample = readU16(bytes, chunkData + 14u);
            hasFormat = true;
        }
        else if (fourccEquals(bytes, offset, "data") && chunkSize > 0u)
        {
            sound.pcm.assign(bytes.begin() + static_cast<std::ptrdiff_t>(chunkData),
                             bytes.begin() + static_cast<std::ptrdiff_t>(chunkEnd));
            hasData = true;
        }

        offset = chunkEnd + (chunkSize & 1u);
    }

    return hasFormat &&
           hasData &&
           sound.formatTag == 1u &&
           sound.channels > 0u &&
           sound.samplesPerSecond > 0u &&
           sound.blockAlign > 0u &&
           !sound.pcm.empty();
}

const SoundSystem::SoundData* SoundSystem::loadSound(const std::string& normalizedSoundPath)
{
    const auto cached = loadedSounds_.find(normalizedSoundPath);
    if (cached != loadedSounds_.end())
    {
        return &cached->second;
    }

    const std::filesystem::path resolved = resolveSound(normalizedSoundPath);
    if (resolved.empty())
    {
        ++missingCount_;
        return nullptr;
    }

    SoundData data;
    if (!parseWaveFile(resolved, data))
    {
        std::cout << "[sound] failed to parse WAV " << resolved.string() << '\n';
        ++missingCount_;
        return nullptr;
    }

    std::cout << "[sound] loaded " << normalizedSoundPath
              << " " << data.channels << "ch "
              << data.samplesPerSecond << "hz "
              << data.bitsPerSample << "bit "
              << data.pcm.size() << " bytes\n";
    auto inserted = loadedSounds_.emplace(normalizedSoundPath, std::move(data));
    return &inserted.first->second;
}

bool SoundSystem::playSound(const std::string& soundPath)
{
    return playSound(soundPath, 1.0f);
}

bool SoundSystem::playSoundOnEntityChannel(const std::string& soundPath,
                                           std::int32_t entityIndex,
                                           float volume)
{
    const std::string normalized = normalizePath(soundPath);
    stopEntityChannel(entityIndex);
    if (normalized.empty() || normalized == "common/null.wav" || normalized == "sound/common/null.wav")
    {
        return false;
    }

    const SoundData* sound = loadSound(normalized);
    if (!sound)
    {
        return false;
    }
    return startPlayback(*sound, normalized, volume, &entityIndex);
}

bool SoundSystem::playSound(const std::string& soundPath, float volume)
{
    const std::string normalized = normalizePath(soundPath);
    if (normalized.empty() || normalized == "common/null.wav" || normalized == "sound/common/null.wav")
    {
        return false;
    }

    const SoundData* sound = loadSound(normalized);
    if (!sound)
    {
        return false;
    }

    return startPlayback(*sound, normalized, volume, nullptr);
}

bool SoundSystem::startPlayback(const SoundData& sound,
                                const std::string& normalizedSoundPath,
                                float volume,
                                std::int32_t* entityIndex)
{
    volume = std::clamp(volume, 0.0f, 1.0f);
    if (volume <= 0.0f)
    {
        return false;
    }

#ifdef _WIN32
    WAVEFORMATEX format = {};
    format.wFormatTag = sound.formatTag;
    format.nChannels = sound.channels;
    format.nSamplesPerSec = sound.samplesPerSecond;
    format.nAvgBytesPerSec = sound.averageBytesPerSecond;
    format.nBlockAlign = sound.blockAlign;
    format.wBitsPerSample = sound.bitsPerSample;

    auto voice = std::make_shared<PlaybackVoice>();
    voice->pcm = sound.pcm;
    voice->doneEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!voice->doneEvent)
    {
        ++missingCount_;
        return false;
    }

    MMRESULT result = waveOutOpen(&voice->handle,
                                  WAVE_MAPPER,
                                  &format,
                                  reinterpret_cast<DWORD_PTR>(voice->doneEvent),
                                  0,
                                  CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR)
    {
        CloseHandle(voice->doneEvent);
        std::cout << "[sound] waveOutOpen failed for " << normalizedSoundPath << " code=" << result << '\n';
        ++missingCount_;
        return false;
    }

    voice->header.lpData = reinterpret_cast<LPSTR>(voice->pcm.data());
    voice->header.dwBufferLength = static_cast<DWORD>(voice->pcm.size());
    result = waveOutPrepareHeader(voice->handle, &voice->header, sizeof(voice->header));
    if (result != MMSYSERR_NOERROR)
    {
        waveOutClose(voice->handle);
        CloseHandle(voice->doneEvent);
        std::cout << "[sound] waveOutPrepareHeader failed for " << normalizedSoundPath << " code=" << result << '\n';
        ++missingCount_;
        return false;
    }

    const DWORD channelVolume = static_cast<DWORD>(volume * 65535.0f);
    waveOutSetVolume(voice->handle, channelVolume | (channelVolume << 16u));

    result = waveOutWrite(voice->handle, &voice->header, sizeof(voice->header));
    if (result != MMSYSERR_NOERROR)
    {
        waveOutUnprepareHeader(voice->handle, &voice->header, sizeof(voice->header));
        waveOutClose(voice->handle);
        CloseHandle(voice->doneEvent);
        std::cout << "[sound] waveOutWrite failed for " << normalizedSoundPath << " code=" << result << '\n';
        ++missingCount_;
        return false;
    }

    if (entityIndex)
    {
        std::lock_guard<std::mutex> lock(entityChannelsMutex_);
        entityChannels_[*entityIndex] = voice;
    }
    std::thread(cleanupPlaybackVoice, voice).detach();
    ++playedCount_;
    return true;
#else
    (void)sound;
    (void)normalizedSoundPath;
    (void)entityIndex;
    ++missingCount_;
    return false;
#endif
}
}
