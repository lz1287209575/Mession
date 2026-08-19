#pragma once

#include "Common/Runtime/MLib.h"

namespace mession::script {

    // TScriptInstanceHandle — C++ 持有脚本侧 class 实例的通用 handle
    // 跨 VM 通用:VM 内部将 int Id 映射到自己的对象引用
    //   Lua:    luaL_ref(LUA_REGISTRYINDEX)
    //   Python: PyObject* 存到 VM 内部 map
    //   TS:     JSValue + JS_DupValue
    //   C#:     GCHandle.Alloc(obj, Strong)
    //
    // Generation 字段(DualVM 热重载):
    //   VM 每次 swap 后 VmGeneration ++;handle 持有它生成时的 generation。
    //   跨 generation 的 invoke/release 立即失败(kVmSwapped),避免
    //   误用旧 handle 操作新 VM 的 registry。
    //
    // 跨进程语义:仅同进程;跨进程需要 class hash + 引用关系,
    //   是独立 spec 的范围,不在本 plan。
    class TScriptInstanceHandle {
        public:
        static constexpr int      InvalidId  = -1;
        static constexpr uint32_t InvalidGen = 0;

        TScriptInstanceHandle() = default;
        TScriptInstanceHandle(int InId, uint32 InGen) : Id(InId), Generation(InGen) {
        }

        bool IsValid() const {
            return Id != InvalidId;
        }
        int GetId() const {
            return Id;
        }
        uint32 GetGeneration() const {
            return Generation;
        }
        void SetId(int InId) {
            Id = InId;
        }
        void SetGeneration(uint32 InGen) {
            Generation = InGen;
        }

        // 检查 handle 是否属于当前活跃 VM 的 generation。
        // 用于跨 VM 边界处的 fail-fast 校验。
        bool Matches(uint32 CurrentGen) const {
            return IsValid() && Generation == CurrentGen;
        }

        private:
        int    Id         = InvalidId;
        uint32 Generation = InvalidGen;
    };

} // namespace mession::script