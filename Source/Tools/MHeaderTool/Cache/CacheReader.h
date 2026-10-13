/**
 * @file CacheReader.h
 * @brief CacheReader —— 读取 <CacheDir>/manifest.json 并还原成 SBuildCache。
 *
 * @detail 手写解析(不引入 JSON 库):按"键名字符串 + 冒号 + 首个字面量"的启发式提取字段,
 *         只要能读回自己写出的格式即可,不是通用 JSON 解析器。
 *         解析失败(缺 version/entries 数组)时返回 false,调用方据此退化为全量重建。
 */
#pragma once

#include "BuildCache.h"
#include "Util/FileUtil.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace MHeaderTool {

    // ============================================================================
    // 缓存读取
    // ============================================================================

    /**
     * @brief 缓存读取器(无状态,只有构造时给定的缓存目录)。
     */
    class CacheReader {
        public:
        explicit CacheReader(const fs::path& cacheDir) : CacheDir_(cacheDir) {
        }

        /**
         * @brief 读取并解析缓存文件。
         * @param outCache [out] 解析结果;注意是**追加**语义,调用前应保证其为空
         * @return true 解析成功;false 文件不存在/为空,或缺少 version/entries 结构
         *
         * @detail 只做格式解析,不做版本校验(版本比对在 IncrementalDriver::Decide 里)。
         *         解析成功后会自动调用 outCache.BuildIndices()。
         *         陷阱:TypeEntries 不会被清空 —— 同一个 outCache 对象重复 Load 会累积重复条目。
         */
        bool Load(SBuildCache& outCache) {
            const fs::path manifestPath = CacheDir_ / "manifest.json";
            if (!FileExists(manifestPath)) {
                return false;
            }

            auto content = ReadFile(manifestPath);
            if (content.empty()) {
                return false;
            }

            // 简单的 JSON 解析
            return ParseManifest(content, outCache);
        }

        /**
         * @brief 缓存文件是否存在(只判断存在性,不解析内容)。
         * @return manifest.json 存在时为 true
         */
        bool IsCacheValid() const {
            const fs::path manifestPath = CacheDir_ / "manifest.json";
            return FileExists(manifestPath);
        }

        private:
        /**
         * @brief 解析 manifest 文本并把条目追加进 outCache。
         * @param content  文件全文
         * @param outCache [out] 追加目标;函数内不改动 Version_ 之外的其他旧字段
         * @return true 解析到 entries 数组;false 结构缺失
         *
         * @detail NOTE: 只回填 version 与条目,`createdAt` 与 `MHeaderToolVersion` **不会**被读回;
         *         条目里的 definedTypes / includedHeaders / parsedContentHash 同样不读回
         *         (写入侧写了,读取侧忽略,往返是有损的)。
         */
        bool ParseManifest(const MString& content, SBuildCache& outCache) {
            // 版本:用"首次出现"的 "version": 后面第一个数字片段,不做嵌套/重复键处理
            if (content.find("\"version\":") != MString::npos) {
                // 找到版本号
                size_t versionPos = content.find("\"version\":");
                size_t colonPos   = content.find(':', versionPos);
                if (colonPos != MString::npos) {
                    size_t start = colonPos + 1;
                    size_t end   = content.find_first_of(",}\n", start);
                    if (end == MString::npos)
                        end = content.size();
                    MString versionStr = content.substr(start, end - start);
                    outCache.Version_  = std::stoul(versionStr);
                }
            }

            // 类型条目:定位 "entries": 后的第一个 '[' 与全文最后一个 ']'
            size_t entriesPos = content.find("\"entries\":");
            if (entriesPos == MString::npos) {
                return false;
            }

            // entriesPos 之后首个 '[' 作数组起点;末尾 ']' 用全文查找(要求文件以数组收尾)
            size_t arrayStart = content.find('[', entriesPos);
            size_t arrayEnd   = content.find_last_of(']');
            if (arrayStart == MString::npos || arrayEnd == MString::npos) {
                return false;
            }

            MString entriesContent = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

            // 逐条目扫描:用花括号配平(且在字符串内不计数)找出每个 { ... } 的完整范围
            size_t pos        = 0;
            int    entryCount = 0;
            while (pos < entriesContent.size()) {
                // 跳过空白
                while (pos < entriesContent.size() && std::isspace(static_cast<unsigned char>(entriesContent[pos]))) {
                    ++pos;
                }
                if (pos >= entriesContent.size() || entriesContent[pos] != '{') {
                    break;
                }

                // 找到匹配的 }
                size_t entryStart = pos;
                int    braceDepth = 0;
                bool   inString   = false;
                while (pos < entriesContent.size()) {
                    char c = entriesContent[pos];
                    if (c == '"' && (pos == 0 || entriesContent[pos - 1] != '\\')) {
                        inString = !inString;
                    } else if (!inString) {
                        if (c == '{')
                            ++braceDepth;
                        else if (c == '}') {
                            --braceDepth;
                            if (braceDepth == 0) {
                                break;
                            }
                        }
                    }
                    ++pos;
                }

                size_t  entryEnd     = pos;
                MString entryContent = entriesContent.substr(entryStart, entryEnd - entryStart + 1);
                ParseEntry(entryContent, outCache.TypeEntries);
                ++entryCount;

                pos = entryEnd + 1;
            }
            // entryCount 只用于统计,调试输出此前已被注释掉;需要排查时取消下面一行的注释。

            // 索引是派生数据,解析完必须重建
            outCache.BuildIndices();
            return true;
        }

        /**
         * @brief 解析单个 `{...}` 条目并追加进 entries。
         * @param content 条目 JSON 文本(含外层花括号)
         * @param entries [out] 追加目标
         *
         * @detail 只有当 typeName 非空时才追加 —— 解析不出类型名的条目会被静默丢弃,
         *         避免把半截数据写进缓存。
         */
        void ParseEntry(const MString& content, TVector<STypeCacheEntry>& entries) {
            STypeCacheEntry entry;

            if (auto value = ExtractJsonString(content, "typeName"))
                entry.TypeName = *value;

            if (auto value = ExtractJsonString(content, "headerPath"))
                entry.HeaderPath = *value;

            if (auto value = ExtractJsonNumber(content, "fileSize"))
                entry.HeaderFingerprint.FileSize = *value;

            if (auto value = ExtractJsonNumber(content, "modifiedTime"))
                entry.HeaderFingerprint.ModifiedTime = *value;

            if (auto value = ExtractJsonNumber(content, "contentHash"))
                entry.HeaderFingerprint.ContentHash = *value;

            if (auto value = ExtractJsonString(content, "outputHeader"))
                entry.OutputHeader = *value;

            if (auto value = ExtractJsonString(content, "outputSource"))
                entry.OutputSource = *value;

            if (!entry.TypeName.empty()) {
                entries.push_back(entry);
            }
        }

        /**
         * @brief 取 `"key": "value"` 里的字符串 value(已处理反斜杠转义,**不**做转义还原)。
         * @param content 待搜索文本
         * @param key     键名(不带引号)
         * @return 命中的原始子串;键缺失或 value 不是字符串字面量时返回 std::nullopt
         *
         * @detail 只是"找第一次出现",因此同名键(或出现在字符串里)会误命中 —— 对自产格式足够。
         */
        TOptional<MString> ExtractJsonString(const MString& content, const MString& key) {
            MString pattern = "\"" + key + "\"";
            size_t  keyPos  = content.find(pattern);
            if (keyPos == MString::npos)
                return std::nullopt;

            size_t colonPos = content.find(':', keyPos);
            if (colonPos == MString::npos)
                return std::nullopt;

            size_t valueStart = colonPos + 1;
            while (valueStart < content.size() && std::isspace(static_cast<unsigned char>(content[valueStart])))
                ++valueStart;

            if (valueStart >= content.size() || content[valueStart] != '"')
                return std::nullopt;

            size_t valueEnd = valueStart + 1;
            while (valueEnd < content.size() && content[valueEnd] != '"') {
                if (content[valueEnd] == '\\')
                    ++valueEnd;
                ++valueEnd;
            }

            return content.substr(valueStart + 1, valueEnd - valueStart - 1);
        }

        /**
         * @brief 取 `"key": 123` 里的无符号整数 value。
         * @param content 待搜索文本
         * @param key     键名(不带引号)
         * @return 命中的数值;键缺失或后面不是数字时返回 std::nullopt
         *
         * @detail 允许数字里出现 '.',交给 std::stoull 截断取整;超出 uint64_t 会抛出
         *         std::out_of_range(调用方未捕获)。
         */
        TOptional<uint64_t> ExtractJsonNumber(const MString& content, const MString& key) {
            MString pattern = "\"" + key + "\"";
            size_t  keyPos  = content.find(pattern);
            if (keyPos == MString::npos)
                return std::nullopt;

            size_t colonPos = content.find(':', keyPos);
            if (colonPos == MString::npos)
                return std::nullopt;

            size_t valueStart = colonPos + 1;
            while (valueStart < content.size() && std::isspace(static_cast<unsigned char>(content[valueStart])))
                ++valueStart;

            size_t valueEnd = valueStart;
            while (valueEnd < content.size() && (std::isdigit(static_cast<unsigned char>(content[valueEnd])) || content[valueEnd] == '.')) {
                ++valueEnd;
            }

            if (valueEnd == valueStart)
                return std::nullopt;

            return std::stoull(content.substr(valueStart, valueEnd - valueStart));
        }

        fs::path CacheDir_;
    };

} // namespace MHeaderTool
