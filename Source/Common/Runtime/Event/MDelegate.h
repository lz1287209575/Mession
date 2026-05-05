#pragma once

#include "Common/Runtime/MLib.h"

/**
 * MDelegate - 类型安全的委托系统
 *
 * 封装：对象指针 + 成员函数指针
 * 支持：Bind(object, &Class::Method) -> Delegate
 * 调用：Delegate.Invoke(args...)
 *
 * 设计参考：UE TMulticastDelegate，但更轻量，专注于游戏服务器场景。
 *
 * 用法示例：
 *   MDelegate<void(int, float)> d;
 *   d.Bind(&myObject, &MyObject::OnEvent);
 *   d.Invoke(42, 3.14f);
 *
 *   // 多播委托
 *   MMulticastDelegate<void()> OnLogin;
 *   OnLogin.Add(&sys1, &Sys1::Handle);
 *   OnLogin.Broadcast();
 */

template<typename TSignature>
class MDelegate;

// ============================================
// 单播委托特化：void(T*)
//
// 每个 MDelegate 只能绑定一个处理函数。
// 通过空基类优化（EBO）避免存储多余状态。
// ============================================

template<typename TClass, typename TEvent>
class MDelegate<void(TClass*, const TEvent*)>
{
public:
    using TMethod = void (TClass::*)(const TEvent*);

    MDelegate() = default;
    MDelegate(std::nullptr_t) {}

    template<typename TObject>
    void Bind(TObject* InObject, void (TObject::*InMethod)(const TEvent*))
    {
        Object = static_cast<TClass*>(InObject);
        Method = reinterpret_cast<TMethod>(InMethod);
    }

    void Clear()
    {
        Object = nullptr;
        Method = nullptr;
    }

    explicit operator bool() const { return Object && Method; }

    void operator()(const TEvent* Event) const { Invoke(Event); }

    void Invoke(const TEvent* Event) const
    {
        if (Object && Method)
        {
            (Object->*Method)(Event);
        }
    }

    TClass* GetObject() const { return Object; }
    TMethod GetMethod() const { return Method; }

private:
    TClass* Object = nullptr;
    TMethod Method = nullptr;
};

// ============================================
// 多播委托特化：void(T*)
//
// 一个委托列表，可绑定多个处理函数。
// Broadcast() 依次调用所有已注册的处理函数。
// 线程安全由调用方保证（通常在 strand 内调用）。
// ============================================

template<typename TClass, typename TEvent>
class MMulticastDelegate<void(TClass*, const TEvent*)>
{
public:
    using TSingleDelegate = MDelegate<void(TClass*, const TEvent*)>;
    using TMethod = typename TSingleDelegate::TMethod;

    // 添加一个处理函数，返回句柄（用于后续移除）
    // 句柄为该委托在列表中的索引。
    uint32 Add(TClass* InObject, TMethod InMethod)
    {
        uint32 Handle = static_cast<uint32>(Delegates.size());
        Delegates.push_back(TSingleDelegate());
        Delegates.back().Bind(InObject, InMethod);
        return Handle;
    }

    // 添加委托（右值，直接 push）
    uint32 AddDelegate(TSingleDelegate&& D)
    {
        uint32 Handle = static_cast<uint32>(Delegates.size());
        Delegates.push_back(std::move(D));
        return Handle;
    }

    // 通过句柄移除
    void Remove(uint32 Handle)
    {
        if (Handle < Delegates.size())
        {
            Delegates[Handle].Clear();
        }
    }

    // 通过对象+方法移除（线性查找）
    template<typename TObject>
    void Remove(TObject* InObject, void (TObject::*InMethod)(const TEvent*))
    {
        TMethod M = reinterpret_cast<TMethod>(InMethod);
        for (auto& D : Delegates)
        {
            if (D.GetObject() == static_cast<TClass*>(InObject) && D.GetMethod() == M)
            {
                D.Clear();
                break;
            }
        }
    }

    // 清空所有处理函数
    void Clear()
    {
        for (auto& D : Delegates)
        {
            D.Clear();
        }
        Delegates.clear();
    }

    // 是否为空
    bool IsEmpty() const
    {
        for (const auto& D : Delegates)
        {
            if (D) return false;
        }
        return true;
    }

    // 广播：依次调用所有有效处理函数
    void Broadcast(const TEvent* Event) const
    {
        for (const auto& D : Delegates)
        {
            if (D)
            {
                D.Invoke(Event);
            }
        }
    }

    // 重载 () 调用广播
    void operator()(const TEvent* Event) const { Broadcast(Event); }

    size_t GetHandlerCount() const { return Delegates.size(); }

private:
    TVector<TSingleDelegate> Delegates;
};

// ============================================
// 句柄式订阅：用于一次性订阅
//
// 用法：
//   MEventSubscription Sub = Bus.Subscribe<FPlayerLoginEvent>(
//       this, &MySystem::OnLogin);
//   // 触发一次后自动退订：
//   Sub.MarkAsOneShot();
//   Bus.Publish(event);
// ============================================

struct MEventSubscription
{
    uint32 Handle = 0;
    uint32 EventTypeId = 0;

    bool IsValid() const { return Handle != 0 && EventTypeId != 0; }
    void Reset() { Handle = 0; EventTypeId = 0; }

    // 标记为一次性订阅（触发一次后自动退订）
    MEventSubscription& OneShot() { bOneShot = true; return *this; }
    bool IsOneShot() const { return bOneShot; }

private:
    bool bOneShot = false;
};
