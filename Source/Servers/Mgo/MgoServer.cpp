#include "MgoServer.h"
#include "Build/Generated/MWorldService.mgenerated.h"

#include "Common/Config.h"
#include "Core/Concurrency/Promise.h"
#include "Core/Concurrency/ThreadPool.h"
#include "Core/Json.h"
#include "Gameplay/InventoryMember.h"

#ifdef MESSION_USE_MONGOCXX
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/options/replace.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/uri.hpp>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#endif

namespace
{
const TMap<FString, const char*> MgoEnvMap = {
    {"port", "MESSION_MGO_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
    {"debug_http_port", "MESSION_MGO_DEBUG_HTTP_PORT"},
    {"mongo_enable", "MESSION_MGO_MONGO_ENABLE"},
    {"mongo_uri", "MESSION_MGO_MONGO_URI"},
    {"mongo_db", "MESSION_MGO_MONGO_DB"},
    {"mongo_collection", "MESSION_MGO_MONGO_COLLECTION"},
    {"mongo_db_workers", "MESSION_MGO_DB_WORKERS"},
};

#ifdef MESSION_USE_MONGOCXX
struct SMongoRuntime
{
    std::unique_ptr<mongocxx::instance> Instance;
    std::unique_ptr<mongocxx::pool> Pool;
    FString MongoUri;
    std::mutex Mutex;
};

SMongoRuntime& GetMongoRuntime()
{
    static SMongoRuntime Runtime;
    return Runtime;
}

bool EnsureMongoClientReady(const SMgoConfig& Config)
{
    SMongoRuntime& Runtime = GetMongoRuntime();
    std::lock_guard<std::mutex> Lock(Runtime.Mutex);
    try
    {
        if (!Runtime.Instance)
        {
            Runtime.Instance = std::make_unique<mongocxx::instance>();
        }
        if (!Runtime.Pool || Runtime.MongoUri != Config.MongoUri)
        {
            Runtime.Pool = std::make_unique<mongocxx::pool>(mongocxx::uri{Config.MongoUri});
            Runtime.MongoUri = Config.MongoUri;
        }
        return true;
    }
    catch (const std::exception& Ex)
    {
        LOG_ERROR("Mongo init failed: %s", Ex.what());
        Runtime.Pool.reset();
        Runtime.MongoUri.clear();
        return false;
    }
}

bool TryDecodeHex(const FString& InHex, TArray& OutBytes)
{
    OutBytes.clear();
    if (InHex.empty())
    {
        return true;
    }
    if ((InHex.size() % 2) != 0)
    {
        return false;
    }

    auto HexNibble = [](char Ch) -> int32
    {
        if (Ch >= '0' && Ch <= '9')
        {
            return static_cast<int32>(Ch - '0');
        }
        if (Ch >= 'A' && Ch <= 'F')
        {
            return 10 + static_cast<int32>(Ch - 'A');
        }
        if (Ch >= 'a' && Ch <= 'f')
        {
            return 10 + static_cast<int32>(Ch - 'a');
        }
        return -1;
    };

    OutBytes.reserve(InHex.size() / 2);
    for (size_t Index = 0; Index < InHex.size(); Index += 2)
    {
        const int32 Hi = HexNibble(InHex[Index]);
        const int32 Lo = HexNibble(InHex[Index + 1]);
        if (Hi < 0 || Lo < 0)
        {
            OutBytes.clear();
            return false;
        }
        OutBytes.push_back(static_cast<uint8>((Hi << 4) | Lo));
    }
    return true;
}

template<typename TSubDocument>
void AppendVectorField(TSubDocument& InDoc, const char* InKey, const SVector& InValue)
{
    using bsoncxx::builder::basic::kvp;
    const FString Key = InKey ? InKey : "";
    InDoc.append(kvp(Key, [&](bsoncxx::builder::basic::sub_document InSubDoc)
    {
        InSubDoc.append(kvp("x", InValue.X));
        InSubDoc.append(kvp("y", InValue.Y));
        InSubDoc.append(kvp("z", InValue.Z));
    }));
}

template<typename TSubDocument>
void AppendRotatorField(TSubDocument& InDoc, const char* InKey, const SRotator& InValue)
{
    using bsoncxx::builder::basic::kvp;
    const FString Key = InKey ? InKey : "";
    InDoc.append(kvp(Key, [&](bsoncxx::builder::basic::sub_document InSubDoc)
    {
        InSubDoc.append(kvp("pitch", InValue.Pitch));
        InSubDoc.append(kvp("yaw", InValue.Yaw));
        InSubDoc.append(kvp("roll", InValue.Roll));
    }));
}

template<typename TSubDocument>
int32 AppendPersistenceFields(TSubDocument& InDoc, const MClass* InClassMeta, const void* InObject);

template<typename TSubDocument>
void AppendPropertyField(TSubDocument& InDoc, const MProperty* InProp, const void* InObject)
{
    using bsoncxx::builder::basic::kvp;
    if (!InProp || !InObject)
    {
        return;
    }

    const FString Key = InProp->Name;
    switch (InProp->Type)
    {
    case EPropertyType::Int8:
        InDoc.append(kvp(Key, static_cast<int32>(*InProp->GetValuePtr<int8>(InObject))));
        return;
    case EPropertyType::Int16:
        InDoc.append(kvp(Key, static_cast<int32>(*InProp->GetValuePtr<int16>(InObject))));
        return;
    case EPropertyType::Int32:
        InDoc.append(kvp(Key, *InProp->GetValuePtr<int32>(InObject)));
        return;
    case EPropertyType::Int64:
        InDoc.append(kvp(Key, *InProp->GetValuePtr<int64>(InObject)));
        return;
    case EPropertyType::UInt8:
        InDoc.append(kvp(Key, static_cast<int32>(*InProp->GetValuePtr<uint8>(InObject))));
        return;
    case EPropertyType::UInt16:
        InDoc.append(kvp(Key, static_cast<int32>(*InProp->GetValuePtr<uint16>(InObject))));
        return;
    case EPropertyType::UInt32:
        InDoc.append(kvp(Key, static_cast<int64>(*InProp->GetValuePtr<uint32>(InObject))));
        return;
    case EPropertyType::UInt64:
    {
        const uint64 Value = *InProp->GetValuePtr<uint64>(InObject);
        if (Value <= static_cast<uint64>(std::numeric_limits<int64>::max()))
        {
            InDoc.append(kvp(Key, static_cast<int64>(Value)));
        }
        else
        {
            InDoc.append(kvp(Key, MString::ToString(Value)));
        }
        return;
    }
    case EPropertyType::Float:
        InDoc.append(kvp(Key, static_cast<double>(*InProp->GetValuePtr<float>(InObject))));
        return;
    case EPropertyType::Double:
        InDoc.append(kvp(Key, *InProp->GetValuePtr<double>(InObject)));
        return;
    case EPropertyType::Bool:
        InDoc.append(kvp(Key, *InProp->GetValuePtr<bool>(InObject)));
        return;
    case EPropertyType::String:
        InDoc.append(kvp(Key, *InProp->GetValuePtr<FString>(InObject)));
        return;
    case EPropertyType::Vector:
        AppendVectorField(InDoc, Key.c_str(), *InProp->GetValuePtr<SVector>(InObject));
        return;
    case EPropertyType::Rotator:
        AppendRotatorField(InDoc, Key.c_str(), *InProp->GetValuePtr<SRotator>(InObject));
        return;
    case EPropertyType::Struct:
    {
        const void* StructPtr = InProp->GetValueVoidPtr(InObject);
        if (!StructPtr)
        {
            InDoc.append(kvp(Key, "<null-struct>"));
            return;
        }
        if (MClass* StructMeta = MReflectObject::FindStruct(InProp->CppTypeIndex))
        {
            InDoc.append(kvp(Key, [&](bsoncxx::builder::basic::sub_document InSubDoc)
            {
                AppendPersistenceFields(InSubDoc, StructMeta, StructPtr);
            }));
            return;
        }
        InDoc.append(kvp(Key, InProp->ExportValueToString(InObject)));
        return;
    }
    case EPropertyType::Array:
        if (InProp->CppTypeIndex == std::type_index(typeid(TVector<SInventoryItem>)))
        {
            const auto* Items = InProp->GetValuePtr<TVector<SInventoryItem>>(InObject);
            if (!Items)
            {
                InDoc.append(kvp(Key, bsoncxx::types::b_null{}));
                return;
            }
            InDoc.append(kvp(Key, [&](bsoncxx::builder::basic::sub_array InArray)
            {
                for (const SInventoryItem& Item : *Items)
                {
                    InArray.append([&](bsoncxx::builder::basic::sub_document InElem)
                    {
                        InElem.append(kvp("instance_id", static_cast<int64_t>(Item.InstanceId)));
                        InElem.append(kvp("item_id", static_cast<int64_t>(Item.ItemId)));
                        InElem.append(kvp("count", static_cast<int64_t>(Item.Count)));
                        InElem.append(kvp("bound", Item.bBound));
                        InElem.append(kvp("expire_at", static_cast<int64_t>(Item.ExpireAtUnixSeconds)));
                        InElem.append(kvp("flags", static_cast<int64_t>(Item.Flags)));
                    });
                }
            }));
            return;
        }
        InDoc.append(kvp(Key, InProp->ExportValueToString(InObject)));
        return;
    case EPropertyType::Enum:
    {
        const void* Raw = InProp->GetValueVoidPtr(InObject);
        if (!Raw)
        {
            InDoc.append(kvp(Key, "<null>"));
            return;
        }

        int64 EnumValue = 0;
        switch (InProp->Size)
        {
        case 1: EnumValue = static_cast<int64>(*reinterpret_cast<const uint8*>(Raw)); break;
        case 2: EnumValue = static_cast<int64>(*reinterpret_cast<const uint16*>(Raw)); break;
        case 4: EnumValue = static_cast<int64>(*reinterpret_cast<const uint32*>(Raw)); break;
        case 8: EnumValue = static_cast<int64>(*reinterpret_cast<const uint64*>(Raw)); break;
        default:
            InDoc.append(kvp(Key, InProp->ExportValueToString(InObject)));
            return;
        }

        InDoc.append(kvp(Key, EnumValue));
        return;
    }
    default:
        InDoc.append(kvp(Key, InProp->ExportValueToString(InObject)));
        return;
    }
}

template<typename TSubDocument>
int32 AppendPersistenceFields(TSubDocument& InDoc, const MClass* InClassMeta, const void* InObject)
{
    if (!InClassMeta || !InObject)
    {
        return 0;
    }

    int32 Count = 0;
    if (const MClass* Parent = InClassMeta->GetParentClass())
    {
        Count += AppendPersistenceFields(InDoc, Parent, InObject);
    }

    for (const MProperty* Prop : InClassMeta->GetProperties())
    {
        if (!Prop || !Prop->HasAnyDomains(EPropertyDomainFlags::Persistence))
        {
            continue;
        }
        AppendPropertyField(InDoc, Prop, InObject);
        ++Count;
    }
    return Count;
}

bool TryGetUInt64Field(bsoncxx::document::view View, const char* Key, uint64& OutValue)
{
    auto Elem = View[Key];
    if (!Elem)
    {
        return false;
    }

    if (Elem.type() == bsoncxx::type::k_int64)
    {
        const int64 Raw = Elem.get_int64().value;
        if (Raw < 0)
        {
            return false;
        }
        OutValue = static_cast<uint64>(Raw);
        return true;
    }
    if (Elem.type() == bsoncxx::type::k_int32)
    {
        const int32 Raw = Elem.get_int32().value;
        if (Raw < 0)
        {
            return false;
        }
        OutValue = static_cast<uint64>(Raw);
        return true;
    }
    return false;
}

bool TryGetUInt32Field(bsoncxx::document::view View, const char* Key, uint32& OutValue)
{
    uint64 Temp = 0;
    if (!TryGetUInt64Field(View, Key, Temp))
    {
        return false;
    }
    OutValue = static_cast<uint32>(Temp);
    return true;
}

bool PersistSnapshotToMongo(
    const SMgoConfig& Config,
    uint64 ObjectId,
    uint16 ClassId,
    uint32 OwnerWorldId,
    uint64 RequestId,
    uint64 Version,
    const FString& ClassName,
    const FString& SnapshotHex)
{
    if (!EnsureMongoClientReady(Config))
    {
        return false;
    }

    SMongoRuntime& Runtime = GetMongoRuntime();
    auto Client = Runtime.Pool->acquire();
    auto Database = Client->database(Config.MongoDatabase);
    auto Collection = Database.collection(Config.MongoCollection);

    using bsoncxx::builder::basic::document;
    using bsoncxx::builder::basic::kvp;

    const int32 SnapshotSize = static_cast<int32>(SnapshotHex.size() / 2);

    TArray SnapshotBytes;
    const bool bHexDecoded = TryDecodeHex(SnapshotHex, SnapshotBytes);
    MClass* ClassMeta = MReflectObject::FindClass(ClassName);
    if (!ClassMeta && ClassId != 0)
    {
        ClassMeta = MReflectObject::FindClass(ClassId);
    }
    if (!bHexDecoded)
    {
        LOG_WARN("Mgo decode failed: invalid hex payload object=%llu class=%s", static_cast<unsigned long long>(ObjectId), ClassName.c_str());
    }
    if (!ClassMeta)
    {
        LOG_WARN("Mgo decode failed: class meta not found object=%llu class_id=%u class=%s",
                 static_cast<unsigned long long>(ObjectId),
                 static_cast<unsigned>(ClassId),
                 ClassName.c_str());
    }

    std::unique_ptr<MReflectObject> DecodedObject;
    if (bHexDecoded && ClassMeta)
    {
        void* InstanceRaw = ClassMeta->CreateInstance();
        if (InstanceRaw)
        {
            DecodedObject.reset(static_cast<MReflectObject*>(InstanceRaw));
            ClassMeta->ReadSnapshotByDomain(
                DecodedObject.get(),
                SnapshotBytes,
                ToMask(EPropertyDomainFlags::Persistence));
        }
        else
        {
            LOG_WARN("Mgo decode failed: could not create reflected instance class=%s", ClassName.c_str());
        }
    }

    uint64 OwnerPlayerId = 0;
    bool bHasOwnerPlayerId = false;
    if (DecodedObject && ClassMeta)
    {
        if (MProperty* OwnerProp = ClassMeta->FindProperty("OwnerPlayerId"))
        {
            switch (OwnerProp->Type)
            {
            case EPropertyType::UInt64:
                OwnerPlayerId = *OwnerProp->GetValuePtr<uint64>(DecodedObject.get());
                bHasOwnerPlayerId = true;
                break;
            case EPropertyType::UInt32:
                OwnerPlayerId = static_cast<uint64>(*OwnerProp->GetValuePtr<uint32>(DecodedObject.get()));
                bHasOwnerPlayerId = true;
                break;
            case EPropertyType::Int64:
            {
                const int64 Raw = *OwnerProp->GetValuePtr<int64>(DecodedObject.get());
                if (Raw >= 0)
                {
                    OwnerPlayerId = static_cast<uint64>(Raw);
                    bHasOwnerPlayerId = true;
                }
                break;
            }
            case EPropertyType::Int32:
            {
                const int32 Raw = *OwnerProp->GetValuePtr<int32>(DecodedObject.get());
                if (Raw >= 0)
                {
                    OwnerPlayerId = static_cast<uint64>(Raw);
                    bHasOwnerPlayerId = true;
                }
                break;
            }
            default:
                break;
            }
        }
    }

    const bool bUseOwnerScopedKey = bHasOwnerPlayerId && ClassName != "MPlayerAvatar";

    document FilterBuilder;
    if (bUseOwnerScopedKey)
    {
        FilterBuilder.append(kvp("owner_player_id", static_cast<int64_t>(OwnerPlayerId)));
        FilterBuilder.append(kvp("class_name", ClassName));
    }
    else
    {
        FilterBuilder.append(kvp("object_id", static_cast<int64_t>(ObjectId)));
    }

    auto ExistingResult = Collection.find_one(FilterBuilder.view());
    if (ExistingResult)
    {
        const bsoncxx::document::view ExistingView = ExistingResult->view();
        uint32 ExistingOwner = 0;
        if (TryGetUInt32Field(ExistingView, "owner_world_id", ExistingOwner) &&
            ExistingOwner != OwnerWorldId)
        {
            LOG_WARN("Mgo persist rejected by owner fence: object=%llu owner=%u existing_owner=%u req=%llu ver=%llu",
                     static_cast<unsigned long long>(ObjectId),
                     static_cast<unsigned>(OwnerWorldId),
                     static_cast<unsigned>(ExistingOwner),
                     static_cast<unsigned long long>(RequestId),
                     static_cast<unsigned long long>(Version));
            return false;
        }

        uint64 ExistingVersion = 0;
        const bool bHasVersion = TryGetUInt64Field(ExistingView, "version", ExistingVersion);
        if (bHasVersion)
        {
            if (ExistingVersion > Version)
            {
                LOG_DEBUG("Mgo persist stale version ignored: object=%llu req=%llu ver=%llu existing_ver=%llu",
                          static_cast<unsigned long long>(ObjectId),
                          static_cast<unsigned long long>(RequestId),
                          static_cast<unsigned long long>(Version),
                          static_cast<unsigned long long>(ExistingVersion));
                return true;
            }

            if (ExistingVersion == Version)
            {
                uint64 ExistingRequestId = 0;
                if (TryGetUInt64Field(ExistingView, "last_request_id", ExistingRequestId) &&
                    ExistingRequestId == RequestId)
                {
                    return true;
                }

                LOG_WARN("Mgo persist conflicting same-version write: object=%llu req=%llu ver=%llu existing_req=%llu",
                         static_cast<unsigned long long>(ObjectId),
                         static_cast<unsigned long long>(RequestId),
                         static_cast<unsigned long long>(Version),
                         static_cast<unsigned long long>(ExistingRequestId));
                return false;
            }
        }
    }

    document ReplacementBuilder;
    ReplacementBuilder.append(kvp("object_id", static_cast<int64_t>(ObjectId)));
    ReplacementBuilder.append(kvp("class_id", static_cast<int32_t>(ClassId)));
    ReplacementBuilder.append(kvp("owner_world_id", static_cast<int32_t>(OwnerWorldId)));
    ReplacementBuilder.append(kvp("last_request_id", static_cast<int64_t>(RequestId)));
    ReplacementBuilder.append(kvp("version", static_cast<int64_t>(Version)));
    ReplacementBuilder.append(kvp("class_name", ClassName));
    if (bHasOwnerPlayerId)
    {
        ReplacementBuilder.append(kvp("owner_player_id", static_cast<int64_t>(OwnerPlayerId)));
    }
    ReplacementBuilder.append(kvp("key_scope", bUseOwnerScopedKey ? "owner_class" : "object_id"));
    ReplacementBuilder.append(kvp("schema_version", 2));
    ReplacementBuilder.append(kvp("updated_at", bsoncxx::types::b_date{std::chrono::system_clock::now()}));
    ReplacementBuilder.append(kvp("raw", [&](bsoncxx::builder::basic::sub_document InRaw)
    {
        InRaw.append(kvp("snapshot_hex", SnapshotHex));
        InRaw.append(kvp("snapshot_size", SnapshotSize));
    }));

    int32 PersistFieldCount = 0;
    ReplacementBuilder.append(kvp("fields", [&](bsoncxx::builder::basic::sub_document InFields)
    {
        if (DecodedObject && ClassMeta)
        {
            PersistFieldCount = AppendPersistenceFields(InFields, ClassMeta, DecodedObject.get());
        }
    }));
    ReplacementBuilder.append(kvp("field_count", PersistFieldCount));
    ReplacementBuilder.append(kvp("decode_ok", bHexDecoded && DecodedObject != nullptr));

    mongocxx::options::replace ReplaceOptions;
    ReplaceOptions.upsert(true);

    try
    {
        Collection.replace_one(FilterBuilder.view(), ReplacementBuilder.view(), ReplaceOptions);
        return true;
    }
    catch (const std::exception& Ex)
    {
        LOG_ERROR("Mongo replace_one failed: object=%llu class=%s error=%s",
                  static_cast<unsigned long long>(ObjectId),
                  ClassName.c_str(),
                  Ex.what());
        return false;
    }
}

bool LoadSnapshotFromMongo(
    const SMgoConfig& Config,
    uint64 ObjectId,
    bool& bOutFound,
    uint16& OutClassId,
    FString& OutClassName,
    FString& OutSnapshotHex)
{
    bOutFound = false;
    OutClassId = 0;
    OutClassName.clear();
    OutSnapshotHex.clear();

    if (!EnsureMongoClientReady(Config))
    {
        return false;
    }

    SMongoRuntime& Runtime = GetMongoRuntime();
    auto Client = Runtime.Pool->acquire();
    auto Database = Client->database(Config.MongoDatabase);
    auto Collection = Database.collection(Config.MongoCollection);

    using bsoncxx::builder::basic::document;
    using bsoncxx::builder::basic::kvp;

    document FilterBuilder;
    FilterBuilder.append(kvp("object_id", static_cast<int64_t>(ObjectId)));

    try
    {
        auto Result = Collection.find_one(FilterBuilder.view());
        if (!Result)
        {
            return true;
        }

        const auto View = Result->view();
        auto ClassIdElem = View["class_id"];
        auto ClassNameElem = View["class_name"];
        auto RawElem = View["raw"];

        if (ClassIdElem && ClassIdElem.type() == bsoncxx::type::k_int32)
        {
            OutClassId = static_cast<uint16>(ClassIdElem.get_int32().value);
        }

        if (ClassNameElem && ClassNameElem.type() == bsoncxx::type::k_utf8)
        {
            OutClassName = ClassNameElem.get_utf8().value.to_string();
        }

        if (RawElem && RawElem.type() == bsoncxx::type::k_document)
        {
            auto RawView = RawElem.get_document().view();
            auto SnapshotHexElem = RawView["snapshot_hex"];
            if (SnapshotHexElem && SnapshotHexElem.type() == bsoncxx::type::k_utf8)
            {
                OutSnapshotHex = SnapshotHexElem.get_utf8().value.to_string();
            }
        }
        else
        {
            auto LegacySnapshotHexElem = View["snapshot_hex"];
            if (LegacySnapshotHexElem && LegacySnapshotHexElem.type() == bsoncxx::type::k_utf8)
            {
                OutSnapshotHex = LegacySnapshotHexElem.get_utf8().value.to_string();
            }
        }

        bOutFound = !OutSnapshotHex.empty();
        return true;
    }
    catch (const std::exception& Ex)
    {
        LOG_ERROR("Mongo find_one failed: object=%llu error=%s",
                  static_cast<unsigned long long>(ObjectId),
                  Ex.what());
        return false;
    }
}
#endif
}

bool MMgoServer::LoadConfig(const FString& ConfigPath)
{
    TMap<FString, FString> Vars;
    if (!ConfigPath.empty())
    {
        MConfig::LoadFromFile(ConfigPath, Vars);
    }
    MConfig::ApplyEnvOverrides(Vars, MgoEnvMap);
    Config.ListenPort = MConfig::GetU16(Vars, "port", Config.ListenPort);
    Config.RouterServerAddr = MConfig::GetStr(Vars, "router_addr", Config.RouterServerAddr);
    Config.RouterServerPort = MConfig::GetU16(Vars, "router_port", Config.RouterServerPort);
    Config.ServerName = MConfig::GetStr(Vars, "server_name", Config.ServerName);
    Config.ZoneId = MConfig::GetU16(Vars, "zone_id", Config.ZoneId);
    Config.DebugHttpPort = MConfig::GetU16(Vars, "debug_http_port", Config.DebugHttpPort);
    Config.EnableMongoStorage = MConfig::GetBool(Vars, "mongo_enable", Config.EnableMongoStorage);
    Config.MongoUri = MConfig::GetStr(Vars, "mongo_uri", Config.MongoUri);
    Config.MongoDatabase = MConfig::GetStr(Vars, "mongo_db", Config.MongoDatabase);
    Config.MongoCollection = MConfig::GetStr(Vars, "mongo_collection", Config.MongoCollection);
    Config.MongoDbWorkers = MConfig::GetU32(Vars, "mongo_db_workers", Config.MongoDbWorkers);
    if (Config.MongoDbWorkers == 0)
    {
        Config.MongoDbWorkers = 1;
    }
    return true;
}

bool MMgoServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    MServerConnection::SetLocalInfo(6, EServerType::Mgo, Config.ServerName);

