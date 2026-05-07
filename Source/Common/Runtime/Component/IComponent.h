#pragma once

#include "Common/Runtime/Object/MObject.h"

class IComponent
{
public:
    virtual ~IComponent() = default;

    virtual void OnAttach(MObject* Owner) {}
    virtual void OnDetach() {}

    MObject* GetOwner() const { return Owner; }

protected:
    MObject* Owner = nullptr;
};
