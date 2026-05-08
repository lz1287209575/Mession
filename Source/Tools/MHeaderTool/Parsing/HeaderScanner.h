#pragma once

#include "Core/Types.h"
#include "Util/FileUtil.h"
#include "Cache/BuildCache.h"
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

namespace MHeaderTool
{

// ============================================================================
// Header Scanner - 文件扫描与增量检测
// ============================================================================

class HeaderScanner
{
public:
    explicit HeaderScanner(const SOptions& options)
        : Options_(options)
    {
    }

    // 扫描所有 header 文件
    std::vector<fs::path> ScanHeaders()
    {
        std::vector<fs::path> headers;

        if (!fs::exists(Options_.SourceRoot))
        {
            return headers;
        }

        for (const auto& entry : fs::recursive_directory_iterator(Options_.SourceRoot))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const fs::path path = entry.path();
            if (path.extension() == ".h")
            {
                headers.push_back(path);
            }
        }

        return headers;
    }

    // 计算文件指纹
    SFileFingerprint ComputeFingerprint(const fs::path& path) const
    {
        SFileFingerprint fp;
        fp.Path = path;
        fp.FileSize = GetFileSize(path);
        fp.ModifiedTime = GetModifiedTime(path);
        fp.ContentHash = ComputeFileHash(path);
        return fp;
    }

    // 检测变化的文件
    SChangeDetectionResult DetectChanges(
        const std::vector<fs::path>& currentFiles,
        const SBuildCache& previousCache) const
    {
        SChangeDetectionResult result;

        // 建立 previous cache 的文件集合
        std::set<fs::path> previousFiles;
        for (const auto& entry : previousCache.TypeEntries)
        {
            previousFiles.insert(entry.HeaderPath);
        }

        // 检测新增文件（包含反射标记的）
        for (const auto& file : currentFiles)
        {
            if (previousFiles.find(file) == previousFiles.end())
            {
                // 新文件
                auto content = ReadFile(file);
                if (HasReflectionMarkers(content))
                {
                    result.AddedFiles.push_back(file);
                    result.ChangedFiles.push_back(file);
                }
            }
        }

        // 检测修改的文件
        for (const auto& file : currentFiles)
        {
            auto currentFp = ComputeFingerprint(file);
            auto prevEntry = previousCache.FindByHeader(file);

            if (prevEntry)
            {
                if (currentFp != prevEntry->HeaderFingerprint)
                {
                    result.ChangedFiles.push_back(file);
                }
            }
        }

        // 检测删除的文件
        for (const auto& file : previousFiles)
        {
            if (std::find(currentFiles.begin(), currentFiles.end(), file) == currentFiles.end())
            {
                result.RemovedFiles.push_back(file);
            }
        }

        return result;
    }

    // 检查文件是否包含反射标记
    static bool HasReflectionMarkers(const std::string& content)
    {
        return content.find("MGENERATED_BODY(") != std::string::npos ||
               content.find("MSTRUCT(") != std::string::npos ||
               content.find("MENUM(") != std::string::npos;
    }

private:
    SOptions Options_;
};

}  // namespace MHeaderTool