    bRunning = true;
    MLogger::LogStartupBanner("MgoServer", Config.ListenPort, 0);

    InitBackendMessageHandlers();
    InitRouterMessageHandlers();
    MMgoService::BindHandlers(this);

    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = MakeShared<MServerConnection>(RouterConfig);
    RouterServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("Router server authenticated: %s", Info.ServerName.c_str());
        SendRouterRegister();
    });
    RouterServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleRouterServerMessage(Type, Data);
    });
    RouterServerConn->Connect();

    if (Config.EnableMongoStorage)
    {
#ifdef MESSION_USE_MONGOCXX
        DbThreadPool = TUniquePtr<MThreadPool>(new MThreadPool(Config.MongoDbWorkers));
        const bool bReady = EnsureMongoClientReady(Config);
        LOG_INFO("Mgo Mongo storage requested: enabled=%d uri=%s db=%s coll=%s workers=%u ready=%d",
                 1,
                 Config.MongoUri.c_str(),
                 Config.MongoDatabase.c_str(),
                 Config.MongoCollection.c_str(),
                 static_cast<unsigned>(Config.MongoDbWorkers),
                 bReady ? 1 : 0);
        LOG_INFO("Mgo Mongo async worker pool started: workers=%llu",
                 static_cast<unsigned long long>(DbThreadPool ? DbThreadPool->GetNumThreads() : 0));
#else
        LOG_WARN("Mgo Mongo storage requested but mongocxx is not enabled at build time");
#endif
    }

    if (Config.DebugHttpPort > 0)
    {
        DebugServer = TUniquePtr<MHttpDebugServer>(new MHttpDebugServer(
            Config.DebugHttpPort,
            [this]() { return BuildDebugStatusJson(); }));
        if (!DebugServer->Start())
        {
            LOG_ERROR("Mgo debug HTTP failed to start on port %u", static_cast<unsigned>(Config.DebugHttpPort));
            DebugServer.reset();
        }
    }

    return true;
}

