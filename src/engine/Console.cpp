#include "engine/Console.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace goldsrc
{
void Console::registerCommand(std::string name, CommandHandler handler)
{
    if (name.empty())
    {
        throw std::invalid_argument("console command name cannot be empty");
    }

    commands_[std::move(name)] = std::move(handler);
}

bool Console::execute(const std::string& line)
{
    std::istringstream stream(line);
    std::string name;
    stream >> name;

    if (name.empty())
    {
        return true;
    }

    std::vector<std::string> args;
    std::string arg;
    while (stream >> arg)
    {
        args.push_back(arg);
    }

    const auto command = commands_.find(name);
    if (command == commands_.end())
    {
        std::cout << "unknown command: " << name << '\n';
        return false;
    }

    command->second(args);
    return true;
}
}
