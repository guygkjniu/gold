#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace goldsrc
{
struct LaunchOptions
{
    std::filesystem::path baseDirectory;
    std::filesystem::path gameDirectory = "valve";
    std::vector<std::string> startupCommands;
    std::vector<std::string> unhandledArguments;
    bool runOnce = false;

    static LaunchOptions parse(int argc, char** argv);

private:
    static std::string requireValue(int argc, char** argv, int& index, const std::string& option);
};
}
