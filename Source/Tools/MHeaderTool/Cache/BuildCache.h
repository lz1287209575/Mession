/**
 * @file BuildCache.h
 * @brief 增量构建缓存的数据模型:文件指纹、单类型缓存条目、整表缓存与决策结果。
 *
 * @detail
 *   - 只描述"缓存里存什么"与"决策长什么样",不含任何 I/O —— 读写分别在 CacheWriter /
 *     CacheReader 里。
 *   - 命名空间是 MHeaderTool(工具层),不涉及生成产物的 ABI。
 *   - 全部为可拷贝的聚合体;索引表(TypeNameToIndex / HeaderPathToIndex)是**派生数据**,
 *     Load 之后必须调用 BuildIndices() 重建,不要跨进程持久化。
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace MHeaderTool {

    // ============================================================================
    // 文件指纹 —— 用于检测文件变化
    // ============================================================================

    /**
     * @brief 一个文件的指纹:大小 + 内容哈希(+ 记录但**不参与比较**的修改时间)。
     */
    struct SFileFingerprint {
        fs::path Path;             // 指纹对应的文件路径(不参与比较)
        uint64_t FileSize     = 0; // 字节数
        uint64_t ModifiedTime = 0; // 修改时间(秒);仅作记录,不参与相等判断
        uint64_t ContentHash  = 0; // 内容哈希,非加密哈希

        /**
         * @brief 指纹是否相等(只看 FileSize + ContentHash)。
         * @param other 待比较指纹
         * @return 两者大小与内容哈希一致时为 true
         *
         * @detail 刻意跳过 Path 与 ModifiedTime:时间戳在不同文件系统/平台上不可比,且
         *         复制文件会改变时间。因此"内容相同但路径不同"的两个文件会判定相等。
         */
        bool operator==(const SFileFingerprint& other) const {
            return FileSize == other.FileSize && ContentHash == other.ContentHash;
        }

        /** @brief operator== 的逻辑取反。 */
        bool operator!=(const SFileFingerprint& other) const {
            return !(*this == other);
        }
    };

    // ============================================================================
    // 单类型缓存条目
    // ============================================================================

    /**
     * @brief 一个反射类型(以头部文件为单位)的缓存条目。
     */
    struct STypeCacheEntry {
        MString          TypeName;              // 反射类型名
        fs::path         HeaderPath;            // 声明所在头文件(相对路径,键空间见 CacheReader 说明)
        SFileFingerprint HeaderFingerprint;     // 上次解析时该头文件的指纹
        uint64_t         ParsedContentHash = 0; // 解析结果相关的内容哈希(NOTE:当前写入/读取两侧都未使用)
        fs::path         OutputHeader;          // 上次生成的 .mgenerated.h 路径
        fs::path         OutputSource;          // 上次生成的 .mgenerated.cpp 路径
        TVector<MString> DefinedTypes;          // 该头文件里定义的全部类型(NOTE:CacheWriter 会写,但 CacheReader 不读回)
        TVector<MString> IncludedHeaders;       // 该头文件直接 include 的头(NOTE:同上,读回时丢失)

        /**
         * @brief 条目是否"可用"(至少要有类型名和头文件路径)。
         * @return 两者都非空时为 true
         *
         * @detail 只做最小校验,不检查输出文件是否存在 —— 那是 IncrementalDriver 的职责。
         */
        bool IsValid() const {
            return !TypeName.empty() && !HeaderPath.empty();
        }
    };

    // ============================================================================
    // 整表缓存
    // ============================================================================

    /**
     * @brief 一次构建的完整缓存内容。
     */
    struct SBuildCache {
        static constexpr uint32_t Version = 2; // 缓存格式版本;不兼容变更时必须递增

        uint32_t                              Version_ = Version; // 实际存盘/读回的版本,反序列化后与 Version 比对
        std::chrono::system_clock::time_point CreatedAt;          // 缓存创建时刻(写入秒级 Unix 时间戳)
        TVector<STypeCacheEntry>              TypeEntries;        // 全部类型条目,顺序即写入顺序
        MString                               MHeaderToolVersion; // NOTE:字段已声明但没有任何读写路径使用

        // 派生索引:不持久化,必须在反序列化后重建
        TMap<MString, size_t>  TypeNameToIndex;   // 类型名 → TypeEntries 下标
        TMap<fs::path, size_t> HeaderPathToIndex; // 头文件路径 → TypeEntries 下标

        /**
         * @brief 依据 TypeEntries 重建两张索引表(先清空)。
         *
         * @detail 同一类型名重复出现时**后者覆盖前者**;同一头文件路径重复出现时**保留第一个**。
         *         索引里存的是下标,任何对 TypeEntries 的增删改都会让它失效。
         */
        void BuildIndices() {
            TypeNameToIndex.clear();
            HeaderPathToIndex.clear();
            for (size_t i = 0; i < TypeEntries.size(); ++i) {
                const auto& entry = TypeEntries[i];
                if (!entry.TypeName.empty()) {
                    TypeNameToIndex[entry.TypeName] = i;
                }
                // 使用第一个出现的路径（不覆盖）
                if (!entry.HeaderPath.empty() && HeaderPathToIndex.find(entry.HeaderPath) == HeaderPathToIndex.end()) {
                    HeaderPathToIndex[entry.HeaderPath] = i;
                }
            }
        }

        /**
         * @brief 按类型名查条目。
         * @param typeName 类型名
         * @return 命中的条目指针;未命中返回 nullptr
         *
         * @detail 返回的是指向 TypeEntries 内部的指针:任何一次 BuildIndices 或对 TypeEntries 的
         *         修改都会让它悬空,不要长期持有。
         */
        const STypeCacheEntry* FindType(const MString& typeName) const {
            auto it = TypeNameToIndex.find(typeName);
            if (it != TypeNameToIndex.end()) {
                return &TypeEntries[it->second];
            }
            return nullptr;
        }

        /**
         * @brief 按头文件路径查条目。
         * @param headerPath 头文件路径(必须与写入时同一键空间)
         * @return 命中的条目指针;未命中返回 nullptr
         *
         * @detail 生命周期约束同 FindType。
         */
        const STypeCacheEntry* FindByHeader(const fs::path& headerPath) const {
            auto it = HeaderPathToIndex.find(headerPath);
            if (it != HeaderPathToIndex.end()) {
                return &TypeEntries[it->second];
            }
            return nullptr;
        }
    };

    // ============================================================================
    // 增量决策结果
    // ============================================================================

    /**
     * @brief 本轮应该做什么。
     *
     * @detail Skip = 全部命中缓存;Regenerate = 只重生成 AffectedFiles/AffectedTypes;
     *         FullRebuild = 忽略缓存全量重建。
     */
    enum class EIncrementalAction { Skip, Regenerate, FullRebuild };

    /**
     * @brief 一次增量决策的完整输出:动作 + 原因 + 受影响范围 + 计数。
     */
    struct SIncrementalDecision {
        EIncrementalAction Action = EIncrementalAction::Skip; // 默认最保守:什么都不做
        MString            Reason;                            // 面向人的决策原因文本(可含计数)
        TVector<MString>   AffectedTypes;                     // 需要重生成的类型名
        TVector<fs::path>  AffectedFiles;                     // 需要重生成的头文件(新增 + 修改 + 删除)
        TVector<fs::path>  RemovedFiles;                      // 已从磁盘消失的头文件(是 AffectedFiles 的子集)
        size_t             SkippedCount     = 0;              // 命中缓存、被跳过的头文件数
        size_t             RegeneratedCount = 0;              // 需要重生成的头文件数
    };

    // ============================================================================
    // 变更检测结果
    // ============================================================================

    /**
     * @brief 文件级变更检测结果(新增/修改/删除 + 是否强制全量重建)。
     *
     * @detail 当前由外部调用方填充/消费;缓存路径本身用的是 SIncrementalDecision。
     */
    struct SChangeDetectionResult {
        TVector<fs::path> ChangedFiles;                 // 内容变化的文件
        TVector<fs::path> AddedFiles;                   // 新增的文件
        TVector<fs::path> RemovedFiles;                 // 删除的文件
        bool              bRequiresFullRebuild = false; // 是否必须放弃缓存全量重建
        MString           RebuildReason;                // 全量重建的原因文本
    };

} // namespace MHeaderTool
