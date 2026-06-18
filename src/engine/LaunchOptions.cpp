#include "engine/LaunchOptions.h"

#include <filesystem>
#include <stdexcept>

namespace goldsrc
{
LaunchOptions LaunchOptions::parse(int argc, char** argv)
{
    LaunchOptions options;
    options.baseDirectory = std::filesystem::current_path();

    for (int i = 1; i < argc; ++i)
    {
        const std::string token = argv[i];

        if (token == "--run-once")
        {
            options.runOnce = true;
        }
        else if (token == "-basedir")
        {
            options.baseDirectory = requireValue(argc, argv, i, token);
        }
        else if (token == "-game")
        {
            options.gameDirectory = requireValue(argc, argv, i, token);
        }
        else if (!token.empty() && token[0] == '+')
        {
            std::string command = token.substr(1);
            while (i + 1 < argc)
            {
                const std::string next = argv[i + 1];
                if (!next.empty() && (next[0] == '-' || next[0] == '+'))
                {
                    break;
                }
                command += " ";
                command += next;
                ++i;
            }
            options.startupCommands.push_back(std::move(command));
        }
        else
        {
            options.unhandledArguments.push_back(token);
        }
    }

    return options;
}

std::string LaunchOptions::requireValue(int argc, char** argv, int& index, const std::string& option)
{
    if (index + 1 >= argc)
    {
        throw std::runtime_error("missing value for " + option);
    }

    ++index;
    return argv[index];
}
}