uint16 MMgoServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MMgoServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    if (!Conn)
    {
        return;
    }

    Conn->SetNonBlocking(true);
    BackendConnections[ConnId] = SMgoPeer{Conn, false, 0, EServerType::Unknown, ""};
    LOG_INFO("New backend connected (connection_id=%llu)", static_cast<unsigned long long>(ConnId));

    EventLoop.RegisterConnection(ConnId, Conn,
        [this](uint64 Id, const TArray& Payload)
        {
            HandleBackendPacket(Id, Payload);
        },
        [this](uint64 Id)
        {
            LOG_INFO("Backend disconnected: %llu", static_cast<unsigned long long>(Id));
            BackendConnections.erase(Id);
        });
}

void MMgoServer::ShutdownConnections()
{
    if (DbThreadPool)
    {
        DbThreadPool->Shutdown();
        DbThreadPool.reset();
    }
    for (auto& [Id, Peer] : BackendConnections)
    {
        (void)Id;
        if (Peer.Connection)
        {
            Peer.Connection->Close();
        }
    }
    BackendConnections.clear();
    if (RouterServerConn)
    {
        RouterServerConn->Disconnect();
        RouterServerConn.reset();
    }
    if (DebugServer)
    {
        DebugServer->Stop();
        DebugServer.reset();
    }
    LOG_INFO("Mgo server shutdown complete");
}

