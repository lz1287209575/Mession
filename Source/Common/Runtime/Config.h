/**
 * @file Config.h
 * @brief MConfig —— 扁平配置文件(`KEY=VALUE`,支持 `#` 注释)与环境变量的读取工具。
 *
 * LoadFromFile() 逐行解析:先截掉 `#` 之后的内容,再按第一个 `=` 拆成
 * key/value,两侧空白(空格 / 制表符)都裁掉,key 为空的行忽略。不处理
 * 转义、多行值与 include —— 只适合环境 / 启动参数这类扁平配置,需要嵌套
 * 结构请用 Json.h。
 *
 * Get*() 系列在 key 缺失时一律返回调用方给的默认值,不抛异常也不报错。
 * 但各取值的容错范围并不一致:GetInt 走 atoi,越界不回退;GetU16 对越界值
 * 回退默认值;GetU32 / GetU64 只拒绝「一个数字都读不出」的输入,不做上界
 * 检查。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace MConfig {
    inline bool LoadFromFile(const MString& Path, TMap<MString, MString>& OutVars) {
        TIfstream File(Path);
        if (!File.is_open()) {
            return false;
        }

        MString Line;
        while (std::getline(File, Line)) {
            size_t CommentPos = Line.find('#');
            if (CommentPos != MString::npos) {
                Line = Line.substr(0, CommentPos);
            }

            size_t EqPos = Line.find('=');
            if (EqPos != MString::npos && EqPos > 0) {
                MString Key   = Line.substr(0, EqPos);
                MString Value = Line.substr(EqPos + 1);

                while (!Key.empty() && (Key.back() == ' ' || Key.back() == '\t')) {
                    Key.pop_back();
                }
                while (!Key.empty() && (Key.front() == ' ' || Key.front() == '\t')) {
                    Key.erase(0, 1);
                }
                while (!Value.empty() && (Value.back() == ' ' || Value.back() == '\t')) {
                    Value.pop_back();
                }
                while (!Value.empty() && (Value.front() == ' ' || Value.front() == '\t')) {
                    Value.erase(0, 1);
                }

                if (!Key.empty()) {
                    OutVars[Key] = Value;
                }
            }
        }
        return true;
    }

    inline MString GetEnv(const char* Name) {
        const char* Val = std::getenv(Name);
        return Val ? MString(Val) : MString();
    }

    inline int GetEnvInt(const char* Name, int Default) {
        const char* Val = std::getenv(Name);
        if (!Val || !Val[0]) {
            return Default;
        }
        return std::atoi(Val);
    }

    inline void ApplyEnvOverrides(TMap<MString, MString>& Vars, const TMap<MString, const char*>& EnvMap) {
        for (const auto& [Key, EnvName] : EnvMap) {
            MString Val = GetEnv(EnvName);
            if (!Val.empty()) {
                Vars[Key] = Val;
            }
        }
    }

    inline int32 GetInt(const TMap<MString, MString>& Vars, const MString& Key, int32 Default) {
        auto It = Vars.find(Key);
        if (It == Vars.end()) {
            return Default;
        }
        return std::atoi(It->second.c_str());
    }

    inline uint16 GetU16(const TMap<MString, MString>& Vars, const MString& Key, uint16 Default) {
        auto It = Vars.find(Key);
        if (It == Vars.end()) {
            return Default;
        }
        int Val = std::atoi(It->second.c_str());
        return (Val > 0 && Val <= 65535) ? static_cast<uint16>(Val) : Default;
    }

    inline uint32 GetU32(const TMap<MString, MString>& Vars, const MString& Key, uint32 Default) {
        auto It = Vars.find(Key);
        if (It == Vars.end()) {
            return Default;
        }
        const MString& S = It->second;
        if (S.empty()) {
            return Default;
        }
        char*         End = nullptr;
        unsigned long Val = std::strtoul(S.c_str(), &End, 10);
        if (End == S.c_str()) {
            return Default;
        }
        return static_cast<uint32>(Val);
    }

    inline uint64 GetU64(const TMap<MString, MString>& Vars, const MString& Key, uint64 Default) {
        auto It = Vars.find(Key);
        if (It == Vars.end()) {
            return Default;
        }
        const MString& S = It->second;
        if (S.empty()) {
            return Default;
        }
        char*              End = nullptr;
        unsigned long long Val = std::strtoull(S.c_str(), &End, 10);
        if (End == S.c_str()) {
            return Default;
        }
        return static_cast<uint64>(Val);
    }

    inline bool GetBool(const TMap<MString, MString>& Vars, const MString& Key, bool Default) {
        auto It = Vars.find(Key);
        if (It == Vars.end() || It->second.empty()) {
            return Default;
        }
        char c = It->second[0];
        if (c == '1') {
            return true;
        }
        if (c == '0') {
            return false;
        }
        if (c == 't' || c == 'T' || c == 'y' || c == 'Y') {
            return true;
        }
        if (c == 'f' || c == 'F' || c == 'n' || c == 'N') {
            return false;
        }
        return std::atoi(It->second.c_str()) != 0;
    }

    inline MString GetStr(const TMap<MString, MString>& Vars, const MString& Key, const MString& Default) {
        auto It = Vars.find(Key);
        return (It != Vars.end()) ? It->second : Default;
    }
} // namespace MConfig
