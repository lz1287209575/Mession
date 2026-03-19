#pragma once

#include "NetDriver/PersistenceSubsystem.h"

struct SMongoPersistenceConfig
{
    FString Uri = "mongodb://127.0.0.1:27017";
    FString Database = "mession";
    FString Collection = "world_snapshots";
    bool bUpsert = true;
};

class MMongoPersistenceSink : public IPersistenceSink
{
public:
    explicit MMongoPersistenceSink(SMongoPersistenceConfig InConfig);
    bool Persist(const SPersistenceRecord& InRecord) override;

private:
    SMongoPersistenceConfig Config;
    bool bWarnedNoDriver = false;
};
