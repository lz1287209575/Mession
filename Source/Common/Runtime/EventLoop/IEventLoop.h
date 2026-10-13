/**
 * @file IEventLoop.h
 * @brief IEventLoop —— 可被主循环逐帧驱动的子事件循环抽象接口。
 *
 * @detail 实现者只提供单步入口 RunOnce();驱动频率与调用线程由外层主循环
 *   决定(例如 MEventLoopGroup、MNetServerBase 的 reactor 线程)。
 *   TimeoutMs 的具体语义由子类自行解释:网络循环当作 poll 超时,
 *   纯任务循环可忽略。
 */
#pragma once

/**
 * @brief IEventLoop - 子事件循环 / 单步执行抽象。
 *
 * @detail 本接口不约定线程安全:由实现方声明(例如 MNetEventLoop 要求
 *   所有注册与 RunOnce 都在同一线程调用)。析构由持有者负责,
 *   接口本身不提供 Stop/Join 语义。
 */
class IEventLoop {
    public:
    virtual ~IEventLoop() = default;

    /**
     * @brief RunOnce - 执行一帧(非阻塞语义由实现方决定)。
     * @param timeoutMs 单步超时(毫秒),由子类解释:网络循环用作 poll 超时,纯任务循环忽略
     */
    virtual void RunOnce(int timeoutMs = 0) = 0;
};
