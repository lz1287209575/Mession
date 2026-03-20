#include "AOIComponent.h"
#include "Common/Runtime/Log/Logger.h"

void MAOISystem::AddObject(uint64 ObjectId, const SVector& Position)
{
    ObjectPositions[ObjectId] = Position;
    
    SAOICell Cell = GetCell(Position);
    AddObjectToCell(ObjectId, Cell);
    
    LOG_DEBUG("Object %llu added to AOI cell (%d, %d)", 
              (unsigned long long)ObjectId, Cell.X, Cell.Y);
}

void MAOISystem::RemoveObject(uint64 ObjectId)
{
    auto It = ObjectPositions.find(ObjectId);
    if (It == ObjectPositions.end())
    {
        return;
    }
    
    SAOICell Cell = GetCell(It->second);
    RemoveObjectFromCell(ObjectId, Cell);
    
    ObjectPositions.erase(It);
}

void MAOISystem::UpdateObjectPosition(uint64 ObjectId, const SVector& NewPosition)
{
    auto It = ObjectPositions.find(ObjectId);
    if (It == ObjectPositions.end())
    {
        // 对象不存在，直接添加
        AddObject(ObjectId, NewPosition);
        return;
    }
    
    SAOICell OldCell = GetCell(It->second);
    SAOICell NewCell = GetCell(NewPosition);
    
    if (OldCell.X != NewCell.X || OldCell.Y != NewCell.Y)
    {
        // 跨格子了，移除旧的，添加新的
        RemoveObjectFromCell(ObjectId, OldCell);
        AddObjectToCell(ObjectId, NewCell);
    }
    
    It->second = NewPosition;
}

void MAOISystem::GetVisibleObjects(uint64 ObjectId, TVector<uint64>& OutVisibleObjects)
{
    OutVisibleObjects.clear();
    
    auto It = ObjectPositions.find(ObjectId);
    if (It == ObjectPositions.end())
    {
        return;
    }
    
    SAOICell CenterCell = GetCell(It->second);
    
    // 获取周围3x3区域内的对象
    for (int32 dx = -1; dx <= 1; ++dx)
    {
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            SAOICell Cell(CenterCell.X + dx, CenterCell.Y + dy);
            
            auto CellIt = Cells.find(Cell);
            if (CellIt != Cells.end())
            {
                for (uint64 OtherId : CellIt->second.Objects)
                {
                    if (OtherId != ObjectId)
                    {
                        OutVisibleObjects.push_back(OtherId);
                    }
                }
            }
        }
    }
}

SAOICell MAOISystem::GetCell(const SVector& Position) const
{
    int32 X = (int32)std::floor(Position.X / CellSize);
    int32 Y = (int32)std::floor(Position.Y / CellSize);
    return SAOICell(X, Y);
}

void MAOISystem::AddObjectToCell(uint64 ObjectId, const SAOICell& Cell)
{
    Cells[Cell].Objects.insert(ObjectId);
}

void MAOISystem::RemoveObjectFromCell(uint64 ObjectId, const SAOICell& Cell)
{
    auto It = Cells.find(Cell);
    if (It != Cells.end())
    {
        It->second.Objects.erase(ObjectId);
        
        // 如果格子空了，可以选择删除
        if (It->second.Objects.empty())
        {
            Cells.erase(It);
        }
    }
}
