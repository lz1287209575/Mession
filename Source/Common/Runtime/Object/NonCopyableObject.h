/**
 * @file NonCopyableObject.h
 * @brief MNonCopyable —— 禁止拷贝、允许移动的空基类。
 *
 * @detail 给服务、连接等"有唯一身份、不该被值语义复制"的类型做基类;
 *   移动操作保留默认实现,便于放进容器或按值返回。
 */
#pragma once

/**
 * @brief MNonCopyable - 不可复制基类(可移动)。
 *
 * @detail 只做编译期约束,无任何数据成员,不引入额外开销(空基类优化)。
 */
struct MNonCopyable {
    MNonCopyable()                               = default;
    MNonCopyable(const MNonCopyable&)            = delete;
    MNonCopyable& operator=(const MNonCopyable&) = delete;
    MNonCopyable(MNonCopyable&&)                 = default;
    MNonCopyable& operator=(MNonCopyable&&)      = default;
};
