#include "Common/Runtime/Asset/MObjectAssetBinary.h"
#include "Common/Runtime/Asset/MObjectAssetCompiler.h"
#include "Common/Runtime/Asset/MObjectAssetJson.h"
#include "Common/Runtime/Asset/MObjectAssetLoader.h"
#include "Common/Runtime/Object/Object.h"
#include "Servers/Scene/Combat/Monster.h"
#include "Servers/Scene/Combat/MonsterConfig.h"
#include "Servers/Scene/Combat/MonsterManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
namespace fs = std::filesystem;

bool ReadTextFile(const fs::path& FilePath, MString& OutText)
{
    TIfstream Input(FilePath, std::ios::binary);
    if (!Input.is_open())
    {
        return false;
    }

    OutText.assign(
        std::istreambuf_iterator<char>(Input),
        std::istreambuf_iterator<char>());
    return true;
}

bool WriteBinaryFile(const fs::path& FilePath, const TByteArray& Bytes)
{
    TOfstream Output(FilePath, std::ios::binary | std::ios::trunc);
    if (!Output.is_open())
    {
        return false;
    }

    if (!Bytes.empty())
    {
        Output.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
    }

    return Output.good();
}

bool WriteTextFile(const fs::path& FilePath, const MString& Text)
{
    TOfstream Output(FilePath, std::ios::binary | std::ios::trunc);
    if (!Output.is_open())
    {
        return false;
    }

    Output.write(Text.data(), static_cast<std::streamsize>(Text.size()));
    return Output.good();
}

fs::path BuildDefaultMobPath(const fs::path& InputPath)
{
    const MString Filename = InputPath.filename().string();
    if (Filename.size() > 10 && Filename.ends_with(".mobj.json"))
    {
        return InputPath.parent_path() / (Filename.substr(0, Filename.size() - 10) + ".mob");
    }

    fs::path OutputPath = InputPath;
    OutputPath.replace_extension(".mob");
    return OutputPath;
}

fs::path BuildDefaultRoundTripPath(const fs::path& InputPath)
{
    const MString Filename = InputPath.filename().string();
    if (Filename.size() > 10 && Filename.ends_with(".mobj.json"))
    {
        return InputPath.parent_path() / (Filename.substr(0, Filename.size() - 10) + ".roundtrip.json");
    }

    return InputPath.parent_path() / (InputPath.stem().string() + ".roundtrip.json");
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: MObjectAssetSmokeTool <input.mobj.json> [output.mob] [roundtrip.json] [--no-roundtrip]\n";
        return 1;
    }

    const fs::path InputPath = fs::path(argv[1]);
    size_t ArgIndex = 2;
    fs::path MobPath = BuildDefaultMobPath(InputPath);
    if (ArgIndex < static_cast<size_t>(argc) && MString(argv[ArgIndex]) != "--no-roundtrip")
    {
        MobPath = fs::path(argv[ArgIndex]);
        ++ArgIndex;
    }

    bool bEmitRoundTrip = true;
    fs::path RoundTripPath = BuildDefaultRoundTripPath(InputPath);
    for (; ArgIndex < static_cast<size_t>(argc); ++ArgIndex)
    {
        const MString Arg = argv[ArgIndex];
        if (Arg == "--no-roundtrip")
        {
            bEmitRoundTrip = false;
            continue;
        }

        RoundTripPath = fs::path(Arg);
    }

    MString JsonText;
    if (!ReadTextFile(InputPath, JsonText))
    {
        std::cerr << "read_input_failed: " << InputPath << "\n";
        return 2;
    }

    TByteArray Bytes;
    MString Error;
    if (!MObjectAssetCompiler::CompileBytesFromJson(JsonText, Bytes, &Error))
    {
        std::cerr << "compile_failed: " << Error << "\n";
        return 3;
    }

    MObjectAssetBinary::SFileHeader Header;
    size_t PayloadOffset = 0;
    if (!MObjectAssetBinary::ReadHeader(Bytes, Header, PayloadOffset, &Error))
    {
        std::cerr << "header_read_failed: " << Error << "\n";
        return 4;
    }

    if (Header.PayloadEncoding != MObjectAssetBinary::EPayloadEncoding::TaggedFields)
    {
        std::cerr << "unexpected_payload_encoding: " << static_cast<uint32>(Header.PayloadEncoding) << "\n";
        return 5;
    }

    if (!WriteBinaryFile(MobPath, Bytes))
    {
        std::cerr << "write_mob_failed: " << MobPath << "\n";
        return 6;
    }

    MMonsterManager* Manager = NewMObject<MMonsterManager>(nullptr, "AssetSmokeManager");
    MObject* LoadedRoot = MObjectAssetLoader::LoadFromBytes(Bytes, Manager, &Error);
    if (!LoadedRoot)
    {
        std::cerr << "load_failed: " << Error << "\n";
        DestroyMObject(Manager);
        return 7;
    }

    if (bEmitRoundTrip)
    {
        MString RoundTripJson;
        if (!MObjectAssetJson::ExportAssetObjectToJson(LoadedRoot, RoundTripJson, &Error))
        {
            std::cerr << "roundtrip_export_failed: " << Error << "\n";
            DestroyMObject(Manager);
            return 8;
        }

        if (!WriteTextFile(RoundTripPath, RoundTripJson))
        {
            std::cerr << "write_roundtrip_failed: " << RoundTripPath << "\n";
            DestroyMObject(Manager);
            return 9;
        }
    }

    if (LoadedRoot->GetClass() == MMonsterConfig::StaticClass())
    {
        auto* Config = static_cast<MMonsterConfig*>(LoadedRoot);
        if (!Manager->RegisterMonsterConfig(Config, Error))
        {
            std::cerr << "register_config_failed: " << Error << "\n";
            DestroyMObject(Manager);
            return 10;
        }

        FCombatUnitRef Unit;
        if (!Manager->SpawnMonster(9001, *Config, Unit, Error))
        {
            std::cerr << "spawn_from_config_failed: " << Error << "\n";
            DestroyMObject(Manager);
            return 11;
        }

        MMonster* Monster = Manager->FindMonster(Unit);
        if (!Monster)
        {
            std::cerr << "spawned_monster_not_found\n";
            DestroyMObject(Manager);
            return 12;
        }

        std::cout
            << "spawn_ok"
            << " class=" << LoadedRoot->GetClass()->GetName()
            << " template_id=" << Monster->GetMonsterTemplateId()
            << " scene_id=" << Monster->GetSceneId()
            << " combat_entity_id=" << Monster->GetCombatEntityId()
            << " debug_name=" << Monster->GetDebugName()
            << "\n";
    }
    else
    {
        std::cout
            << "load_ok"
            << " class=" << LoadedRoot->GetClass()->GetName()
            << "\n";
    }

    std::cout
        << "mob_path=" << MobPath
        << " roundtrip_path=" << (bEmitRoundTrip ? RoundTripPath.string() : "<disabled>")
        << " payload_bytes=" << Header.PayloadSize
        << "\n";

    DestroyMObject(Manager);
    return 0;
}
