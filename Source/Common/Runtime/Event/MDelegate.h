/**
 * @file MDelegate.h
 * @brief MDelegate / MMulticastDelegate(轻量类型安全委托)与 MEventSubscription(订阅句柄)。
 *
 * @detail 只提供一种签名的特化:void(TClass*, const TEvent*) —— 单播与多播各一个。
 * 两个委托都是纯数据(对象指针 + 成员函数指针),不做生命周期管理:被绑对象先析构就会悬垂,
 * 不提供弱引用/自动解绑。多播版本把处理函数平铺存在 TVector 里,Add 只追加、Remove 只置空,
 * 因此已发出的句柄长期有效。全部实现无内部加锁,线程安全由调用方保证(通常在单个
 * strand / loop 线程内使用)。
 */
#pragma once

#include "Common/Runtime/MLib.h"

/**
 * @brief MDelegate - 类型安全委托的通用模板声明。
 *
 * @detail 这是一个**只有声明、没有定义**的主模板:实际只特化了
 * void(TClass*, const TEvent*) 这一种签名。因此像 MDelegate<void(int, float)>
 * 这样的写法会得到不完整类型而编译失败 —— 需要别的签名时必须新增特化。
 */
template <typename TSignature> class MDelegate;

// ============================================
// 单播委托特化:void(T*)
//
// 每个 MDelegate 只能绑定一个处理函数。
// ============================================

/**
 * @brief MDelegate<void(TClass*, const TEvent*)> - 单播委托,只持有一个成员函数。
 *
 * @detail 存储就是对象指针 + 成员函数指针两个字段,没有基类、也没有做类型擦除
 * (旧注释里"通过空基类优化(EBO)避免存储多余状态"的说法不成立,本实现没有空基类)。
 */
template <typename TClass, typename TEvent> class MDelegate<void(TClass*, const TEvent*)> {
    public:
    using TMethod = void (TClass::*)(const TEvent*);

    MDelegate() = default;
    MDelegate(std::nullptr_t) {
    }

    /**
     * @brief Bind - 绑定对象与成员函数。
     * @param InObject 处理对象(非拥有,需比本委托活得久)
     * @param InMethod 成员函数指针
     *
     * @detail 参数用独立模板 TObject 接收,再 static_cast 到 TClass*、
     * reinterpret_cast 成 TMethod —— 因此**不会**在编译期校验 TObject 与 TClass 的继承关系:
     * 传错类型能编译通过,调用时表现为未定义行为(故意避免 RTTI/虚表依赖的代价)。
     */
    template <typename TObject> void Bind(TObject* InObject, void (TObject::*InMethod)(const TEvent*)) {
        Object = static_cast<TClass*>(InObject);
        Method = reinterpret_cast<TMethod>(InMethod);
    }

    /**
     * @brief Clear - 解绑(把对象与函数指针都置空)。
     */
    void Clear() {
        Object = nullptr;
        Method = nullptr;
    }

    /**
     * @brief operator bool - 是否已绑定。
     * @return true 当且仅当对象与成员函数指针都在
     */
    explicit operator bool() const {
        return Object && Method;
    }

    /**
     * @brief operator() - 等价于 Invoke。
     */
    void operator()(const TEvent* Event) const {
        Invoke(Event);
    }

    /**
     * @brief Invoke - 调用已绑定的成员函数。
     * @param Event 事件指针,原样转交(可为 nullptr,由被调函数自己约定)
     *
     * @detail 未绑定时静默返回(不抛异常、不崩溃),便于可选回调的写法。
     */
    void Invoke(const TEvent* Event) const {
        if (Object && Method) {
            (Object->*Method)(Event);
        }
    }

    /**
     * @brief GetObject - 取已绑定的对象指针(未绑定时为 nullptr)。
     */
    TClass* GetObject() const {
        return Object;
    }

    /**
     * @brief GetMethod - 取已绑定的成员函数指针(用于按对象+方法反查)。
     */
    TMethod GetMethod() const {
        return Method;
    }

    private:
    TClass* Object = nullptr;
    TMethod Method = nullptr;
};

// ============================================
// 多播委托特化:void(T*)
//
// 一个委托列表,可绑定多个处理函数。
// ============================================

/**
 * @brief MMulticastDelegate<void(TClass*, const TEvent*)> - 多播委托。
 *
 * @detail 语义(实现即契约,含若干易踩点):
 *   - Add 只追加,句柄 = 追加前的槽位下标,因此句柄一旦发出就不会因后续 Remove 失效;
 *   - Remove/Clear 只把槽位置空,不压缩、也不复用空槽 —— 句柄永远是"下标"而非"序号";
 *   - GetHandlerCount() 返回槽位总数(含已置空的),不等于有效处理函数个数;
 *   - Broadcast 按槽位顺序同步调用所有非空处理函数,不捕获异常:中途抛异常/崩溃会中断
 *     后续处理函数,需要在处理函数内部自行兜底;
 *   - 无内部加锁,线程安全由调用方保证(通常在 strand 内调用)。
 */
