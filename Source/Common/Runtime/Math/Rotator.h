/**
 * @file Rotator.h
 * @brief SRotator - Pitch / Yaw / Roll 三轴旋转量。
 *
 * @detail 纯数据载体:不做角度单位换算、不做归一化、不含旋转矩阵/四元数变换。
 * 单位由使用方自行约定(本类型只保证是三个 float)。反射存档按 POD 整体写出,
 * 字段个数/顺序/类型即为序列化布局,禁止调整。
 */
#pragma once

/**
 * @brief SRotator - 三轴旋转量(Pitch 俯仰 / Yaw 偏航 / Roll 翻滚)。
 *
 * @detail 默认构造为全 0(即无旋转)。各分量独立,不保证欧拉角的合法范围。
 */
struct SRotator {
    float Pitch = 0.0f;
    float Yaw   = 0.0f;
    float Roll  = 0.0f;

    SRotator() = default;
    SRotator(float InPitch, float InYaw, float InRoll) : Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {
    }
};