void MMgoServer::OnRunStarted()
{
    LOG_INFO("Mgo server running...");
}

void MMgoServer::Tick()
{
    if (!bRunning)
    {
        return;
    }
    TickBackends();
}

void MMgoServer::TickBackends()
{
    if (RouterServerConn)
    {
        RouterServerConn->Tick(DEFAULT_TICK_RATE);
    }
}

FString MMgoServer::BuildDebugStatusJson() const
{
    MJsonWriter W = MJsonWriter::Object();
    W.Key("server"); W.Value("Mgo");
    W.Key("backendConnections"); W.Value(static_cast<uint64>(BackendConnections.size()));
    W.Key("persistRequests"); W.Value(PersistRequestCount);
    W.Key("persistBytes"); W.Value(PersistBytesTotal);
    W.Key("mongoEnabled"); W.Value(Config.EnableMongoStorage);
    W.Key("mongoDbWorkers"); W.Value(static_cast<uint64>(Config.MongoDbWorkers));
    W.Key("mongoSuccess"); W.Value(PersistMongoSuccess);
    W.Key("mongoFailed"); W.Value(PersistMongoFailed);
    W.Key("mongoLoadRequests"); W.Value(LoadRequestCount);
    W.Key("mongoLoadSuccess"); W.Value(LoadMongoSuccess);
    W.Key("mongoLoadFailed"); W.Value(LoadMongoFailed);
    W.EndObject();
    return W.ToString();
}