template <typename TClass, typename TEvent> class MMulticastDelegate<void(TClass*, const TEvent*)> {
    public:
    using TSingleDelegate = MDelegate<void(TClass*, const TEvent*)>;
    using TMethod         = typename TSingleDelegate::TMethod;

    /**
     * @brief Add - 添加一个处理函数。
     * @return 句柄(= 该处理函数所在槽位下标,可用于后续 Remove)
     *
     * @detail 注意第一个处理函数的句柄是 0,而 MEventSubscription::IsValid() 把 0 视为无效;
     * 两者混用时最容易在"退订第一个处理函数"上踩坑。
     */
    uint32 Add(TClass* InObject, TMethod InMethod) {
        uint32 Handle = static_cast<uint32>(Delegates.size());
        Delegates.push_back(TSingleDelegate());
        Delegates.back().Bind(InObject, InMethod);
        return Handle;
    }

    /**
     * @brief AddDelegate - 直接追加一个已构造好的委托(移动)。
     * @return 句柄(= 槽位下标)
     */
    uint32 AddDelegate(TSingleDelegate&& D) {
        uint32 Handle = static_cast<uint32>(Delegates.size());
        Delegates.push_back(std::move(D));
        return Handle;
    }

    /**
     * @brief Remove - 按句柄移除。
     * @param Handle Add/AddDelegate 返回的句柄
     *
     * @detail 越界句柄静默忽略;只置空对应槽位,不改变其它句柄。
     */
    void Remove(uint32 Handle) {
        if (Handle < Delegates.size()) {
            Delegates[Handle].Clear();
        }
    }

    /**
     * @brief Remove - 按"对象 + 成员函数"移除(线性查找,只移除第一个匹配项)。
     * @param InObject 处理对象
     * @param InMethod 成员函数指针
     */
    template <typename TObject> void Remove(TObject* InObject, void (TObject::*InMethod)(const TEvent*)) {
        TMethod M = reinterpret_cast<TMethod>(InMethod);
        for (auto& D : Delegates) {
            if (D.GetObject() == static_cast<TClass*>(InObject) && D.GetMethod() == M) {
                D.Clear();
                break;
            }
        }
    }

    /**
     * @brief Clear - 解绑全部处理函数并释放槽位(之后句柄全部失效)。
     */
    void Clear() {
        for (auto& D : Delegates) {
            D.Clear();
        }
        Delegates.clear();
    }

    /**
     * @brief IsEmpty - 是否没有任何有效处理函数。
     * @return true 当且仅当所有槽位都是空的
     */
    bool IsEmpty() const {
        for (const auto& D : Delegates) {
            if (D)
                return false;
        }
        return true;
    }

    /**
     * @brief Broadcast - 依次调用所有有效处理函数。
     * @param Event 事件指针,原样转交
     */
    void Broadcast(const TEvent* Event) const {
        for (const auto& D : Delegates) {
            if (D) {
                D.Invoke(Event);
            }
        }
    }

    /**
     * @brief operator() - 等价于 Broadcast。
     */
    void operator()(const TEvent* Event) const {
        Broadcast(Event);
    }

    /**
     * @brief GetHandlerCount - 槽位总数(含已 Remove 留下的空槽)。
     */
    size_t GetHandlerCount() const {
        return Delegates.size();
    }

    private:
    TVector<TSingleDelegate> Delegates;
};

// ============================================
// 句柄式订阅:用于一次性订阅
//
// 用法:
//   MEventSubscription Sub = Bus.Subscribe<FPlayerLoginEvent>(
//       this, &MySystem::OnLogin);
//   // 触发一次后自动退订:
//   Sub.OneShot();
//   Bus.Publish(event);
// ============================================

/**
 * @brief MEventSubscription - 订阅句柄(由 MEventBus::Subscribe / SubscribeOnce 返回)。
 *
 * @detail Handle 是该事件类型内处理函数列表的下标,EventTypeId 指明所属事件类型;
 * 两者一起才能定位到一个订阅。OneShot() 只在本结构上打标记,真正的"触发一次后自动退订"
 * 由 MEventBus::Publish 依据 OneShotHandles 实现。
 * 已知限制:第一个订阅者的 Handle 恰为 0,而 IsValid() 要求 Handle != 0,
 * 因此**句柄 0 会被当作无效订阅**,无法用该句柄退订(详见 MEventBus::Unsubscribe)。
 */
struct MEventSubscription {
    uint32 Handle      = 0;
    uint32 EventTypeId = 0;

    /**
     * @brief IsValid - 句柄是否可用于退订。
     * @return true 当且仅当 Handle != 0 且 EventTypeId != 0
     */
    bool IsValid() const {
        return Handle != 0 && EventTypeId != 0;
    }

    /**
     * @brief Reset - 复位为无效句柄(不解除订阅本身)。
     */
    void Reset() {
        Handle      = 0;
        EventTypeId = 0;
    }

    /**
     * @brief OneShot - 标记为一次性订阅(触发一次后由 Publish 自动退订)。
     * @return *this,支持链式写法
     *
     * @detail 注意:SubscribeOnce 会同时把这个标记和 OneShotHandles 登记一起做;
     * 手工调用 OneShot() 只置标记,不会登记到 OneShotHandles,Publish 不会自动退订。
     */
    MEventSubscription& OneShot() {
        bOneShot = true;
        return *this;
    }

    /**
     * @brief IsOneShot - 是否被标记为一次性订阅。
     */
    bool IsOneShot() const {
        return bOneShot;
    }

    private:
    bool bOneShot = false;
};
