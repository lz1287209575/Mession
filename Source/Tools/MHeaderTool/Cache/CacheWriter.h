/**
 * @file CacheWriter.h
 * @brief CacheWriter —— 把 SBuildCache 手写序列化成 <CacheDir>/manifest.json。
 *
 * @detail 这是 CacheReader 的镜像:格式由这两个类共同约定,不引入 JSON 库,只覆盖本工程
 *         用到的字段子集(字符串值经 EscapeJsonString 完整转义)。
 *         写入是**整体覆盖**:Save 一次就把 manifest.json 全量替换,没有增量合并或锁定。
 */
#pragma once

#include "BuildCache.h"
#include "Util/FileUtil.h"
#include "Util/StringUtil.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace MHeaderTool {

    // ============================================================================
    // 缓存写入
    // ============================================================================

    /**
     * @brief 缓存写入器(无状态,只有构造时给定的缓存目录)。
     */
    class CacheWriter {
        public:
        explicit CacheWriter(const fs::path& cacheDir) : CacheDir_(cacheDir) {
        }

        /**
         * @brief 把缓存写到 `<CacheDir_>/manifest.json`。
         * @param cache 待写入缓存
         * @return true 写成功;false 目录创建失败或写文件失败
         *
         * @detail 目录不存在时会递归创建。写入是覆盖式的:失败时可能留下被截断的旧文件,
         *         下一次 Load 会因解析失败退化到全量重建。
         */
        bool Save(const SBuildCache& cache) {
            if (!CreateDirectory(CacheDir_)) {
                return false;
            }

            const fs::path manifestPath = CacheDir_ / "manifest.json";
            MString        json         = SerializeCache(cache);
            return WriteFile(manifestPath, json);
        }

        private:
        /**
         * @brief 生成整份 manifest 文本。
         * @param cache 缓存内容
         * @return JSON 文本(以 '\n' 收尾)
         *
         * @detail 只序列化 version / createdAt / entries;`MHeaderToolVersion` 与条目里的
         *         `ParsedContentHash` 当前不落盘(对应字段在 CacheReader 侧也不读回)。
         */
        MString SerializeCache(const SBuildCache& cache) {
            MString json;
            json += "{\n";
            json += "  \"version\": " + std::to_string(cache.Version_) + ",\n";
            json += "  \"createdAt\": " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(cache.CreatedAt.time_since_epoch()).count()) + ",\n";
            json += "  \"entries\": [\n";

            for (size_t i = 0; i < cache.TypeEntries.size(); ++i) {
                if (i > 0)
                    json += ",\n";
                json += SerializeEntry(cache.TypeEntries[i]);
            }

            json += "\n  ]\n";
            json += "}\n";
            return json;
        }

        /**
         * @brief 生成单个条目的 JSON 文本(不含外层数组的逗号)。
         * @param entry 条目
         * @return JSON 文本
         *
         * @detail definedTypes / includedHeaders 为空时整个键被省略(保持文件紧凑);
         *         所有字符串都过 EscapeJsonString。
         */
        MString SerializeEntry(const STypeCacheEntry& entry) {
            MString json;
            json += "    {\n";
            json += "      \"typeName\": \"" + EscapeJsonString(entry.TypeName) + "\",\n";
            json += "      \"headerPath\": \"" + EscapeJsonString(entry.HeaderPath.generic_string()) + "\",\n";
            json += "      \"fileSize\": " + std::to_string(entry.HeaderFingerprint.FileSize) + ",\n";
            json += "      \"modifiedTime\": " + std::to_string(entry.HeaderFingerprint.ModifiedTime) + ",\n";
            json += "      \"contentHash\": " + std::to_string(entry.HeaderFingerprint.ContentHash) + ",\n";
            json += "      \"outputHeader\": \"" + EscapeJsonString(entry.OutputHeader.generic_string()) + "\",\n";
            json += "      \"outputSource\": \"" + EscapeJsonString(entry.OutputSource.generic_string()) + "\"";

            if (!entry.DefinedTypes.empty()) {
                json += ",\n      \"definedTypes\": [";
                for (size_t i = 0; i < entry.DefinedTypes.size(); ++i) {
                    if (i > 0)
                        json += ", ";
                    json += "\"" + EscapeJsonString(entry.DefinedTypes[i]) + "\"";
                }
                json += "]";
            }

            if (!entry.IncludedHeaders.empty()) {
                json += ",\n      \"includedHeaders\": [";
                for (size_t i = 0; i < entry.IncludedHeaders.size(); ++i) {
                    if (i > 0)
                        json += ", ";
                    json += "\"" + EscapeJsonString(entry.IncludedHeaders[i]) + "\"";
                }
                json += "]";
            }

            json += "\n    }";
            return json;
        }

        fs::path CacheDir_;
    };

} // namespace MHeaderTool
