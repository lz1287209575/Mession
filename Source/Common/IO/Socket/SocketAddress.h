/**
 * @file SocketAddress.h
 * @brief SSocketAddress - IPv4 的 "ip + port" 值对象。
 *
 * @detail 刻意不引入任何平台网络头文件:只 include 本文件就能使用地址类型,
 * 不必拖进 winsock2 / arpa-inet。只表示 IPv4(下游 MSocketPlatform 全走 AF_INET),
 * Ip 是点分十进制字符串,格式合法性由使用方或 MSocketPlatform::ParseIPv4Address 校验。
 */
#pragma once

#include "Common/Runtime/MLib.h"

/**
 * @brief SSocketAddress - IPv4 地址(字符串形式 IP + 端口)。
 *
 * @detail 纯数据。默认构造为空 IP + 端口 0(即 IsValid() == false)。
 */
struct SSocketAddress {
    MString Ip;
    uint16  Port = 0;

    SSocketAddress() = default;

    SSocketAddress(const MString& InIp, uint16 InPort) : Ip(InIp), Port(InPort) {
    }

    /**
     * @brief IsValid - 地址是否可用于发起连接/绑定。
     * @return true 当且仅当 Ip 非空且 Port != 0
     *
     * @detail 只判"非空"与"端口非 0":不校验 Ip 是否为合法点分十进制,
     * 也不接受 Port == 0 表示"任意端口"。
     */
    bool IsValid() const {
        return !Ip.empty() && Port != 0;
    }
};
