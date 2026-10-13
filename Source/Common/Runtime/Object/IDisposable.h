/**
 * @file IDisposable.h
 * @brief IDisposable —— 持有非托管资源的 MObject 子类的显式释放接口。
 *
 * @detail 与析构的分工:普通成员资源由 C++ 析构负责,而 socket fd、文件句柄、
 *   native buffer 这类"需要确定性回收、又不能等到 TSharedPtr 归零"的资源走
 *   Dispose() 显式释放。本接口不继承 MObject,通过 dynamic_cast 由 ~MObject 触发。
 */
#pragma once

/**
 * @brief IDisposable - 可释放资源接口。
 *
 * @detail 用法:
 *   - 任何持有非托管资源(socket fd、文件句柄、native buffer)的 MObject 子类实现本接口
 *   - ~MObject() 会 dynamic_cast 检查本接口,并在未释放时调用一次 Dispose()
 *   - 子类 Dispose() 应先查 IsDisposed() 保证幂等,释放完毕再调 MarkDisposed()
 *   - bDisposed 是普通 bool(非 std::atomic):跨线程同时 Dispose 需调用方自己同步
 */
class IDisposable {
    public:
    virtual ~IDisposable() = default;

    virtual void Dispose() = 0;

    public:
    /** @brief 子类 Dispose() 末尾调用;只置标志位,不触发任何释放动作。 */
    void MarkDisposed() {
        bDisposed = true;
    }
    /**
     * @brief 查询是否已释放。
     * @return true 已调用过 MarkDisposed()
     *
     * @detail ~MObject 在调用 Dispose() 前会先查这个标志,子类也可在 Dispose()
     *   开头用它实现幂等(重复调用直接返回)。
     */
    bool IsDisposed() const {
        return bDisposed;
    }

    private:
    bool bDisposed = false;
};
