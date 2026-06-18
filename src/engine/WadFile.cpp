#include "engine/WadFile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace goldsrc
{
namespace
{
std::string fixedString(const char* text, std::size_t size)
{
    std::size_t length = 0;
    while (length < size && text[length] != '\0')
    {
        ++length;
    }
    return std::string(text, text + length);
}

std::string normalizeName(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return name;
}

bool readBytes(std::ifstream& file, std::int32_t position, std::int32_t size, std::vector<std::uint8_t>& out)
{
    if (position < 0 || size <= 0)
    {
        return false;
    }

    out.resize(static_cast<std::size_t>(size));
    file.seekg(position, std::ios::beg);
    file.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(file);
}

template <typename T>
T readPod(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    T value = {};
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

bool decodeMipTexture(const std::vector<std::uint8_t>& data, TextureImage& out, std::string& error)
{
    if (data.size() < 40)
    {
        error = "WAD texture entry is too small";
        return false;
    }

    out.name = fixedString(reinterpret_cast<const char*>(data.data()), 16);
    out.width = readPod<std::uint32_t>(data, 16);
    out.height = readPod<std::uint32_t>(data, 20);
    const std::uint32_t offset0 = readPod<std::uint32_t>(data, 24);
    const std::uint32_t offset3 = readPod<std::uint32_t>(data, 36);

    if (out.width == 0 || out.height == 0)
    {
        error = "WAD texture has an invalid size";
        return false;
    }

    const std::uint64_t pixelCount = static_cast<std::uint64_t>(out.width) * out.height;
    if (pixelCount > static_cast<std::uint64_t>(data.size()) || offset0 + pixelCount > data.size())
    {
        error = "WAD texture pixels point outside the entry";
        return false;
    }

    const std::uint32_t mip3Size = std::max(1u, (out.width / 8u) * (out.height / 8u));
    const std::uint32_t paletteCountOffset = offset3 + mip3Size;
    const std::uint32_t paletteOffset = paletteCountOffset + 2u;
    if (paletteOffset + 256u * 3u > data.size())
    {
        error = "WAD texture palette points outside the entry";
        return false;
    }

    out.rgba.resize(static_cast<std::size_t>(pixelCount) * 4u);
    const bool hasMaskedTransparency = !out.name.empty() && out.name[0] == '{';
    for (std::uint64_t i = 0; i < pixelCount; ++i)
    {
        const std::uint8_t index = data[static_cast<std::size_t>(offset0 + i)];
        const std::size_t paletteIndex = static_cast<std::size_t>(paletteOffset) + static_cast<std::size_t>(index) * 3u;
        out.rgba[static_cast<std::size_t>(i) * 4u + 0u] = data[paletteIndex + 0u];
        out.rgba[static_cast<std::size_t>(i) * 4u + 1u] = data[paletteIndex + 1u];
        out.rgba[static_cast<std::size_t>(i) * 4u + 2u] = data[paletteIndex + 2u];
        out.rgba[static_cast<std::size_t>(i) * 4u + 3u] = hasMaskedTransparency && index == 255 ? 0u : 255u;
    }

    return true;
}
}

bool WadFile::load(const std::filesystem::path& path, std::string& error)
{
    error.clear();
    entries_.clear();
    path_ = path;

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "could not open WAD file";
        return false;
    }

    char magic[4] = {};
    std::int32_t lumpCount = 0;
    std::int32_t directoryOffset = 0;
    file.read(magic, sizeof(magic));
    file.read(reinterpret_cast<char*>(&lumpCount), sizeof(lumpCount));
    file.read(reinterpret_cast<char*>(&directoryOffset), sizeof(directoryOffset));

    if (!file || std::strncmp(magic, "WAD3", 4) != 0 || lumpCount < 0 || directoryOffset < 0)
    {
        error = "invalid WAD3 header";
        return false;
    }

    file.seekg(directoryOffset, std::ios::beg);
    for (std::int32_t i = 0; i < lumpCount; ++i)
    {
        Entry entry;
        char name[16] = {};
        std::uint8_t compression = 0;
        std::uint8_t padding[2] = {};

        file.read(reinterpret_cast<char*>(&entry.filePosition), sizeof(entry.filePosition));
        file.read(reinterpret_cast<char*>(&entry.diskSize), sizeof(entry.diskSize));
        file.read(reinterpret_cast<char*>(&entry.size), sizeof(entry.size));
        file.read(reinterpret_cast<char*>(&entry.type), sizeof(entry.type));
        file.read(reinterpret_cast<char*>(&compression), sizeof(compression));
        file.read(reinterpret_cast<char*>(padding), sizeof(padding));
        file.read(name, sizeof(name));

        if (!file)
        {
            error = "could not read WAD directory";
            return false;
        }

        if (compression != 0)
        {
            continue;
        }

        entry.name = fixedString(name, sizeof(name));
        entries_[normalizeName(entry.name)] = entry;
    }

    return true;
}

bool WadFile::findTexture(const std::string& name, TextureImage& out, std::string& error) const
{
    error.clear();
    const auto entry = entries_.find(normalizeName(name));
    if (entry == entries_.end())
    {
        return false;
    }

    std::ifstream file(path_, std::ios::binary);
    if (!file)
    {
        error = "could not reopen WAD file";
        return false;
    }

    std::vector<std::uint8_t> data;
    if (!readBytes(file, entry->second.filePosition, entry->second.diskSize, data))
    {
        error = "could not read WAD texture entry";
        return false;
    }

    return decodeMipTexture(data, out, error);
}
}