void MMgoServer::HandleBackendPacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
    {
        return;
    }

    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());

    if (MsgType == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!PeerIt->second.bAuthenticated)
        {
            LOG_WARN("Rejecting MT_RPC from unauthenticated backend connection %llu",
                     static_cast<unsigned long long>(ConnectionId));
            return;
        }

        if (!TryInvokeServerRpc(&MgoService, Payload, ERpcType::ServerToServer))
        {
            LOG_WARN("MgoServer MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    BackendMessageDispatcher.Dispatch(ConnectionId, MsgType, Payload);
}

bool MMgoServer::SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload)
{
    auto It = BackendConnections.find(ConnectionId);
    if (It == BackendConnections.end() || !It->second.Connection)
    {
        return false;
    }

    TArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(Type);
    Packet.insert(Packet.end(), Payload.begin(), Payload.end());
    return It->second.Connection->Send(Packet.data(), Packet.size());
}

void MMgoServer::HandleRouterServerMessage(uint8 Type, const TArray& Data)
{
    if (Type == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(this, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("MgoServer router MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    RouterMessageDispatcher.Dispatch(Type, Data);
}

void MMgoServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_ServerRegister,
        SServerRegisterMessage{6, EServerType::Mgo, Config.ServerName, "127.0.0.1", Config.ListenPort, Config.ZoneId});
}

void MMgoServer::InitBackendMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_ServerHandshake,
        &MMgoServer::OnBackend_ServerHandshake,
        "MT_ServerHandshake");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_Heartbeat,
        &MMgoServer::OnBackend_Heartbeat,
        "MT_Heartbeat");
}

