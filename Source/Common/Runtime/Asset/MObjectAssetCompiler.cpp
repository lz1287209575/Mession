/**
 * @file MObjectAssetCompiler.cpp
 * @brief MObjectAssetCompiler 实现:整文件读写 + 转发二进制编译。
 *
 * @detail 文件读写都走二进制模式(资产可能含任意字节/无 BOM 的 UTF-8),
 *   读写失败统一转成 "asset_compile_*" 前缀的错误串回传。
 */
#include "Common/Runtime/Asset/MObjectAssetCompiler.h"

#include "Common/Runtime/Asset/MObjectAssetBinary.h"

namespace MObjectAssetCompiler {
    namespace {
        /**
         * @brief 一次性读入整个文件。
         * @param FilePath 输入路径
         * @param OutText 内容(二进制安全,按 MString 承载字节)
         * @param OutError 失败原因,可空
         * @return true 成功
         */
        bool ReadWholeFile(const MString& FilePath, MString& OutText, MString* OutError) {
            TIfstream Input(FilePath, std::ios::binary);
            if (!Input.is_open()) {
                if (OutError) {
                    *OutError = "asset_compile_open_input_failed:" + FilePath;
                }
                return false;
            }

            OutText.assign(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>());
            return true;
        }

        /**
         * @brief 截断方式写出整个文件,并检查写后流状态。
         * @param FilePath 输出路径
         * @param Bytes 待写字节;为空时只创建/截断文件
         * @param OutError 失败原因,可空
         * @return true 成功
         *
         * @detail 写后检查 good() 才能发现磁盘满/管道关闭之类的延迟错误;
         *   失败时不会删除已创建的文件。
         */
        bool WriteWholeFile(const MString& FilePath, const TByteArray& Bytes, MString* OutError) {
            TOfstream Output(FilePath, std::ios::binary | std::ios::trunc);
            if (!Output.is_open()) {
                if (OutError) {
                    *OutError = "asset_compile_open_output_failed:" + FilePath;
                }
                return false;
            }

            if (!Bytes.empty()) {
                Output.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
            }

            if (!Output.good()) {
                if (OutError) {
                    *OutError = "asset_compile_write_failed:" + FilePath;
                }
                return false;
            }

            return true;
        }
    } // namespace

    /** @brief 对象 → 字节;直接转发 MObjectAssetBinary::BuildFromObject。 */
    bool CompileBytesFromObject(const MObject* RootObject, TByteArray& OutBytes, MString* OutError) {
        return MObjectAssetBinary::BuildFromObject(RootObject, OutBytes, OutError);
    }

    /** @brief JSON 文本 → 字节;直接转发 MObjectAssetBinary::BuildFromJson。 */
    bool CompileBytesFromJson(const MString& JsonText, TByteArray& OutBytes, MString* OutError) {
        return MObjectAssetBinary::BuildFromJson(JsonText, OutBytes, OutError);
    }

    /**
     * @brief 读 JSON 文件 → 编译成 .mobj → 写出文件。
     *
     * @detail 先把字节全部编译到内存再落盘,避免编译中途失败留下半个文件
     *   (但打开输出文件失败仍会发生在读入之后)。
     */
    bool CompileFileFromJson(const MString& JsonFilePath, const MString& OutputFilePath, MString* OutError) {
        MString JsonText;
        if (!ReadWholeFile(JsonFilePath, JsonText, OutError)) {
            return false;
        }

        TByteArray Bytes;
        if (!CompileBytesFromJson(JsonText, Bytes, OutError)) {
            return false;
        }

        return WriteWholeFile(OutputFilePath, Bytes, OutError);
    }
} // namespace MObjectAssetCompiler
