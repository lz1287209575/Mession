/**
 * @file TScriptInstanceHandle.h
 * @brief TScriptInstanceHandle —— 脚本 class 实例句柄(跨 VM 通用的纯值类型)声明。
 *
 * @detail 只保存 VM 内部对象引用(int Id)与生成时的 VmGeneration,
 *   不持有任何资源;具体映射与校验语义见下方类的说明。
 */
#pragma once

#include "Common/Runtime/MLib.h"

namespace mession::script {

    /**
     * @brief TScriptInstanceHandle - C++ 侧持有脚本 class 实例的通用句柄(跨 VM 通用)。
     *
     * @detail VM 内部把 int Id 映射到自己的对象引用:
     *   Lua:    luaL_ref(LUA_REGISTRYINDEX)
     *   Python: PyObject* 存到 VM 内部 map
     *   TS:     JSValue + JS_DupValue
     *   C#:     GCHandle.Alloc(obj, Strong)
     *
     * Generation 字段(DualVM 热重载):VM 每次 swap 后 VmGeneration ++;handle 记录
     *   自己生成时的 generation。Matches 用于在跨 VM 边界做 fail-fast 校验,避免拿旧
     *   handle 去操作新 VM 的 registry。
     *   NOTE: 目前只有 MLuaProxyActor 的钩子路径调用了 Matches;
     *   MLuaEngine::InvokeInstanceMethod / ReleaseInstance 尚未做 generation 校验
     *   (旧 handle 通常因 registry ref 失效而报 kInstanceReleased)。
     *
     * 跨进程语义:仅限同进程;跨进程需要 class hash + 引用关系,属独立 spec 范围。
     *
     * 纯值类型,可自由拷贝;不持有任何资源,析构无副作用。
     */
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

        /**
         * @brief Matches - 判断本 handle 是否属于给定 generation 的活跃 VM。
         * @param CurrentGen 当前 VM 的 generation(通常取 MLuaEngine::GetVmGeneration)
         * @return true 表示有效且 generation 相同;用于跨 VM 边界 fail-fast
         */
        bool Matches(uint32 CurrentGen) const {
            return IsValid() && Generation == CurrentGen;
        }

        private:
        int    Id         = InvalidId;
        uint32 Generation = InvalidGen;
    };

} // namespace mession::script