#pragma once

#include "BuildCache.h"
#include "CacheReader.h"
#include "Util/FileUtil.h"
#include <filesystem>
#include <set>
#include <map>

namespace fs = std::filesystem;

namespace MHeaderTool
{

// ============================================================================
// Incremental Driver - 增量编译决策
// ============================================================================

class IncrementalDriver
{
public:
    explicit IncrementalDriver(const SOptions& options)
        : Options_(options)
    {
    }

    // 决定增量编译策略
    SIncrementalDecision Decide(const std::vector<fs::path>& currentHeaders)
    {
        SIncrementalDecision decision;

        if (Options_.bForceFull)
        {
            decision.Action = EIncrementalAction::FullRebuild;
            decision.Reason = "Force full rebuild requested";
            return decision;
        }

        SBuildCache previousCache;
        CacheReader reader(Options_.CacheDir);
        if (!reader.Load(previousCache))
        {
            decision.Action = EIncrementalAction::FullRebuild;
            decision.Reason = "No valid cache found";
            return decision;
        }

        // 构建缓存中每个 header 对应的所有条目的哈希集合
        std::map<fs::path, std::vector<uint64_t>> headerToHashes;
        std::set<fs::path> cachedHeaders;
        for (const auto& entry : previousCache.TypeEntries)
        {
            headerToHashes[entry.HeaderPath].push_back(entry.HeaderFingerprint.ContentHash);
            cachedHeaders.insert(entry.HeaderPath);
        }

        // 将当前 header 转换为相对路径并去重（每个唯一 header 只处理一次）
        std::set<fs::path> uniqueCurrentHeaders;
        std::map<fs::path, fs::path> absoluteToRelative;
        for (const auto& header : currentHeaders)
        {
            fs::path relPath = MakeRelativePath(header);
            if (uniqueCurrentHeaders.insert(relPath).second)
            {
                // 新增成功，记录映射
                absoluteToRelative[header] = relPath;
            }
        }

        // 检测新增文件（header 在缓存中不存在）
        for (const auto& relPath : uniqueCurrentHeaders)
        {
            if (cachedHeaders.find(relPath) == cachedHeaders.end())
            {
                decision.AffectedFiles.push_back(relPath);
                ++decision.RegeneratedCount;
            }
        }

        // 检测修改的文件（通过哈希集合比较）
        for (const auto& relPath : uniqueCurrentHeaders)
        {
            if (cachedHeaders.find(relPath) == cachedHeaders.end())
            {
                // 不在缓存中（新增），已经在上面处理
                continue;
            }

            auto it = headerToHashes.find(relPath);
            if (it == headerToHashes.end())
            {
                continue;
            }

            // 找到对应的绝对路径来计算指纹
            fs::path absPath;
            for (const auto& kv : absoluteToRelative)
            {
                if (kv.second == relPath)
                {
                    absPath = kv.first;
                    break;
                }
            }

            if (absPath.empty())
            {
                continue;
            }

            auto currentFp = ComputeFingerprint(absPath);
            // 检查当前文件的哈希是否匹配缓存中的任何一个
            bool foundMatch = false;
            for (uint64_t cachedHash : it->second)
            {
                if (currentFp.ContentHash == cachedHash)
                {
                    foundMatch = true;
                    break;
                }
            }

            if (!foundMatch)
            {
                decision.AffectedFiles.push_back(relPath);
                // 添加该 header 下所有类型名
                for (const auto& entry : previousCache.TypeEntries)
                {
                    if (entry.HeaderPath == relPath)
                    {
                        decision.AffectedTypes.push_back(entry.TypeName);
                    }
                }
                ++decision.RegeneratedCount;
            }
        }

        // 检测删除的文件
        for (const auto& cachedHeader : cachedHeaders)
        {
            fs::path absPath = Options_.SourceRoot / cachedHeader;
            if (!FileExists(absPath))
            {
                decision.RemovedFiles.push_back(cachedHeader);
                decision.AffectedFiles.push_back(cachedHeader);
            }
        }

        // 决定是否需要完全重建
        if (decision.RemovedFiles.size() > 10)
        {
            decision.Action = EIncrementalAction::FullRebuild;
            decision.Reason = "Too many files removed (" + std::to_string(decision.RemovedFiles.size()) + ")";
            return decision;
        }

        // 检查缓存版本
        if (previousCache.Version_ != SBuildCache::Version)
        {
            decision.Action = EIncrementalAction::FullRebuild;
            decision.Reason = "Cache version mismatch";
            return decision;
        }

        // 如果有受影响的文件，进行增量编译
        if (!decision.AffectedFiles.empty())
        {
            decision.Action = EIncrementalAction::Regenerate;
            decision.SkippedCount = currentHeaders.size() - decision.RegeneratedCount;
            return decision;
        }

        // 没有变化，跳过
        decision.Action = EIncrementalAction::Skip;
        decision.SkippedCount = currentHeaders.size();
        return decision;
    }

    // 将绝对路径转换为相对路径
    fs::path MakeRelativePath(const fs::path& absolutePath) const
    {
        fs::path sourceRoot = fs::absolute(Options_.SourceRoot);
        fs::path absPath = fs::absolute(absolutePath);

        // 检查是否在 sourceRoot 下
        std::error_code ec;
        fs::path relPath = fs::relative(absPath, sourceRoot, ec);
        if (ec)
        {
            // 如果失败，尝试从 Source/ 开始提取
            std::string pathStr = absPath.generic_string();
            size_t sourcePos = pathStr.find("Source/");
            if (sourcePos != std::string::npos)
            {
                return pathStr.substr(sourcePos);
            }
            return absPath.filename();
        }
        return relPath;
    }

    // 计算文件指纹
    SFileFingerprint ComputeFingerprint(const fs::path& path) const
    {
        SFileFingerprint fp;
        fp.Path = path;
        fp.FileSize = GetFileSize(path);
        fp.ModifiedTime = GetModifiedTime(path);
        fp.ContentHash = ComputeFileHashFast(path);
        return fp;
    }

    // 检查是否需要重新生成某个类型
    bool NeedsRegeneration(
        const std::string& typeName,
        const fs::path& headerPath,
        const SBuildCache& cache) const
    {
        auto entry = cache.FindType(typeName);
        if (!entry)
        {
            return true;  // 新类型，需要生成
        }

        // 检查输出文件是否存在
        if (!FileExists(entry->OutputHeader) || !FileExists(entry->OutputSource))
        {
            return true;  // 输出文件丢失，需要重新生成
        }

        // 检查源文件是否更新
        auto currentFp = ComputeFingerprint(headerPath);
        if (currentFp != entry->HeaderFingerprint)
        {
            return true;  // 源文件更新，需要重新生成
        }

        return false;  // 使用缓存
    }

    // 获取需要重新生成的类型列表
    std::vector<std::string> GetTypesToRegenerate(
        const std::vector<fs::path>& affectedHeaders,
        const SBuildCache& cache) const
    {
        std::vector<std::string> types;

        for (const auto& header : affectedHeaders)
        {
            auto entry = cache.FindByHeader(header);
            if (entry)
            {
                if (NeedsRegeneration(entry->TypeName, header, cache))
                {
                    types.push_back(entry->TypeName);
                }
            }
            else
            {
                // 新文件，添加文件名作为类型名
                types.push_back(header.filename().string());
            }
        }

        return types;
    }

private:
    SOptions Options_;
};

}  // namespace MHeaderTool
