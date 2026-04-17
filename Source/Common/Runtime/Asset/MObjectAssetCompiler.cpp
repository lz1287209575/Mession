#include "Common/Runtime/Asset/MObjectAssetCompiler.h"

#include "Common/Runtime/Asset/MObjectAssetBinary.h"

namespace MObjectAssetCompiler
{
namespace
{
bool ReadWholeFile(const MString& FilePath, MString& OutText, MString* OutError)
{
    TIfstream Input(FilePath, std::ios::binary);
    if (!Input.is_open())
    {
        if (OutError)
        {
            *OutError = "asset_compile_open_input_failed:" + FilePath;
        }
        return false;
    }

    OutText.assign(
        std::istreambuf_iterator<char>(Input),
        std::istreambuf_iterator<char>());
    return true;
}

bool WriteWholeFile(const MString& FilePath, const TByteArray& Bytes, MString* OutError)
{
    TOfstream Output(FilePath, std::ios::binary | std::ios::trunc);
    if (!Output.is_open())
    {
        if (OutError)
        {
            *OutError = "asset_compile_open_output_failed:" + FilePath;
        }
        return false;
    }

    if (!Bytes.empty())
    {
        Output.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
    }

    if (!Output.good())
    {
        if (OutError)
        {
            *OutError = "asset_compile_write_failed:" + FilePath;
        }
        return false;
    }

    return true;
}
}

bool CompileBytesFromObject(const MObject* RootObject, TByteArray& OutBytes, MString* OutError)
{
    return MObjectAssetBinary::BuildFromObject(RootObject, OutBytes, OutError);
}

bool CompileBytesFromJson(const MString& JsonText, TByteArray& OutBytes, MString* OutError)
{
    return MObjectAssetBinary::BuildFromJson(JsonText, OutBytes, OutError);
}

bool CompileFileFromJson(const MString& JsonFilePath, const MString& OutputFilePath, MString* OutError)
{
    MString JsonText;
    if (!ReadWholeFile(JsonFilePath, JsonText, OutError))
    {
        return false;
    }

    TByteArray Bytes;
    if (!CompileBytesFromJson(JsonText, Bytes, OutError))
    {
        return false;
    }

    return WriteWholeFile(OutputFilePath, Bytes, OutError);
}
}
