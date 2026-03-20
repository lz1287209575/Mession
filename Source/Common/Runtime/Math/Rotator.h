#pragma once

// 旋转向量
struct SRotator
{
    float Pitch = 0.0f;
    float Yaw = 0.0f;
    float Roll = 0.0f;

    SRotator() = default;
    SRotator(float InPitch, float InYaw, float InRoll) 
        : Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {}
};