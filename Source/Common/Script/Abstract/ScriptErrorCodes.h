#pragma once

#include "Common/Runtime/MLib.h"

namespace mession::script {

    // 跨 VM 错误码字典(字符串常量)
    // 业务侧 catch / 匹配用,不要在生产代码里直接比较字符串;
    // 后续会演进为 enum + lookup 表。
    namespace ScriptErrorCodes {
        inline constexpr const char* kLuaRuntime = "lua_runtime_error";
        inline constexpr const char* kLuaSyntax  = "lua_syntax_error";
        inline constexpr const char* kLuaMemory  = "lua_memory_error";

        inline constexpr const char* kPythonRuntime   = "python_runtime_error";
        inline constexpr const char* kPythonImport    = "python_import_error";
        inline constexpr const char* kPythonAttribute = "python_attribute_error";

        inline constexpr const char* kJsRuntime   = "js_runtime_error";
        inline constexpr const char* kJsType      = "js_type_error";
        inline constexpr const char* kJsReference = "js_reference_error";

        inline constexpr const char* kCsException = "cs_exception";
        inline constexpr const char* kCsTypeLoad  = "cs_type_load_error";

        inline constexpr const char* kTypeMismatch  = "type_mismatch";
        inline constexpr const char* kNotFound      = "not_found";
        inline constexpr const char* kInvalidArg    = "invalid_arg";
        inline constexpr const char* kUnimplemented = "unimplemented";

        // Script 实例句柄专用
        inline constexpr const char* kClassNotFound    = "class_not_found";
        inline constexpr const char* kFactoryReturnNil = "factory_returned_nil";
        inline constexpr const char* kInstanceReleased = "instance_released";
        inline constexpr const char* kMethodNotFound   = "method_not_found";

        // DualVM 热重载专用
        inline constexpr const char* kVmSwapped       = "vm_swapped";
        inline constexpr const char* kVmDrainTimeout = "vm_drain_timeout";
        inline constexpr const char* kActorNotFound   = "actor_not_found";
    } // namespace ScriptErrorCodes

    // FScriptError:VM 抛出的错误描述
    // 业务侧只需关心 Code + Message;StackTrace 由 VM 实现按需填充
    struct FScriptError {
        MString Code;       // 来自 ScriptErrorCodes 命名空间
        MString Message;    // 人类可读描述
        MString StackTrace; // 可选,VM 实现可填

        FScriptError() = default;

        FScriptError(const char* InCode, MString InMessage, MString InStack = MString()) : Code(InCode), Message(std::move(InMessage)), StackTrace(std::move(InStack)) {
        }

        FScriptError(MString InCode, MString InMessage, MString InStack = MString()) : Code(std::move(InCode)), Message(std::move(InMessage)), StackTrace(std::move(InStack)) {
        }
    };

    // FScriptError → MString 转换(满足 TResult<T, MString> 接口)
    // 格式:"[code] message\nstack_trace"
    inline MString ToErrorString(const FScriptError& E) {
        MString Out;
        Out.reserve(16 + E.Code.size() + E.Message.size() + E.StackTrace.size());
        Out.append("[");
        Out.append(E.Code);
        Out.append("] ");
        Out.append(E.Message);
        if (!E.StackTrace.empty()) {
            Out.append("\n");
            Out.append(E.StackTrace);
        }
        return Out;
    }

} // namespace mession::script