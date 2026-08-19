#pragma once

#include "BuildCache.h"
#include "Util/FileUtil.h"
#include "Util/StringUtil.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace MHeaderTool {

    // ============================================================================
    // Cache Writer - 写入缓存
    // ============================================================================

    class CacheWriter {
        public:
        explicit CacheWriter(const fs::path& cacheDir) : CacheDir_(cacheDir) {
        }

        // 保存缓存
        bool Save(const SBuildCache& cache) {
            if (!CreateDirectory(CacheDir_)) {
                return false;
            }

            const fs::path manifestPath = CacheDir_ / "manifest.json";
            MString        json         = SerializeCache(cache);
            return WriteFile(manifestPath, json);
        }

        private:
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
