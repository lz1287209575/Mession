#pragma once

#include "../../Core/NetCore.h"
#include <map>
#include <set>
#include <vector>

// AOI (Area of Interest) 区域
struct FAOICell
{
    int32 X = 0;
    int32 Y = 0;
    std::set<uint64> Objects;  // 在这个区域内的对象
    
    FAOICell() = default;
    FAOICell(int32 InX, int32 InY) : X(InX), Y(InY) {}
    
    bool operator<(const FAOICell& Other) const
    {
        if (X != Other.X)
            return X < Other.X;
        return Y < Other.Y;
    }
};

// AOI系统 - 区域感知
class FAOISystem
{
private:
    float CellSize = 100.0f;  // 每个格子的大小
    
    // 格子映射
    std::map<FAOICell, FAOICell> Cells;
    
    // 对象位置缓存
    std::map<uint64, FVector> ObjectPositions;
    
public:
    FAOISystem(float InCellSize = 100.0f) : CellSize(InCellSize) {}
    
    // 将对象添加到AOI系统
    void AddObject(uint64 ObjectId, const FVector& Position);
    
    // 移除对象
    void RemoveObject(uint64 ObjectId);
    
    // 更新对象位置
    void UpdateObjectPosition(uint64 ObjectId, const FVector& NewPosition);
    
    // 获取对象周围可见的其他对象
    void GetVisibleObjects(uint64 ObjectId, std::vector<uint64>& OutVisibleObjects);
    
    // 获取对象所在的格子
    FAOICell GetCell(const FVector& Position) const;
    
private:
    void AddObjectToCell(uint64 ObjectId, const FAOICell& Cell);
    void RemoveObjectFromCell(uint64 ObjectId, const FAOICell& Cell);
};
