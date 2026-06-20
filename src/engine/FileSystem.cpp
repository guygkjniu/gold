#include "engine/FileSystem.h"

#include <iostream>

namespace goldsrc
{
FileSystem::FileSystem(std::filesystem::path baseDirectory, std::filesystem::path gameName)
    : baseDirectory_(std::filesystem::absolute(std::move(baseDirectory))),
      gameName_(std::move(gameName))
{
}

const std::filesystem::path& FileSystem::baseDirectory() const
{
    return baseDirectory_;
}

std::filesystem::path FileSystem::gameDirectory() const
{
    return baseDirectory_ / gameName_;
}

bool FileSystem::gameDirectoryExists() const
{
    return std::filesystem::is_directory(gameDirectory());
}

bool FileSystem::exists(const std::filesystem::path& relativePath) const
{
    return !resolve(relativePath).empty();
}

std::filesystem::path FileSystem::resolve(const std::filesystem::path& relativePath) const
{
    const std::filesystem::path candidates[] = {
        gameDirectory() / relativePath,
        baseDirectory_ / "valve" / relativePath,
        baseDirectory_ / relativePath,
    };

    for (const std::filesystem::path& candidate : candidates)
    {
        if (std::filesystem::exists(candidate))
        {
            std::cout << "[fs] resolve " << relativePath.string() << " -> " << candidate.string() << '\n';
            return candidate;
        }
    }

    std::cout << "[fs] missing " << relativePath.string() << '\n';
    return {};
}

std::filesystem::path FileSystem::resolveAssetPath(const std::filesystem::path& assetPath) const
{
    if (assetPath.is_absolute() && std::filesystem::exists(assetPath))
    {
        std::cout << "[fs] resolve asset " << assetPath.string() << " -> " << assetPath.string() << '\n';
        return assetPath;
    }

    const std::filesystem::path filename = assetPath.filename();
    const std::filesystem::path candidates[] = {
        gameDirectory() / filename,
        baseDirectory_ / "valve" / filename,
        baseDirectory_ / filename,
        gameDirectory() / assetPath,
        baseDirectory_ / assetPath,
    };

    for (const std::filesystem::path& candidate : candidates)
    {
        if (std::filesystem::exists(candidate))
        {
            std::cout << "[fs] resolve asset " << assetPath.string() << " -> " << candidate.string() << '\n';
            return candidate;
        }
    }

    std::cout << "[fs] missing asset " << assetPath.string() << '\n';
    return {};
}
}
