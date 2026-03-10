#include "AOIComponent.h"
#include "../../Common/Logger.h"

void FAOISystem::AddObject(uint64 ObjectId, const FVector& Position)
{
    ObjectPositions[ObjectId] = Position;
    
    FAOICell Cell = GetCell(Position);
    AddObjectToCell(ObjectId, Cell);
    
    LOG_DEBUG("Object %llu added to AOI cell (%d, %d)", 
              (unsigned long long)ObjectId, Cell.X, Cell.Y);
}

void FAOISystem::RemoveObject(uint64 ObjectId)
{
    auto It = ObjectPositions.find(ObjectId);
    if (It == ObjectPositions.end())
        return;
    
    FAOICell Cell = GetCell(It->second);
    RemoveObjectFromCell(ObjectId, Cell);
    
    ObjectPositions.erase(It);
}

void FAOISystem::UpdateObjectPosition(uint64 ObjectId, const FVector& NewPosition)
{
    auto It = ObjectPositions.find(ObjectId);
    if (It == ObjectPositions.end())
    {
        // 对象不存在，直接添加
        AddObject(ObjectId, NewPosition);
        return;
    }
    
    FAOICell OldCell = GetCell(It->second);
    FAOICell NewCell = GetCell(NewPosition);
    
    if (OldCell.X != NewCell.X || OldCell.Y != NewCell.Y)
    {
        // 跨格子了，移除旧的，添加新的
        RemoveObjectFromCell(ObjectId, OldCell);
        AddObjectToCell(ObjectId, NewCell);
    }
    
    It->second = NewPosition;
}

void FAOISystem::GetVisibleObjects(uint64 ObjectId, std::vector<uint64>& OutVisibleObjects)
{
    OutVisibleObjects.clear();
    
    auto It = ObjectPositions.find(ObjectId);
    if (It == ObjectPositions.end())
        return;
    
    FAOICell CenterCell = GetCell(It->second);
    
    // 获取周围3x3区域内的对象
    for (int32 dx = -1; dx <= 1; ++dx)
    {
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            FAOICell Cell(CenterCell.X + dx, CenterCell.Y + dy);
            
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

FAOICell FAOISystem::GetCell(const FVector& Position) const
{
    int32 X = (int32)std::floor(Position.X / CellSize);
    int32 Y = (int32)std::floor(Position.Y / CellSize);
    return FAOICell(X, Y);
}

void FAOISystem::AddObjectToCell(uint64 ObjectId, const FAOICell& Cell)
{
    Cells[Cell].Objects.insert(ObjectId);
}

void FAOISystem::RemoveObjectFromCell(uint64 ObjectId, const FAOICell& Cell)
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
