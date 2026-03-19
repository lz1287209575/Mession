#include "NetDriver/MongoPersistenceSink.h"

#include "Common/Logger.h"

namespace
{
FString BytesToHex(const TArray& InData)
{
    static const char* Digits = "0123456789ABCDEF";
    FString Out;
    Out.reserve(InData.size() * 2);
    for (uint8 Byte : InData)
    {
        Out.push_back(Digits[(Byte >> 4) & 0x0F]);
        Out.push_back(Digits[Byte & 0x0F]);
    }
    return Out;
}
}

MMongoPersistenceSink::MMongoPersistenceSink(SMongoPersistenceConfig InConfig)
    : Config(std::move(InConfig))
{
}

bool MMongoPersistenceSink::Persist(const SPersistenceRecord& InRecord)
{
    // 最小闭环版本：先以 Mongo 文档结构输出，避免业务层感知存储细节。
    // 后续可在此处切换到 mongocxx / 驱动真实写入，不影响上层调用。
    if (!bWarnedNoDriver)
    {
        LOG_WARN(
            "Mongo sink running in stub mode (no mongocxx linked): uri=%s db=%s coll=%s",
            Config.Uri.c_str(),
            Config.Database.c_str(),
            Config.Collection.c_str());
        bWarnedNoDriver = true;
    }

    LOG_DEBUG(
        "MongoSink doc {object_id:%llu,class_id:%u,class:%s,snapshot_hex:%s}",
        static_cast<unsigned long long>(InRecord.ObjectId),
        static_cast<unsigned>(InRecord.ClassId),
        InRecord.ClassName.c_str(),
        BytesToHex(InRecord.SnapshotData).c_str());
    return true;
}