void MMgoServer::InitRouterMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        RouterMessageDispatcher,
        EServerMessageType::MT_ServerRegisterAck,
        &MMgoServer::OnRouter_ServerRegisterAck,
        "MT_ServerRegisterAck");
}

void MMgoServer::OnBackend_ServerHandshake(uint64 ConnectionId, const SServerHandshakeMessage& Message)
{
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    SMgoPeer& Peer = PeerIt->second;
    Peer.ServerId = Message.ServerId;
    Peer.ServerType = Message.ServerType;
    Peer.ServerName = Message.ServerName;
    Peer.bAuthenticated = true;

    SendServerMessage(ConnectionId, EServerMessageType::MT_ServerHandshakeAck, SEmptyServerMessage{});
    LOG_INFO("%s authenticated as %d", Peer.ServerName.c_str(), static_cast<int>(Peer.ServerType));
}

void MMgoServer::OnBackend_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage&)
{
    SendServerMessage(ConnectionId, EServerMessageType::MT_HeartbeatAck, SEmptyServerMessage{});
}

void MMgoServer::OnRouter_ServerRegisterAck(const SServerRegisterAckMessage&)
{
    LOG_INFO("Mgo server registered to RouterServer");
}

void MMgoServer::Rpc_OnRouterServerRegisterAck(uint8 Result)
{
    OnRouter_ServerRegisterAck(SServerRegisterAckMessage{Result});
}

