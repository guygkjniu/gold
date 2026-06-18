#pragma once

#include <filesystem>
#include <string>

namespace goldsrc
{
class FileSystem
{
public:
    FileSystem(std::filesystem::path baseDirectory, std::filesystem::path gameName);

    const std::filesystem::path& baseDirectory() const;
    std::filesystem::path gameDirectory() const;
    bool gameDirectoryExists() const;
    bool exists(const std::filesystem::path& relativePath) const;
    std::filesystem::path resolve(const std::filesystem::path& relativePath) const;
    std::filesystem::path resolveAssetPath(const std::filesystem::path& assetPath) const;

private:
    std::filesystem::path baseDirectory_;
    std::filesystem::path gameName_;
};
}
