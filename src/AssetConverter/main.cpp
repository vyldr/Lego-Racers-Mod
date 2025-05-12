#include "Asset/IAssetConverter.h"
#include "Export/Obj/ObjExporter.h"
#include "Export/Obj/ObjImporter.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

IAssetConverter* availableConverters[]{
    new obj::ObjExporter(),
    new obj::ObjImporter(),
};

int main(const int argc, const char** argv)
{
    // Make sure we got two args
    if (argc != 3)
    {
        std::cout << "Invalid input. \nUsage: AssetConverter INPUT.GDB output_directory\n";
        return 1;
    }

    // Get the GDB file and the output directory
    fs::path gdbPath(argv[1]);
    fs::path outDir(argv[2]);

    // Check if GDB exists
    if (!fs::exists(gdbPath) || !fs::is_regular_file(gdbPath))
    {
        std::cout << "Invalid file \"" << gdbPath << "\"\n";
        return 1;
    }

    // Check if output directory exists
    if (!fs::exists(outDir) || !fs::is_directory(outDir))
    {
        std::cout << "Invalid output directory \"" << outDir << "\"\n";
        return 1;
    }

    auto extension = gdbPath.extension().string();
    for (auto& c : extension)
        c = static_cast<char>(toupper(c));

    const auto availableConverter = std::find_if(std::begin(availableConverters),
                                                 std::end(availableConverters),
                                                 [&extension](const IAssetConverter* converter)
                                                 {
                                                     return converter->SupportsExtension(extension);
                                                 });

    if (availableConverter && availableConverter != std::end(availableConverters))
    {
        const auto directory = gdbPath.parent_path().string();
        const auto fileName = gdbPath.filename().string();
        const auto outDirectory = outDir.filename().string();

        (*availableConverter)->Convert(directory, fileName, outDir);
    }
    else
        std::cout << "No converter for file \"" << gdbPath << "\"\n";

    return 0;
}
