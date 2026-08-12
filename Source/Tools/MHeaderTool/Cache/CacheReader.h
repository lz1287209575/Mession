#pragma once

#include "BuildCache.h"
#include "Util/FileUtil.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace MHeaderTool
{

// ============================================================================
// Cache Reader - 读取缓存
// ============================================================================

class CacheReader
{
public:
    explicit CacheReader(const fs::path& cacheDir)
        : CacheDir_(cacheDir)
    {
    }

    // 加载缓存
    bool Load(SBuildCache& outCache)
    {
        const fs::path manifestPath = CacheDir_ / "manifest.json";
        if (!FileExists(manifestPath))
        {
            return false;
        }

        auto content = ReadFile(manifestPath);
        if (content.empty())
        {
            return false;
        }

        // 简单的 JSON 解析
        return ParseManifest(content, outCache);
    }

    // 检查缓存是否有效
    bool IsCacheValid() const
    {
        const fs::path manifestPath = CacheDir_ / "manifest.json";
        return FileExists(manifestPath);
    }

private:
    bool ParseManifest(const MString& content, SBuildCache& outCache)
    {
        // 简单的版本检查
        if (content.find("\"version\":") != MString::npos)
        {
            // 找到版本号
            size_t versionPos = content.find("\"version\":");
            size_t colonPos = content.find(':', versionPos);
            if (colonPos != MString::npos)
            {
                size_t start = colonPos + 1;
                size_t end = content.find_first_of(",}\n", start);
                if (end == MString::npos) end = content.size();
                MString versionStr = content.substr(start, end - start);
                outCache.Version_ = std::stoul(versionStr);
            }
        }

        // 解析类型条目
        // 格式: "entries": [...]
        size_t entriesPos = content.find("\"entries\":");
        if (entriesPos == MString::npos)
        {
            return false;
        }

        // 简化的解析：提取每个条目的关键信息
        size_t arrayStart = content.find('[', entriesPos);
        size_t arrayEnd = content.find_last_of(']');
        if (arrayStart == MString::npos || arrayEnd == MString::npos)
        {
            return false;
        }

        MString entriesContent = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

        // 提取每个条目（使用括号匹配来正确处理嵌套）
        size_t pos = 0;
        int entryCount = 0;
        while (pos < entriesContent.size())
        {
            // 跳过空白
            while (pos < entriesContent.size() && std::isspace(static_cast<unsigned char>(entriesContent[pos])))
            {
                ++pos;
            }
            if (pos >= entriesContent.size() || entriesContent[pos] != '{')
            {
                break;
            }

            // 找到匹配的 }
            size_t entryStart = pos;
            int braceDepth = 0;
            bool inString = false;
            while (pos < entriesContent.size())
            {
                char c = entriesContent[pos];
                if (c == '"' && (pos == 0 || entriesContent[pos-1] != '\\'))
                {
                    inString = !inString;
                }
                else if (!inString)
                {
                    if (c == '{') ++braceDepth;
                    else if (c == '}')
                    {
                        --braceDepth;
                        if (braceDepth == 0)
                        {
                            break;
                        }
                    }
                }
                ++pos;
            }

            size_t entryEnd = pos;
            MString entryContent = entriesContent.substr(entryStart, entryEnd - entryStart + 1);
            ParseEntry(entryContent, outCache.TypeEntries);
            ++entryCount;

            pos = entryEnd + 1;
        }
        // Debug output
        // std::cerr << "CacheReader: parsed " << entryCount << " entries\n";

        outCache.BuildIndices();
        return true;
    }

    void ParseEntry(const MString& content, TVector<STypeCacheEntry>& entries)
    {
        STypeCacheEntry entry;

        // 提取 typeName
        if (auto value = ExtractJsonString(content, "typeName"))
            entry.TypeName = *value;

        // 提取 headerPath
        if (auto value = ExtractJsonString(content, "headerPath"))
            entry.HeaderPath = *value;

        // 提取 fileSize
        if (auto value = ExtractJsonNumber(content, "fileSize"))
            entry.HeaderFingerprint.FileSize = *value;

        // 提取 modifiedTime
        if (auto value = ExtractJsonNumber(content, "modifiedTime"))
            entry.HeaderFingerprint.ModifiedTime = *value;

        // 提取 contentHash
        if (auto value = ExtractJsonNumber(content, "contentHash"))
            entry.HeaderFingerprint.ContentHash = *value;

        // 提取 outputHeader
        if (auto value = ExtractJsonString(content, "outputHeader"))
            entry.OutputHeader = *value;

        // 提取 outputSource
        if (auto value = ExtractJsonString(content, "outputSource"))
            entry.OutputSource = *value;

        if (!entry.TypeName.empty())
        {
            entries.push_back(entry);
        }
    }

    TOptional<MString> ExtractJsonString(const MString& content, const MString& key)
    {
        MString pattern = "\"" + key + "\"";
        size_t keyPos = content.find(pattern);
        if (keyPos == MString::npos) return std::nullopt;

        size_t colonPos = content.find(':', keyPos);
        if (colonPos == MString::npos) return std::nullopt;

        size_t valueStart = colonPos + 1;
        while (valueStart < content.size() && std::isspace(static_cast<unsigned char>(content[valueStart])))
            ++valueStart;

        if (valueStart >= content.size() || content[valueStart] != '"') return std::nullopt;

        size_t valueEnd = valueStart + 1;
        while (valueEnd < content.size() && content[valueEnd] != '"')
        {
            if (content[valueEnd] == '\\') ++valueEnd;
            ++valueEnd;
        }

        return content.substr(valueStart + 1, valueEnd - valueStart - 1);
    }

    TOptional<uint64_t> ExtractJsonNumber(const MString& content, const MString& key)
    {
        MString pattern = "\"" + key + "\"";
        size_t keyPos = content.find(pattern);
        if (keyPos == MString::npos) return std::nullopt;

        size_t colonPos = content.find(':', keyPos);
        if (colonPos == MString::npos) return std::nullopt;

        size_t valueStart = colonPos + 1;
        while (valueStart < content.size() && std::isspace(static_cast<unsigned char>(content[valueStart])))
            ++valueStart;

        size_t valueEnd = valueStart;
        while (valueEnd < content.size() &&
               (std::isdigit(static_cast<unsigned char>(content[valueEnd])) || content[valueEnd] == '.'))
        {
            ++valueEnd;
        }

        if (valueEnd == valueStart) return std::nullopt;

        return std::stoull(content.substr(valueStart, valueEnd - valueStart));
    }

    fs::path CacheDir_;
};

}  // namespace MHeaderTool