void MMgoServer::Rpc_OnPersistSnapshot(
    uint64 ObjectId,
    uint16 ClassId,
    uint32 OwnerWorldId,
    uint64 RequestId,
    uint64 Version,
    const FString& ClassName,
    const FString& SnapshotHex)
{
    ++PersistRequestCount;
    PersistBytesTotal += static_cast<uint64>(SnapshotHex.size() / 2);
    const uint64 SnapshotBytes = static_cast<uint64>(SnapshotHex.size() / 2);

#ifdef MESSION_USE_MONGOCXX
    if (Config.EnableMongoStorage && DbThreadPool)
    {
        MPromise<bool> PersistPromise;
        MFuture<bool> PersistFuture = PersistPromise.GetFuture();
        PersistFuture.Then([this, ObjectId, ClassId, OwnerWorldId, RequestId, Version, ClassName, SnapshotBytes](MFuture<bool> Future)
        {
            bool bPersisted = false;
            try
            {
                bPersisted = Future.Get();
            }
            catch (...)
            {
                bPersisted = false;
            }

            if (ITaskRunner* Runner = GetTaskRunner())
            {
                Runner->PostTask([this, bPersisted, ObjectId, ClassId, OwnerWorldId, RequestId, Version, ClassName, SnapshotBytes]()
                {
                    if (bPersisted)
                    {
                        ++PersistMongoSuccess;
                        SendPersistSnapshotResultToWorlds(OwnerWorldId, RequestId, ObjectId, Version, true, "");
                        return;
                    }

                    ++PersistMongoFailed;
                    SendPersistSnapshotResultToWorlds(OwnerWorldId, RequestId, ObjectId, Version, false, "PersistFailed");
                    LOG_DEBUG("Mgo persist snapshot failed: object=%llu class_id=%u owner=%u req=%llu ver=%llu class=%s bytes=%llu",
                              static_cast<unsigned long long>(ObjectId),
                              static_cast<unsigned>(ClassId),
                              static_cast<unsigned>(OwnerWorldId),
                              static_cast<unsigned long long>(RequestId),
                              static_cast<unsigned long long>(Version),
                              ClassName.c_str(),
                              static_cast<unsigned long long>(SnapshotBytes));
                });
            }
        });

        const SMgoConfig AsyncConfig = Config;
        const bool bSubmitted = DbThreadPool->Submit(
            [PersistPromise = std::move(PersistPromise), AsyncConfig, ObjectId, ClassId, OwnerWorldId, RequestId, Version, ClassName, SnapshotHex]() mutable
            {
                const bool bPersisted = PersistSnapshotToMongo(
                    AsyncConfig,
                    ObjectId,
                    ClassId,
                    OwnerWorldId,
                    RequestId,
                    Version,
                    ClassName,
                    SnapshotHex);
                PersistPromise.SetValue(bPersisted);
            });
        if (!bSubmitted)
        {
            ++PersistMongoFailed;
            SendPersistSnapshotResultToWorlds(OwnerWorldId, RequestId, ObjectId, Version, false, "SubmitFailed");
            LOG_WARN("Mgo persist submit failed: object=%llu class_id=%u owner=%u req=%llu ver=%llu class=%s",
                     static_cast<unsigned long long>(ObjectId),
                     static_cast<unsigned>(ClassId),
                     static_cast<unsigned>(OwnerWorldId),
                     static_cast<unsigned long long>(RequestId),
                     static_cast<unsigned long long>(Version),
                     ClassName.c_str());
        }
        return;
    }
#endif

    if (Config.EnableMongoStorage)
    {
        ++PersistMongoFailed;
        SendPersistSnapshotResultToWorlds(OwnerWorldId, RequestId, ObjectId, Version, false, "StorageDisabled");
    }

    LOG_DEBUG("Mgo persist snapshot skipped: object=%llu class_id=%u owner=%u req=%llu ver=%llu class=%s bytes=%llu",
              static_cast<unsigned long long>(ObjectId),
              static_cast<unsigned>(ClassId),
              static_cast<unsigned>(OwnerWorldId),
              static_cast<unsigned long long>(RequestId),
              static_cast<unsigned long long>(Version),
              ClassName.c_str(),
              static_cast<unsigned long long>(SnapshotBytes));
}

