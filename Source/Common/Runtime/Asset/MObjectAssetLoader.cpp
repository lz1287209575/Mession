#include "Common/Runtime/Asset/MObjectAssetLoader.h"

#include "Common/Runtime/Asset/MObjectAssetBinary.h"

namespace MObjectAssetLoader
{
namespace
{
bool ReadWholeFileBytes(const MString& FilePath, TByteArray& OutBytes, MString* OutError)
{
    TIfstream Input(FilePath, std::ios::binary);
    if (!Input.is_open())
    {
        if (OutError)
        {
            *OutError = "asset_load_open_failed:" + FilePath;
        }
        return false;
    }

    Input.seekg(0, std::ios::end);
    const std::streamoff Size = Input.tellg();
    if (Size < 0)
    {
        if (OutError)
        {
            *OutError = "asset_load_tellg_failed:" + FilePath;
        }
        return false;
    }
    Input.seekg(0, std::ios::beg);

    OutBytes.resize(static_cast<size_t>(Size));
    if (Size > 0)
    {
        Input.read(reinterpret_cast<char*>(OutBytes.data()), Size);
    }

    if (!Input.good() && !Input.eof())
    {
        if (OutError)
        {
            *OutError = "asset_load_read_failed:" + FilePath;
        }
        return false;
    }

    return true;
}
}

MObject* LoadFromBytes(const TByteArray& Bytes, MObject* Outer, MString* OutError)
{
    return MObjectAssetBinary::LoadFromBytes(Bytes, Outer, OutError);
}

MObject* LoadFromFile(const MString& FilePath, MObject* Outer, MString* OutError)
{
    TByteArray Bytes;
    if (!ReadWholeFileBytes(FilePath, Bytes, OutError))
    {
        return nullptr;
    }

    return LoadFromBytes(Bytes, Outer, OutError);
}
}
