#include "JamFileDumper.h"

#include "Asset/Bmp/BmpDumper.h"
#include "Asset/Gdb/GdbDumper.h"
#include "Asset/IFileTypeProcessor.h"
#include "Asset/Idb/IdbDumper.h"
#include "Asset/Mdb/MdbDumper.h"
#include "Asset/PassthroughDumper.h"
#include "Asset/Srf/SrfDumper.h"
#include "Asset/Tdb/TdbDumper.h"
#include "Endianness.h"
#include "Jam/JamDiskTypes.h"
#include "StreamUtils.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

using namespace dumping;
using namespace jam;
namespace fs = std::filesystem;

class JamFileReadingException final : public std::exception
{
public:
    explicit JamFileReadingException(std::string msg)
        : m_msg(std::move(msg))
    {
    }

    [[nodiscard]] char const* what() const noexcept override
    {
        return m_msg.c_str();
    }

private:
    std::string m_msg;
};

const IFileTypeProcessor* availableFileTypeDumpers[]{
    new bmp::BmpDumper(),
    new gdb::GdbDumper(),
    new idb::IdbDumper(),
    new mdb::MdbDumper(),
    new srf::SrfDumper(),
    new tdb::TdbDumper(),

    // Passthrough should be last due to it accepting any file and simply dumps its data unmodified
    new PassthroughDumper(),
};

class JamFileDumper
{
public:
    JamFileDumper(std::istream& stream, fs::path dumpFolder)
        : m_stream(stream),
          m_dump_folder(std::move(dumpFolder))
    {
    }

    void Dump()
    {
        uint32_t magicBuffer;
        m_stream.seekg(0, std::ios::beg);

        utils::ReadOrThrow(m_stream, &magicBuffer, sizeof(magicBuffer));
        if (magicBuffer != JAM_FILE_MAGIC)
            throw JamFileReadingException("Not a JAM file");

        DiskDirectoryEntry rootDirectory{};
        memset(rootDirectory.directoryName, 0, std::extent_v<decltype(rootDirectory.directoryName)>);
        rootDirectory.dataOffset = 4u;

        DumpDirectory("", rootDirectory);
    }

private:
    static std::string GetDiskDirectoryPath(const std::string& currentPath, const DiskDirectoryEntry& diskDirectory)
    {
        std::ostringstream ss;
        ss << currentPath;
        if (!currentPath.empty())
            ss << "/";

        const std::string diskDirectoryName(diskDirectory.directoryName,
                                            strnlen(diskDirectory.directoryName, std::extent_v<decltype(diskDirectory.directoryName)>));
        ss << diskDirectoryName;
        return ss.str();
    }

    static std::string GetDiskFilePath(const std::string& currentPath, const std::string& fileName)
    {
        std::ostringstream filePathStream;
        filePathStream << currentPath << "\\" << fileName;
        auto filePath = filePathStream.str();
        std::replace(filePath.begin(), filePath.end(), '/', '\\');
        return filePath;
    }

    [[nodiscard]] std::vector<DiskFileEntry> ReadDirectoryFiles() const
    {
        std::vector<DiskFileEntry> files;
        uint32_t fileCount;
        utils::ReadOrThrow(m_stream, &fileCount, sizeof(fileCount));
        fileCount = endianness::FromLittleEndian(fileCount);

        if (fileCount > 100000)
            throw JamFileReadingException("Too many files");

        for (auto i = 0u; i < fileCount; i++)
        {
            DiskFileEntry file{};
            utils::ReadOrThrow(m_stream, &file, sizeof(file));
            files.emplace_back(file);
        }

        return files;
    }

