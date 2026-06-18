#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace goldsrc
{
struct TextureImage
{
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

class WadFile
{
public:
    bool load(const std::filesystem::path& path, std::string& error);
    bool findTexture(const std::string& name, TextureImage& out, std::string& error) const;

private:
    struct Entry
    {
        std::int32_t filePosition = 0;
        std::int32_t diskSize = 0;
        std::int32_t size = 0;
        std::uint8_t type = 0;
        std::string name;
    };

    std::filesystem::path path_;
    std::unordered_map<std::string, Entry> entries_;
};
}
