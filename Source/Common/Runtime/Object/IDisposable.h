#pragma once

/**
 * IDisposable — MObject 派生类的可释放资源接口。
 *
 * 用法：
 *   - 任何持有非托管资源（socket fd、文件句柄、native buffer）的 MObject 子类必须实现此接口。
 *   - ~MObject() 在销毁时会 dynamic_cast 检查 IDisposable，调用 Dispose()（只一次）。
 *   - 子类 Dispose() 内调 MarkDisposed() 标志位 + 释放资源；可重复调用幂等。
 */
class IDisposable
{
public:
    virtual ~IDisposable() = default;

    virtual void Dispose() = 0;

public:
    /** 子类 Dispose() 完成后调用，标志位避免重复 Dispose。*/
    void MarkDisposed() { bDisposed = true; }
    /** ~MObject 调 Dispose() 前查询；子类可在 Dispose() 内检测幂等。*/
    bool IsDisposed() const { return bDisposed; }

private:
    bool bDisposed = false;
};
