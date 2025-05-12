#include "JamFileCreator.h"
#include "JamFileDumper.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(const int argc, const char** argv)
{
    // Make sure we got two args
    if (argc != 3)
    {
        std::cout << "Invalid input. \nUsage: JamFileTool INPUT.JAM output_directory\n";
        return 1;
    }

    // Get the JAM file and the output directory
    fs::path jamPath(argv[1]);
    fs::path outDir(argv[2]);

    // Check if JAM exists
    if (!fs::exists(jamPath) || !fs::is_regular_file(jamPath))
    {
        std::cout << "Invalid JAM file \"" << jamPath << "\"\n";
        return 1;
    }

    // Check if output directory exists
    if (!fs::exists(outDir) || !fs::is_directory(outDir))
    {
        std::cout << "Invalid output directory \"" << outDir << "\"\n";
        return 1;
    }

    // Dump the JAM
    dumping::DumpJamFile(jamPath.string(), outDir.string());

    return 0;
}
