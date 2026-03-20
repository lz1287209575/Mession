#pragma once

#include "Common/Runtime/MLib.h"

// AOI (Area of Interest) 区域
struct SAOICell
{
    int32 X = 0;
    int32 Y = 0;
    TSet<uint64> Objects;  // 在这个区域内的对象
    
    SAOICell() = default;
    SAOICell(int32 InX, int32 InY) : X(InX), Y(InY) {}
    
    bool operator<(const SAOICell& Other) const
    {
        if (X != Other.X)
        {
            return X < Other.X;
        }
        return Y < Other.Y;
    }
};

// AOI系统 - 区域感知
class MAOISystem
{
private:
    float CellSize = 100.0f;  // 每个格子的大小
    
    // 格子映射
    TMap<SAOICell, SAOICell> Cells;
    
    // 对象位置缓存
    TMap<uint64, SVector> ObjectPositions;
    
public:
    MAOISystem(float InCellSize = 100.0f) : CellSize(InCellSize) {}
    
    // 将对象添加到AOI系统
    void AddObject(uint64 ObjectId, const SVector& Position);
    
    // 移除对象
    void RemoveObject(uint64 ObjectId);
    
    // 更新对象位置
    void UpdateObjectPosition(uint64 ObjectId, const SVector& NewPosition);
    
    // 获取对象周围可见的其他对象
    void GetVisibleObjects(uint64 ObjectId, TVector<uint64>& OutVisibleObjects);
    
    // 获取对象所在的格子
    SAOICell GetCell(const SVector& Position) const;
    
private:
    void AddObjectToCell(uint64 ObjectId, const SAOICell& Cell);
    void RemoveObjectFromCell(uint64 ObjectId, const SAOICell& Cell);
};
