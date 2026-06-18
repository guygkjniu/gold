#include "engine/Engine.h"
#include "engine/LaunchOptions.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        goldsrc::LaunchOptions options = goldsrc::LaunchOptions::parse(argc, argv);
        goldsrc::Engine engine(options);
        return engine.run();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "fatal: " << ex.what() << '\n';
        return 1;
    }
}
