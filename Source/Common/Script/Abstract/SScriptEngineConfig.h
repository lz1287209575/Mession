/**
 * @file SScriptEngineConfig.h
 * @brief SScriptEngineConfig —— 脚本引擎初始化配置。
 *
 * @detail NOTE: 该结构目前是"预留契约"。MLuaEngine::Initialize 的配置形参未命名,
 *   未读取任何字段 —— 语言固定为 Lua、VM 名固定 "default"、热重载由外部直接写
 *   MLuaEngine::HotReloadNewBytes 触发。要让这些字段生效须在实现侧显式读取。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Script/Abstract/EScriptLanguage.h"

namespace mession::script {

    /**
     * @brief SScriptEngineConfig - 脚本引擎初始化配置(当前实现未消费)。
     */
    struct SScriptEngineConfig {
        EScriptLanguage Language = EScriptLanguage::Lua; // 目标语言;当前仅 Lua 有实现
        MString         VmName   = "default";            // VM 逻辑名
        MString         ReflectRootPath;                 // Build/Generated/{vm}/ — MHeaderTool emit 根
        float           TickHz                 = 60.0f;  // 脚本 tick 期望频率(Hz)
        bool            bEnableDebugPort       = false;  // 是否开 MobDebug 监听
        uint16          DebugPort              = 0;      // MobDebug 端口;0 表示未指定
        bool            bHotReloadOnFileChange = true;   // 文件变更时是否自动热重载
    };

} // namespace mession::script