void MMgoServer::Rpc_OnLoadSnapshotRequest(uint64 RequestId, uint64 ObjectId)
{
    ++LoadRequestCount;
    LOG_DEBUG("Mgo load request: request=%llu object=%llu",
              static_cast<unsigned long long>(RequestId),
              static_cast<unsigned long long>(ObjectId));

#ifdef MESSION_USE_MONGOCXX
    if (Config.EnableMongoStorage && DbThreadPool)
    {
        struct SLoadResult
        {
            bool bDbCallOk = false;
            bool bFound = false;
            uint16 ClassId = 0;
            FString ClassName;
            FString SnapshotHex;
        };

        MPromise<SLoadResult> LoadPromise;
        MFuture<SLoadResult> LoadFuture = LoadPromise.GetFuture();
        LoadFuture.Then([this, RequestId, ObjectId](MFuture<SLoadResult> Future)
        {
            SLoadResult Result;
            try
            {
                Result = Future.Get();
            }
            catch (...)
            {
                Result.bDbCallOk = false;
                Result.bFound = false;
            }

            if (ITaskRunner* Runner = GetTaskRunner())
            {
                Runner->PostTask([this, RequestId, ObjectId, Result = std::move(Result)]() mutable
                {
                    if (Result.bDbCallOk)
                    {
                        ++LoadMongoSuccess;
                    }
                    else
                    {
                        ++LoadMongoFailed;
                        Result.bFound = false;
                        Result.ClassId = 0;
                        Result.ClassName.clear();
                        Result.SnapshotHex.clear();
                    }

                    SendLoadSnapshotResponseToWorlds(
                        RequestId,
                        ObjectId,
                        Result.bFound,
                        Result.ClassId,
                        Result.ClassName,
                        Result.SnapshotHex);
                    LOG_DEBUG("Mgo load response: request=%llu object=%llu found=%d class=%s bytes=%llu",
                              static_cast<unsigned long long>(RequestId),
                              static_cast<unsigned long long>(ObjectId),
                              Result.bFound ? 1 : 0,
                              Result.ClassName.c_str(),
                              static_cast<unsigned long long>(Result.SnapshotHex.size() / 2));
                });
            }
        });

        const SMgoConfig AsyncConfig = Config;
        const bool bSubmitted = DbThreadPool->Submit([LoadPromise = std::move(LoadPromise), AsyncConfig, ObjectId]() mutable
        {
            SLoadResult Result;
            Result.bDbCallOk = LoadSnapshotFromMongo(
                AsyncConfig,
                ObjectId,
                Result.bFound,
                Result.ClassId,
                Result.ClassName,
                Result.SnapshotHex);
            LoadPromise.SetValue(std::move(Result));
        });
        if (!bSubmitted)
        {
            ++LoadMongoFailed;
            SendLoadSnapshotResponseToWorlds(RequestId, ObjectId, false, 0, "", "");
            LOG_WARN("Mgo load submit failed: request=%llu object=%llu",
                     static_cast<unsigned long long>(RequestId),
                     static_cast<unsigned long long>(ObjectId));
        }
        return;
    }
#endif

    SendLoadSnapshotResponseToWorlds(RequestId, ObjectId, false, 0, "", "");
    if (Config.EnableMongoStorage)
    {
        ++LoadMongoFailed;
    }
    LOG_DEBUG("Mgo load response fallback: request=%llu object=%llu found=0",
              static_cast<unsigned long long>(RequestId),
              static_cast<unsigned long long>(ObjectId));
}

void MMgoServer::SendLoadSnapshotResponseToWorlds(
    uint64 RequestId,
    uint64 ObjectId,
    bool bFound,
    uint16 ClassId,
    const FString& ClassName,
    const FString& SnapshotHex)
{
    for (auto& [ConnectionId, Peer] : BackendConnections)
    {
        (void)ConnectionId;
        if (!Peer.bAuthenticated || Peer.ServerType != EServerType::World || !Peer.Connection)
        {
            continue;
        }

        const bool bSent = MRpc::MWorldService::Rpc_OnMgoLoadSnapshotResponse(
            Peer.Connection,
            RequestId,
            ObjectId,
            bFound,
            ClassId,
            ClassName,
            SnapshotHex);
        if (!bSent)
        {
            LOG_WARN("Mgo->World load response send failed: request=%llu object=%llu world=%s",
                     static_cast<unsigned long long>(RequestId),
                     static_cast<unsigned long long>(ObjectId),
                     Peer.ServerName.c_str());
        }
    }
}

void MMgoServer::SendPersistSnapshotResultToWorlds(
    uint32 OwnerWorldId,
    uint64 RequestId,
    uint64 ObjectId,
    uint64 Version,
    bool bSuccess,
    const FString& Reason)
{
    for (auto& [ConnectionId, Peer] : BackendConnections)
    {
        (void)ConnectionId;
        if (!Peer.bAuthenticated || Peer.ServerType != EServerType::World || !Peer.Connection)
        {
            continue;
        }
        if (OwnerWorldId != 0 && Peer.ServerId != OwnerWorldId)
        {
            continue;
        }

        const bool bSent = MRpc::MWorldService::Rpc_OnMgoPersistSnapshotResult(
            Peer.Connection,
            OwnerWorldId,
            RequestId,
            ObjectId,
            Version,
            bSuccess,
            Reason);
        if (!bSent)
        {
            LOG_WARN("Mgo->World persist ACK send failed: owner=%u request=%llu object=%llu world=%s",
                     static_cast<unsigned>(OwnerWorldId),
                     static_cast<unsigned long long>(RequestId),
                     static_cast<unsigned long long>(ObjectId),
                     Peer.ServerName.c_str());
        }
    }
}
