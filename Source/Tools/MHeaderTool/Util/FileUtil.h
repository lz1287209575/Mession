/**
 * @file FileUtil.h
 * @brief MHeaderTool 的文件系统辅助:读写、存在性、目录创建与内容哈希。
 *
 * @detail
 *   - 全部是 inline 的小工具,函数各自独立,调用方负责错误处理。
 *   - 读文件失败返回空串;写文件返回 bool;涉及时间/大小的接口在文件不存在时会**抛异常**
 *     (std::filesystem::filesystem_error),调用前请确认路径存在。
 *   - 哈希都是非加密的快速哈希,只用于"内容是否变了"的判定,不能作安全用途。
 */
#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace MHeaderTool {

    // ============================================================================
    // 文件操作
    // ============================================================================

    /**
     * @brief 以文本方式读入整个文件。
     * @param path 文件路径
     * @return 文件内容;打开失败返回空串(无法与"空文件"区分)
     */
    inline MString ReadFile(const fs::path& path) {
        std::ifstream input(path);
        if (!input) {
            return {};
        }
        return MString(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    /**
     * @brief 覆盖写入整个文件(不创建父目录)。
     * @param path    目标路径
     * @param content 待写入内容
     * @return true 写入并 flush 成功;false 打开失败或流状态异常
     */
    inline bool WriteFile(const fs::path& path, const MString& content) {
        std::ofstream output(path);
        if (!output) {
            return false;
        }
        output << content;
        return output.good();
    }

    /**
     * @brief 路径是否存在(文件或目录均可)。
     * @param path 待检查路径
     * @return true 存在
     */
    inline bool FileExists(const fs::path& path) {
        return fs::exists(path);
    }

    /**
     * @brief 是否为目录。
     * @param path 待检查路径
     * @return true 是目录;路径不存在时返回 false
     */
    inline bool IsDirectory(const fs::path& path) {
        return fs::is_directory(path);
    }

    /**
     * @brief 取文件字节数。
     * @param path 文件路径
     * @return 字节数
     *
     * @detail 文件不存在或不可访问时**抛出 std::filesystem::filesystem_error**(未捕获)。
     */
    inline uint64_t GetFileSize(const fs::path& path) {
        return fs::file_size(path);
    }

    /**
     * @brief 取文件最后修改时间。
     * @param path 文件路径
     * @return Unix 时间戳(秒);精度为秒
     *
     * @detail 文件不存在时抛异常(同 GetFileSize)。不同文件系统的 last_write_time 纪元可能不同,
     *         所以该值只适合与"同一台机器上同一路径"的历史值比较。
     */
    inline uint64_t GetModifiedTime(const fs::path& path) {
        auto time = fs::last_write_time(path);
        return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
    }

    /**
     * @brief 递归创建目录(已存在视为成功)。
     * @param path 目录路径
     * @return true 创建成功或本来就在;false 出错(通过 error_code 捕获,不抛异常)
     */
    inline bool CreateDirectory(const fs::path& path) {
        std::error_code ec;
        fs::create_directories(path, ec);
        return !ec;
    }

    /**
     * @brief 求 absolutePath 相对 basePath 的路径。
     * @param absolutePath 目标路径
     * @param basePath     基准目录
     * @return 相对路径;fs::relative 失败时退化为"从 basePath 末级目录名之后截取",再失败则只留文件名
     *
     * @detail fallback 分支保证与正常分支同为"相对 basePath"的键空间,不会再混入带目录前缀的
     *         仓库相对路径(见 IncrementalDriver 里的同名实现)。
     */
    inline fs::path MakeRelativePath(const fs::path& absolutePath, const fs::path& basePath) {
        fs::path absPath = fs::absolute(absolutePath);
        fs::path base    = fs::absolute(basePath);

        std::error_code ec;
        fs::path        relPath = fs::relative(absPath, base, ec);
        if (ec) {
            // fallback:按 base 的末级目录名截断,结果仍相对 base(与正常分支同一键空间)
            const MString pathStr  = absPath.generic_string();
            const MString baseName = base.filename().generic_string();
            if (!baseName.empty()) {
                const MString marker = "/" + baseName + "/";
                const size_t  pos    = pathStr.find(marker);
                if (pos != MString::npos) {
                    return pathStr.substr(pos + marker.size());
                }
            }
            return absPath.filename();
        }
        return relPath;
    }

    // ============================================================================
    // 快速文件哈希(增量构建用)
    // ============================================================================

    /**
     * @brief 全量内容哈希(size 作种子 + 多项式滚动)。
     * @param path 文件路径
     * @return 哈希值;文件不存在或为空时返回 0
     *
     * @detail O(文件大小),慢但精确 —— 与 ComputeFileHashFast **算法不同、不可互换**,
     *         同一个文件用两个函数会得到两个不同的值。
     */
    inline uint64_t ComputeFileHash(const fs::path& path) {
        // 全内容多项式哈希:以文件大小为种子,逐字节滚动
        auto content = ReadFile(path);
        if (content.empty()) {
            return 0;
        }

        // 简单多项式哈希,基数 31
        uint64_t hash = content.size();
        for (char c : content) {
            hash = hash * 31 + static_cast<unsigned char>(c);
        }
        return hash;
    }

    /**
     * @brief 快速哈希:只用"文件大小 + 首 4KB + 尾 4KB"。
     * @param path 文件路径
     * @return 哈希值;打不开文件时返回文件大小(退化为弱判别)
     *
     * @detail O(1),用于增量构建的前置判断。代价:仅改动文件中段且总大小不变的文件
     *         可能得到相同哈希而漏检;需要严格正确性时用 ComputeFileHash。
     */
    inline uint64_t ComputeFileHashFast(const fs::path& path) {
        // 快速哈希:文件大小 + 首块 4KB + 尾块 4KB
        auto          size = GetFileSize(path);
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return size;
        }

        // 读首块 4KB
        char buffer[8192];
        file.read(buffer, 4096);
        auto headLen = file.gcount();

        // 读尾块 4KB
        file.seekg(0, std::ios::end);
        auto fileSize = file.tellg();
        if (fileSize > 8192) {
            file.seekg(static_cast<std::streamoff>(fileSize) - 4096);
            file.read(buffer + headLen, 4096);
        }

        // 头尾拼接后再滚动:buffer 里已有 headLen 字节,尾块接在 headLen 之后
        uint64_t hash = size;
        for (std::streamsize i = 0; i < headLen + (fileSize > 8192 ? 4096 : 0); ++i) {
            hash = hash * 31 + static_cast<unsigned char>(buffer[i]);
        }
        return hash;
    }

} // namespace MHeaderTool
