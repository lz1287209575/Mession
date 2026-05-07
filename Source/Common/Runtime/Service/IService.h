#pragma once

class IService
{
public:
    virtual ~IService() = default;

    virtual const char* GetServiceName() const = 0;
    virtual bool IsAvailable() const { return true; }
};