    [[nodiscard]] std::vector<DiskDirectoryEntry> ReadDirectorySubDirectories() const
    {
        std::vector<DiskDirectoryEntry> subDirectories;
        uint32_t subDirectoryCount;
        utils::ReadOrThrow(m_stream, &subDirectoryCount, sizeof(subDirectoryCount));
        subDirectoryCount = endianness::FromLittleEndian(subDirectoryCount);

        if (subDirectoryCount > 100000)
            throw JamFileReadingException("Too many subdirectories");

        for (auto i = 0u; i < subDirectoryCount; i++)
        {
            DiskDirectoryEntry subDirectory{};
            utils::ReadOrThrow(m_stream, &subDirectory, sizeof(subDirectory));
            subDirectories.emplace_back(subDirectory);
        }

        return subDirectories;
    }

    void DumpFile(const std::string& currentPath, const fs::path& dumpPath, const DiskFileEntry& file) const
    {
        const std::string fileName(file.fileName, strnlen(file.fileName, std::extent_v<decltype(file.fileName)>));
        const auto dumpFilePath = dumpPath / fileName;

        auto fileExtension = fs::path(fileName).extension().string();
        for (auto& c : fileExtension)
            c = static_cast<char>(toupper(c));

        const auto filePath = GetDiskFilePath(currentPath, fileName);

        std::ofstream streamOut(dumpFilePath, std::ios::out | std::ios::binary);
        if (!streamOut.is_open())
        {
            std::ostringstream ss;
            ss << "Could not open file for output: \"" << filePath << "\"";
            throw JamFileReadingException(ss.str());
        }

        if (file.dataSize > 0)
        {
            const auto fileDataBuffer = std::make_unique<char[]>(file.dataSize);
            m_stream.seekg(file.dataOffset, std::ios::beg);
            m_stream.read(fileDataBuffer.get(), file.dataSize);

            for (const auto* fileDumper : availableFileTypeDumpers)
            {
                if (fileDumper->SupportFileExtension(fileExtension))
                {
                    try
                    {
                        fileDumper->ProcessFile(filePath, fileDataBuffer.get(), file.dataSize, streamOut);
                    }
                    catch (std::exception& e)
                    {
                        std::cerr << "Failed to dump JAM file \"" << filePath << "\": " << e.what() << "\n";
                    }
                    break;
                }
            }
        }
    }

    void DumpDirectory(const std::string& currentPath, const DiskDirectoryEntry& diskDirectory)
    {
        const auto diskDirectoryPath = GetDiskDirectoryPath(currentPath, diskDirectory);
        const auto dumpPath = m_dump_folder / diskDirectoryPath;
        fs::create_directories(dumpPath);

        m_stream.seekg(diskDirectory.dataOffset, std::ios::beg);

        const auto files = ReadDirectoryFiles();
        const auto subDirectories = ReadDirectorySubDirectories();

        for (const auto& file : files)
            DumpFile(diskDirectoryPath, dumpPath, file);

        for (const auto& subDirectory : subDirectories)
        {
            if (subDirectory.dataOffset < diskDirectory.dataOffset + sizeof(DiskDirectoryEntry))
                throw JamFileReadingException("SubDirectory does not follow current directory");

            DumpDirectory(diskDirectoryPath, subDirectory);
        }
    }

    std::istream& m_stream;
    fs::path m_dump_folder;
};

void dumping::DumpJamFile(const std::string& filePath, const std::string& outDirPath)
{
    std::ifstream stream(filePath, std::ios::in | std::ios::binary);
    if (!stream.is_open())
    {
        std::cerr << "Failed to open JAM File: \"" << filePath << "\"\n";
        return;
    }

    const fs::path jamFilePath(filePath);
    const fs::path path(outDirPath);
    const auto dumpFolder = path.parent_path() / jamFilePath.filename().replace_extension();
    fs::create_directories(dumpFolder);

    std::cout << "Dumping JAM file \"" << filePath << "\""
              << " to folder \"" << dumpFolder << "\"\n";

    try
    {
        JamFileDumper dumper(stream, dumpFolder);
        dumper.Dump();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to dump JAM file: " << e.what() << "\n";
    }
}
