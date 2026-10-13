/**
 * @file IncrementalDriver.h
 * @brief IncrementalDriver —— 对比上次缓存与当前头文件集合,决定本轮是全量、增量还是跳过。
 *
 * @detail 决策是**只读**的:本类不写缓存、不改磁盘,只产出 SIncrementalDecision,
 *         由调用方据 Action 决定后续动作。持有 SOptions 的**拷贝**,构造后不再读取外部状态。
 *         非线程安全(内部无共享状态,但缓存对象由调用方提供,需保证独占)。
 */
#pragma once

#include "BuildCache.h"
#include "CacheReader.h"
#include "Util/FileUtil.h"
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace MHeaderTool {

    // ============================================================================
    // 增量决策
    // ============================================================================

    /**
     * @brief 增量构建决策器:一次 Decide 就是一次完整判定,不保留跨调用的状态。
     */
    class IncrementalDriver {
        public:
        explicit IncrementalDriver(const SOptions& options) : Options_(options) {
        }

        /**
         * @brief 决定本轮该全量、增量还是跳过。
         * @param currentHeaders 当前扫描到的头文件(可以是绝对或相对路径,内部统一转相对)
         * @return 决策结果;AffectedFiles / AffectedTypes 只在 Regenerate/FullRebuild 时非空
         *
         * @detail 判定顺序(先命中者立即返回):
         *   1. `bForceFull` → FullRebuild
         *   2. 缓存不存在/解析失败 → FullRebuild
         *   3. 统计新增、修改(比对内容哈希集合)、删除的文件 → 删除数 > 10 → FullRebuild
         *   4. 缓存版本与 SBuildCache::Version 不一致 → FullRebuild
         *   5. 有受影响文件 → Regenerate,否则 Skip
         *
         *         NOTE: 删除判定用的是 `Options_.SourceRoot / cachedHeader`,而缓存键在
         *         MakeRelativePath 的 fallback 分支会返回带 "Source/" 前缀的仓库相对路径 ——
         *         两种键空间混用时可能把仍存在的头文件误判为已删除。
         */
        SIncrementalDecision Decide(const TVector<fs::path>& currentHeaders) {
            SIncrementalDecision decision;

            if (Options_.bForceFull) {
                decision.Action = EIncrementalAction::FullRebuild;
                decision.Reason = "Force full rebuild requested";
                return decision;
            }

            // 缓存载入是"读一次、只读使用":解析失败即视为没有缓存
            SBuildCache previousCache;
            CacheReader reader(Options_.CacheDir);
            if (!reader.Load(previousCache)) {
                decision.Action = EIncrementalAction::FullRebuild;
                decision.Reason = "No valid cache found";
                return decision;
            }

            // 构建缓存中每个 header 对应的所有条目的哈希集合
            TMap<fs::path, TVector<uint64_t>> headerToHashes;
            TSet<fs::path>                    cachedHeaders;
            for (const auto& entry : previousCache.TypeEntries) {
                headerToHashes[entry.HeaderPath].push_back(entry.HeaderFingerprint.ContentHash);
                cachedHeaders.insert(entry.HeaderPath);
            }

            // 将当前 header 转换为相对路径并去重（每个唯一 header 只处理一次）
            TSet<fs::path>           uniqueCurrentHeaders;
            TMap<fs::path, fs::path> absoluteToRelative;
            for (const auto& header : currentHeaders) {
                fs::path relPath = MakeRelativePath(header);
                if (uniqueCurrentHeaders.insert(relPath).second) {
                    // 新增成功，记录映射
                    absoluteToRelative[header] = relPath;
                }
            }

            // 检测新增文件（header 在缓存中不存在）
            for (const auto& relPath : uniqueCurrentHeaders) {
                if (cachedHeaders.find(relPath) == cachedHeaders.end()) {
                    decision.AffectedFiles.push_back(relPath);
                    ++decision.RegeneratedCount;
                }
            }

            // 检测修改的文件（通过哈希集合比较）
            for (const auto& relPath : uniqueCurrentHeaders) {
                if (cachedHeaders.find(relPath) == cachedHeaders.end()) {
                    // 不在缓存中（新增），已经在上面处理
                    continue;
                }

                // 文件在缓存中，检查是否修改
                auto it = headerToHashes.find(relPath);
                if (it == headerToHashes.end()) {
                    continue;
                }

                // 找到对应的绝对路径来计算指纹
                fs::path absPath;
                for (const auto& kv : absoluteToRelative) {
                    if (kv.second == relPath) {
                        absPath = kv.first;
                        break;
                    }
                }

                if (absPath.empty()) {
                    continue;
                }

                auto currentFp = ComputeFingerprint(absPath);
                // 检查当前文件的哈希是否匹配缓存中的任何一个
                bool foundMatch = false;
                for (uint64_t cachedHash : it->second) {
                    if (currentFp.ContentHash == cachedHash) {
                        foundMatch = true;
                        break;
                    }
                }

                if (!foundMatch) {
                    decision.AffectedFiles.push_back(relPath);
                    // 添加该 header 下所有类型名
                    for (const auto& entry : previousCache.TypeEntries) {
                        if (entry.HeaderPath == relPath) {
                            decision.AffectedTypes.push_back(entry.TypeName);
                        }
                    }
                    ++decision.RegeneratedCount;
                }
            }

            // 检测删除的文件
            for (const auto& cachedHeader : cachedHeaders) {
                fs::path absPath = Options_.SourceRoot / cachedHeader;
                if (!FileExists(absPath)) {
                    decision.RemovedFiles.push_back(cachedHeader);
                    decision.AffectedFiles.push_back(cachedHeader);
                }
            }

            // 删除文件过多(阈值 10)通常意味着目录换位/移动,增量收益为负,直接全量
            if (decision.RemovedFiles.size() > 10) {
                decision.Action = EIncrementalAction::FullRebuild;
                decision.Reason = "Too many files removed (" + std::to_string(decision.RemovedFiles.size()) + ")";
                return decision;
            }

            // 缓存格式版本不一致:旧数据不可信,全量重建
            if (previousCache.Version_ != SBuildCache::Version) {
                decision.Action = EIncrementalAction::FullRebuild;
                decision.Reason = "Cache version mismatch";
                return decision;
            }

            // 有受影响文件 → 增量
            if (!decision.AffectedFiles.empty()) {
                decision.Action = EIncrementalAction::Regenerate;
                // 计数按"传入的头文件个数"而非去重后的个数,故 SkippedCount + RegeneratedCount
                // 不一定等于去重后的头文件数(存在重复入参时)。
                decision.SkippedCount = currentHeaders.size() - decision.RegeneratedCount;
                return decision;
            }

            // 无变化 → 全部跳过
            decision.Action       = EIncrementalAction::Skip;
            decision.SkippedCount = currentHeaders.size();
            return decision;
        }

        /**
         * @brief 把路径折算成相对 Options_.SourceRoot 的路径。
         * @param absolutePath 输入路径(相对路径会先被 fs::absolute 解析)
         * @return 相对路径;fs::relative 失败时退化为"从 'Source/' 开始截取",再失败则只留文件名
         *
         * @detail NOTE: fallback 分支返回的是**仓库相对**路径(以 "Source/" 开头),与正常分支的
         *         "相对 SourceRoot" 不是同一个键空间 —— 混用会让按路径查找/存在性判断失准。
         */
        fs::path MakeRelativePath(const fs::path& absolutePath) const {
            fs::path sourceRoot = fs::absolute(Options_.SourceRoot);
            fs::path absPath    = fs::absolute(absolutePath);

            // 检查是否在 sourceRoot 下
            std::error_code ec;
            fs::path        relPath = fs::relative(absPath, sourceRoot, ec);
            if (ec) {
                // 如果失败，尝试从 Source/ 开始提取
                MString pathStr   = absPath.generic_string();
                size_t  sourcePos = pathStr.find("Source/");
                if (sourcePos != MString::npos) {
                    return pathStr.substr(sourcePos);
                }
                return absPath.filename();
            }
            return relPath;
        }

        /**
         * @brief 计算文件指纹(大小 + 修改时间 + 快速内容哈希)。
         * @param path 文件路径;文件不存在时 fs::file_size / last_write_time 会抛异常(未捕获)
         * @return 指纹
         *
         * @detail 内容哈希用 ComputeFileHashFast(首尾各 4KB),不是全量哈希 —— 中间改动且大小不变的
         *         文件理论上可能漏检,这是"够快且够准"的取舍。
         */
        SFileFingerprint ComputeFingerprint(const fs::path& path) const {
            SFileFingerprint fp;
            fp.Path         = path;
            fp.FileSize     = GetFileSize(path);
            fp.ModifiedTime = GetModifiedTime(path);
            fp.ContentHash  = ComputeFileHashFast(path);
            return fp;
        }

        /**
         * @brief 判断某个类型是否需要重新生成。
         * @param typeName   类型名
         * @param headerPath 声明所在头文件
         * @param cache      上次的缓存
         * @return true 需要重生成;false 可以复用缓存产物
         *
         * @detail 任一条件成立即重生成:缓存里没有该类型、输出头或输出源文件已丢失、
         *         头文件指纹与缓存不一致。
         */
        bool NeedsRegeneration(const MString& typeName, const fs::path& headerPath, const SBuildCache& cache) const {
            auto entry = cache.FindType(typeName);
            if (!entry) {
                return true; // 新类型，需要生成
            }

            // 检查输出文件是否存在
            if (!FileExists(entry->OutputHeader) || !FileExists(entry->OutputSource)) {
                return true; // 输出文件丢失，需要重新生成
            }

            // 检查源文件是否更新
            auto currentFp = ComputeFingerprint(headerPath);
            if (currentFp != entry->HeaderFingerprint) {
                return true; // 源文件更新，需要重新生成
            }

            return false; // 使用缓存
        }

        /**
         * @brief 把"受影响的头文件"翻译成"需要重生成的类型名列表"。
         * @param affectedHeaders 受影响的头文件
         * @param cache           上次的缓存
         * @return 类型名列表;缓存里查不到该头文件时退回使用**文件名**(不是类型名)
         *
         * @detail 一个头文件只按 FindByHeader 取到**第一个**缓存条目,因此同一头文件里的多个类型
         *         不会被全部列出。
         */
        TVector<MString> GetTypesToRegenerate(const TVector<fs::path>& affectedHeaders, const SBuildCache& cache) const {
            TVector<MString> types;

            for (const auto& header : affectedHeaders) {
                auto entry = cache.FindByHeader(header);
                if (entry) {
                    if (NeedsRegeneration(entry->TypeName, header, cache)) {
                        types.push_back(entry->TypeName);
                    }
                } else {
                    // 新文件，添加文件名作为类型名
                    types.push_back(header.filename().string());
                }
            }

            return types;
        }

        private:
        SOptions Options_; // 构造时的选项拷贝;Decide 之外不再变更
    };

} // namespace MHeaderTool
