#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Script/Abstract/EScriptLanguage.h"
#include "Common/Script/Abstract/IScriptEngine.h"
#include "Common/Script/Abstract/IScriptRepl.h"
#include "Common/Script/Lua/LuaScriptState.h"

namespace mession::script::lua {

    class MLuaRepl : public mession::script::IScriptRepl {
        public:
        MLuaRepl();
        ~MLuaRepl() override;

        bool                             Connect(TSharedPtr<mession::script::IScriptEngine> Engine) override;
        bool                             ReadLine(MString& OutLine) override;
        void                             WriteLine(MStringView Line) override;
        TResult<MString>                 Eval(MStringView Code) override;
        void                             Interrupt() override;
        void                             Disconnect() override;
        mession::script::EScriptLanguage GetLanguage() const override {
            return mession::script::EScriptLanguage::Lua;
        }

        private:
        TUniquePtr<MLuaScriptState> State;
    };

} // namespace mession::script::lua