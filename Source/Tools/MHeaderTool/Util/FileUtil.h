#pragma once

#include <string>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace MHeaderTool
{

// ============================================================================
// File Operations
// ============================================================================

inline MString ReadFile(const fs::path& path)
{
    std::ifstream input(path);
    if (!input)
    {
        return {};
    }
    return MString(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

inline bool WriteFile(const fs::path& path, const MString& content)
{
    std::ofstream output(path);
    if (!output)
    {
        return false;
    }
    output << content;
    return output.good();
}

inline bool FileExists(const fs::path& path)
{
    return fs::exists(path);
}

inline bool IsDirectory(const fs::path& path)
{
    return fs::is_directory(path);
}

inline uint64_t GetFileSize(const fs::path& path)
{
    return fs::file_size(path);
}

inline uint64_t GetModifiedTime(const fs::path& path)
{
    auto time = fs::last_write_time(path);
    return std::chrono::duration_cast<std::chrono::seconds>(
        time.time_since_epoch()).count();
}

inline bool CreateDirectory(const fs::path& path)
{
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

inline fs::path MakeRelativePath(const fs::path& absolutePath, const fs::path& basePath)
{
    fs::path absPath = fs::absolute(absolutePath);
    fs::path base = fs::absolute(basePath);

    std::error_code ec;
    fs::path relPath = fs::relative(absPath, base, ec);
    if (ec)
    {
        // 如果失败，尝试从 Source/ 开始提取
        MString pathStr = absPath.generic_string();
        size_t sourcePos = pathStr.find("Source/");
        if (sourcePos != MString::npos)
        {
            return pathStr.substr(sourcePos);
        }
        return absPath.filename();
    }
    return relPath;
}

// ============================================================================
// Fast File Hashing (for incremental build)
// ============================================================================

inline uint64_t ComputeFileHash(const fs::path& path)
{
    // Fast hash: combine file size with partial content hash
    auto content = ReadFile(path);
    if (content.empty())
    {
        return 0;
    }

    // Simple polynomial hash for strings
    uint64_t hash = content.size();
    for (char c : content)
    {
        hash = hash * 31 + static_cast<unsigned char>(c);
    }
    return hash;
}

inline uint64_t ComputeFileHashFast(const fs::path& path)
{
    // Fast hash: size + head + tail
    auto size = GetFileSize(path);
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return size;
    }

    // Read head (4KB)
    char buffer[8192];
    file.read(buffer, 4096);
    auto headLen = file.gcount();

    // Read tail (4KB)
    file.seekg(0, std::ios::end);
    auto fileSize = file.tellg();
    if (fileSize > 8192)
    {
        file.seekg(static_cast<std::streamoff>(fileSize) - 4096);
        file.read(buffer + headLen, 4096);
    }

    // Compute hash
    uint64_t hash = size;
    for (std::streamsize i = 0; i < headLen + (fileSize > 8192 ? 4096 : 0); ++i)
    {
        hash = hash * 31 + static_cast<unsigned char>(buffer[i]);
    }
    return hash;
}

}  // namespace MHeaderTool
