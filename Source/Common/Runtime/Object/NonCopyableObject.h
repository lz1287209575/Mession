#pragma once

// 不可复制基类（可移动），用于服务、连接等
struct MNonCopyable
{
    MNonCopyable() = default;
    MNonCopyable(const MNonCopyable&) = delete;
    MNonCopyable& operator=(const MNonCopyable&) = delete;
    MNonCopyable(MNonCopyable&&) = default;
    MNonCopyable& operator=(MNonCopyable&&) = default;
};
