/**
 * @file MObjectAssetBinary.cpp
 * @brief MObjectAssetBinary 实现:文件头读写 + 字段记录编解码 + 对象装载。
 *
 * @detail 写入路径与读取路径共用同一套字段遍历(GatherSerializableProperties),
 *   保证"先父类后本类"的字段顺序在两侧一致;字段以 AssetFieldId 而非名字
 *   落盘,因此改属性名不影响已产出的资产文件。
 */
#include "Common/Runtime/Asset/MObjectAssetBinary.h"

#include "Common/Runtime/Asset/MObjectAssetJson.h"
#include "Common/Runtime/Object/Object.h"

namespace MObjectAssetBinary {
    namespace {
        /** @brief .mobj 文件魔数,位于文件最前面。 */
        constexpr uint8 GFileMagic[4] = {'M', 'O', 'B', 'J'};

        // ---- 定长小端读写:仅是对 AppendFixedLE/ReadFixedLE 的具名包装,
        // 让文件头字段宽度在代码里一眼可见。----

        void AppendUInt16LE(TByteArray& OutBytes, uint16 Value) {
            AppendFixedLE(OutBytes, Value);
        }

        void AppendUInt32LE(TByteArray& OutBytes, uint32 Value) {
            AppendFixedLE(OutBytes, Value);
        }

        bool ReadUInt16LE(const TByteArray& Bytes, size_t& Offset, uint16& OutValue) {
            return ReadFixedLE(Bytes, Offset, OutValue);
        }

        bool ReadUInt32LE(const TByteArray& Bytes, size_t& Offset, uint32& OutValue) {
            return ReadFixedLE(Bytes, Offset, OutValue);
        }

        /**
         * @brief 追加 20 字节文件头(magic + 各字段,均小端)。
         * @param Header 头内容(Version/Flags 各 2B,其余 4B)
         * @param OutBytes 输出缓冲(追加,不清空)
         */
        void BuildHeaderBytes(const SFileHeader& Header, TByteArray& OutBytes) {
            OutBytes.insert(OutBytes.end(), std::begin(GFileMagic), std::end(GFileMagic));
            AppendUInt16LE(OutBytes, Header.Version);
            AppendUInt16LE(OutBytes, Header.Flags);
            AppendUInt32LE(OutBytes, Header.RootAssetTypeId);
            AppendUInt32LE(OutBytes, static_cast<uint32>(Header.PayloadEncoding));
            AppendUInt32LE(OutBytes, Header.PayloadSize);
        }

        /**
         * @brief 递归收集可序列化属性,顺序为父类 → 本类,且每层保持注册顺序。
         * @param ClassMeta 起始类;为空直接返回
         * @param bAssetOnly true 时只收带 Asset 域的属性(资产路径),
         *   false 时收全部属性(MSTRUCT 路径)
         * @param OutProperties 输出(追加,不清空)
         *
         * @detail 该顺序同时决定二进制字段记录的顺序,读写两侧必须一致。
         */
        void GatherSerializableProperties(const MClass* ClassMeta, bool bAssetOnly, TVector<const MProperty*>& OutProperties) {
            if (!ClassMeta) {
                return;
            }

            GatherSerializableProperties(ClassMeta->GetParentClass(), bAssetOnly, OutProperties);
            for (const MProperty* Prop : ClassMeta->GetProperties()) {
                if (!Prop) {
                    continue;
                }
                if (bAssetOnly && !Prop->HasAnyDomains(EPropertyDomainFlags::Asset)) {
                    continue;
                }
                OutProperties.push_back(Prop);
            }
        }

        /**
         * @brief 把对象的可序列化属性编码成字段记录数组。
         * @param ClassMeta 目标类;与 ObjectData 任一为空即失败
         * @param ObjectData 对象实例(取其属性值)
         * @param bAssetOnly true 只编码 Asset 域属性
         * @param OutPayload 输出(会先 clear):字段数 + 每条[id(4B) + 类型(1B) + 长度 + 值]
         * @param OutError 失败原因,可空
         * @return true 成功
         *
         * @detail 逐字段调用 MProperty::ExportBinaryValue;记录顺序由
         *   GatherSerializableProperties 决定(父类在前)。字段名不落盘,
         *   读取方靠 AssetFieldId 定位属性。
         */
        bool EncodeFieldRecords(const MClass* ClassMeta, const void* ObjectData, bool bAssetOnly, TByteArray& OutPayload, MString* OutError) {
            if (!ClassMeta || !ObjectData) {
                if (OutError) {
                    *OutError = "asset_binary_encode_invalid_target";
                }
                return false;
            }

            OutPayload.clear();
            TVector<const MProperty*> Properties;
            GatherSerializableProperties(ClassMeta, bAssetOnly, Properties);
            AppendVarUInt(OutPayload, static_cast<uint64>(Properties.size()));

            for (const MProperty* Prop : Properties) {
                TByteArray FieldPayload;
                MString    FieldError;
                if (!Prop->ExportBinaryValue(ObjectData, FieldPayload, &FieldError)) {
                    if (OutError) {
                        *OutError = "asset_binary_encode_field_failed:" + Prop->Name + ":" + FieldError;
                    }
                    return false;
                }

                AppendUInt32LE(OutPayload, Prop->AssetFieldId);
                AppendByte(OutPayload, static_cast<uint8>(Prop->Type));
                AppendVarUInt(OutPayload, static_cast<uint64>(FieldPayload.size()));
                OutPayload.insert(OutPayload.end(), FieldPayload.begin(), FieldPayload.end());
            }

            return true;
        }

        /**
         * @brief 把字段记录数组解回对象。
         * @param ClassMeta 目标类;与 ObjectData 任一为空即失败
         * @param ObjectData 目标实例(已构造)
         * @param bAssetOnly true 时拒绝写入非 Asset 域属性
         * @param InPayload 负载:字段数 + 记录数组
         * @param OutError 失败原因,可空
         * @return true 成功
         *
         * @detail 容错策略:按 AssetFieldId 找不到对应属性、或被 bAssetOnly 排除的
         *   记录会整条跳过(只按记录中声明的长度推进游标),因此老文件里多出的
         *   字段不会导致失败;但同一字段的类型字节与反射类型不一致会立即报错,
         *   并且最后要求负载被恰好消费完。
         */
        bool DecodeFieldRecords(const MClass* ClassMeta, void* ObjectData, bool bAssetOnly, const TByteArray& InPayload, MString* OutError) {
            if (!ClassMeta || !ObjectData) {
                if (OutError) {
                    *OutError = "asset_binary_decode_invalid_target";
                }
                return false;
            }

            size_t Offset     = 0;
            uint64 FieldCount = 0;
            if (!ReadVarUInt(InPayload, Offset, FieldCount)) {
                if (OutError) {
                    *OutError = "asset_binary_decode_field_count_failed";
                }
                return false;
            }

            for (uint64 FieldIndex = 0; FieldIndex < FieldCount; ++FieldIndex) {
                uint32 AssetFieldId = 0;
                uint8  EncodedType  = 0;
                uint64 PayloadSize  = 0;
                if (!ReadUInt32LE(InPayload, Offset, AssetFieldId) || !ReadByte(InPayload, Offset, EncodedType) || !ReadVarUInt(InPayload, Offset, PayloadSize) || PayloadSize > static_cast<uint64>(InPayload.size() - Offset)) {
                    if (OutError) {
                        *OutError = "asset_binary_decode_field_header_failed";
                    }
                    return false;
                }

                const size_t PayloadOffset = Offset;
                Offset += static_cast<size_t>(PayloadSize);

                // 该 id 在反射里不存在(例如文件由更新版本的代码产出),整条跳过。
                MProperty* Prop = ClassMeta->FindPropertyByAssetFieldId(AssetFieldId);
                if (!Prop) {
                    continue;
                }
                if (bAssetOnly && !Prop->HasAnyDomains(EPropertyDomainFlags::Asset)) {
                    continue;
                }
                // 类型字节不匹配说明文件与当前反射定义不同源,继续解析只会得到脏数据。
                if (static_cast<uint8>(Prop->Type) != EncodedType) {
                    if (OutError) {
                        *OutError = "asset_binary_field_type_mismatch:" + Prop->Name;
                    }
                    return false;
                }

                TByteArray FieldPayload(InPayload.begin() + static_cast<std::ptrdiff_t>(PayloadOffset), InPayload.begin() + static_cast<std::ptrdiff_t>(PayloadOffset + static_cast<size_t>(PayloadSize)));

                MString FieldError;
                if (!Prop->ImportBinaryValue(ObjectData, FieldPayload, &FieldError)) {
                    if (OutError) {
                        *OutError = "asset_binary_decode_field_failed:" + Prop->Name + ":" + FieldError;
                    }
                    return false;
                }
            }

            // 尾部残留说明字段数与实际内容不符,属于格式错误而非可忽略的噪声。
            if (Offset != InPayload.size()) {
                if (OutError) {
                    *OutError = "asset_binary_decode_trailing_bytes";
                }
                return false;
            }

            return true;
        }

        /**
         * @brief 构造 TaggedFields 负载:单对象头 + 完整字段记录。
         * @param RootObject 根对象;与 GetClass() 任一为空即失败
         * @param OutPayload 输出(会先 clear)
         * @param OutError 失败原因,可空
         * @return true 成功
         *
         * @detail 负载里的"对象表"目前恒为固定值
         *   (对象数 1、根索引 1、本对象索引 1、父索引 0),即只支持单根对象、
         *   不落父子关系;读取侧 LoadTaggedPayload 会校验这四个值。
         */
        bool BuildTaggedPayload(const MObject* RootObject, TByteArray& OutPayload, MString* OutError) {
            if (!RootObject) {
                if (OutError) {
                    *OutError = "asset_binary_null_root";
                }
                return false;
            }

            MClass* ClassMeta = RootObject->GetClass();
            if (!ClassMeta) {
                if (OutError) {
                    *OutError = "asset_binary_missing_class";
                }
                return false;
            }

            TByteArray FieldPayload;
            if (!EncodeFieldRecords(ClassMeta, RootObject, true, FieldPayload, OutError)) {
                return false;
            }

            OutPayload.clear();
            AppendVarUInt(OutPayload, 1u); // 对象总数恒为 1
            AppendVarUInt(OutPayload, 1u); // 根对象索引
            AppendVarUInt(OutPayload, 1u); // 本对象索引
            AppendVarUInt(OutPayload, 0u); // 父对象索引:无父
            AppendString(OutPayload, RootObject->GetName());
            AppendUInt32LE(OutPayload, ClassMeta->GetAssetTypeId());
            OutPayload.insert(OutPayload.end(), FieldPayload.begin(), FieldPayload.end());
            return true;
        }

        /**
         * @brief 解析 TaggedFields 负载并实例化根对象。
         * @param Payload 负载(不含文件头)
         * @param Outer 非空则设为新对象的 Outer,否则把新对象加入 RootSet
         * @param OutError 失败原因,可空
         * @return 新对象,调用方持有;失败返回 nullptr
         *
         * @detail 失败路径都会释放已创建的对象,不会泄漏。仅支持单对象表
         *   (对象数/根索引/对象索引 == 1、父索引 == 0),其它形态直接报
         *   object_table_unsupported;类由负载里的 AssetTypeId 反查
         *   (MObject::FindClassByAssetTypeId),而不是文件头里的 RootAssetTypeId。
         */
        MObject* LoadTaggedPayload(const TByteArray& Payload, MObject* Outer, MString* OutError) {
            size_t Offset      = 0;
            uint64 ObjectCount = 0;
            uint64 RootIndex   = 0;
            uint64 ObjectIndex = 0;
            uint64 ParentIndex = 0;
            uint32 AssetTypeId = 0;
            if (!ReadVarUInt(Payload, Offset, ObjectCount) || !ReadVarUInt(Payload, Offset, RootIndex) || !ReadVarUInt(Payload, Offset, ObjectIndex) || !ReadVarUInt(Payload, Offset, ParentIndex)) {
                if (OutError) {
                    *OutError = "asset_binary_decode_object_header_failed";
                }
                return nullptr;
            }

            if (ObjectCount != 1u || RootIndex != 1u || ObjectIndex != 1u || ParentIndex != 0u) {
                if (OutError) {
                    *OutError = "asset_binary_object_table_unsupported";
                }
                return nullptr;
            }

            MString ObjectName;
            if (!ReadString(Payload, Offset, ObjectName) || !ReadUInt32LE(Payload, Offset, AssetTypeId)) {
                if (OutError) {
                    *OutError = "asset_binary_decode_object_meta_failed";
                }
                return nullptr;
            }

            MClass* ClassMeta = MObject::FindClassByAssetTypeId(AssetTypeId);
            if (!ClassMeta) {
                if (OutError) {
                    *OutError = "asset_binary_unknown_asset_type_id:" + std::to_string(AssetTypeId);
                }
                return nullptr;
            }

            MObject* RootObject = static_cast<MObject*>(ClassMeta->CreateInstance());
            if (!RootObject) {
                if (OutError) {
                    *OutError = "asset_binary_create_instance_failed:" + ClassMeta->GetName();
                }
                return nullptr;
            }

            RootObject->SetName(ObjectName);
            if (Outer) {
                RootObject->SetOuter(Outer);
            } else {
                // 无 Outer 时交给 RootSet 托管,调用方拿到的裸指针也因此不会悬空。
                RootObject->AddToRoot();
            }

            TByteArray FieldPayload(Payload.begin() + static_cast<std::ptrdiff_t>(Offset), Payload.end());
            MString    DecodeError;
            if (!DecodeFieldRecords(ClassMeta, RootObject, true, FieldPayload, &DecodeError)) {
                delete RootObject;
                if (OutError) {
                    *OutError = DecodeError;
                }
                return nullptr;
            }

            return RootObject;
        }
    } // namespace

    /** @brief MSTRUCT 导出:走通用字段记录编码,bAssetOnly=false(不限 Asset 域)。 */
    bool EncodeStructFields(const MClass* StructMeta, const void* StructData, TByteArray& OutData, MString* OutError) {
        return EncodeFieldRecords(StructMeta, StructData, false, OutData, OutError);
    }

    /** @brief MSTRUCT 导入:走通用字段记录解码,bAssetOnly=false(不限 Asset 域)。 */
    bool DecodeStructFields(const MClass* StructMeta, void* StructData, const TByteArray& InData, MString* OutError) {
        return DecodeFieldRecords(StructMeta, StructData, false, InData, OutError);
    }

    /**
     * @brief 组装完整 .mobj 文件:TaggedFields 负载 + 文件头。
     *
     * @detail RootAssetTypeId 取自对象实际类,读取时用于二次校验类型一致;
     *   reserve 按"魔数 + 头定长部分 + 负载"精确预留,避免多次扩容。
     */
    bool BuildFromObject(const MObject* RootObject, TByteArray& OutBytes, MString* OutError) {
        if (!RootObject) {
            if (OutError) {
                *OutError = "asset_binary_null_root";
            }
            return false;
        }

        MClass* ClassMeta = RootObject->GetClass();
        if (!ClassMeta) {
            if (OutError) {
                *OutError = "asset_binary_missing_class";
            }
            return false;
        }

        TByteArray Payload;
        if (!BuildTaggedPayload(RootObject, Payload, OutError)) {
            return false;
        }

        SFileHeader Header;
        Header.RootAssetTypeId = ClassMeta->GetAssetTypeId();
        Header.PayloadEncoding = EPayloadEncoding::TaggedFields;
        Header.PayloadSize     = static_cast<uint32>(Payload.size());

        OutBytes.clear();
        OutBytes.reserve(sizeof(GFileMagic) + 2 + 2 + 4 + 4 + 4 + Payload.size());
        BuildHeaderBytes(Header, OutBytes);
        OutBytes.insert(OutBytes.end(), Payload.begin(), Payload.end());
        return true;
    }

    /**
     * @brief 由 JSON 文本产出 .mobj(先造对象再编码,对象用完即删)。
     *
     * @detail 失败时 OutBytes 可能已被部分写入,调用方不应使用。
     */
    bool BuildFromJson(const MString& JsonText, TByteArray& OutBytes, MString* OutError) {
        MObject* RootObject = MObjectAssetJson::ImportAssetObjectFromJson(JsonText, nullptr, OutError);
        if (!RootObject) {
            return false;
        }

        const bool bSuccess = BuildFromObject(RootObject, OutBytes, OutError);
        delete RootObject;
        return bSuccess;
    }

    /**
     * @brief 校验 magics/版本/负载编码/长度自洽性,并回传负载偏移。
     *
     * @detail 校验顺序:总长度足够 → magic → 各字段可读 → Version == FileVersion
     *   → PayloadEncoding 属于 {JsonBridge, TaggedFields} → PayloadSize 不越界。
     *   Flags 目前只写入不校验,预留给后续格式扩展。
     */
    bool ReadHeader(const TByteArray& Bytes, SFileHeader& OutHeader, size_t& OutPayloadOffset, MString* OutError) {
        OutHeader        = SFileHeader{};
        OutPayloadOffset = 0;

        if (Bytes.size() < sizeof(GFileMagic) + 2 + 2 + 4 + 4 + 4) {
            if (OutError) {
                *OutError = "asset_binary_truncated_header";
            }
            return false;
        }

        if (!std::equal(std::begin(GFileMagic), std::end(GFileMagic), Bytes.begin())) {
            if (OutError) {
                *OutError = "asset_binary_bad_magic";
            }
            return false;
        }

        size_t Offset          = sizeof(GFileMagic);
        uint32 PayloadEncoding = 0;
        if (!ReadUInt16LE(Bytes, Offset, OutHeader.Version) || !ReadUInt16LE(Bytes, Offset, OutHeader.Flags) || !ReadUInt32LE(Bytes, Offset, OutHeader.RootAssetTypeId) || !ReadUInt32LE(Bytes, Offset, PayloadEncoding) ||
            !ReadUInt32LE(Bytes, Offset, OutHeader.PayloadSize)) {
            if (OutError) {
                *OutError = "asset_binary_read_header_failed";
            }
            return false;
        }

        if (OutHeader.Version != FileVersion) {
            if (OutError) {
                *OutError = "asset_binary_unsupported_version:" + std::to_string(OutHeader.Version);
            }
            return false;
        }

        OutHeader.PayloadEncoding = static_cast<EPayloadEncoding>(PayloadEncoding);
        if (OutHeader.PayloadEncoding != EPayloadEncoding::JsonBridge && OutHeader.PayloadEncoding != EPayloadEncoding::TaggedFields) {
            if (OutError) {
                *OutError = "asset_binary_unsupported_payload_encoding:" + std::to_string(PayloadEncoding);
            }
            return false;
        }

        if (Offset + OutHeader.PayloadSize > Bytes.size()) {
            if (OutError) {
                *OutError = "asset_binary_truncated_payload";
            }
            return false;
        }

        OutPayloadOffset = Offset;
        return true;
    }

    /**
     * @brief 直接取出 JsonBridge 负载文本,不构造任何对象。
     *
     * @detail 适用于只关心源 JSON 的工具链(如资产 diff/校验),避免为读文本
     *   付一次反序列化代价。
     */
    bool ExtractPayloadJson(const TByteArray& Bytes, MString& OutJson, SFileHeader* OutHeader, MString* OutError) {
        SFileHeader Header;
        size_t      PayloadOffset = 0;
        if (!ReadHeader(Bytes, Header, PayloadOffset, OutError)) {
            return false;
        }

        if (Header.PayloadEncoding != EPayloadEncoding::JsonBridge) {
            if (OutError) {
                *OutError = "asset_binary_payload_not_json_bridge";
            }
            return false;
        }

        OutJson.assign(reinterpret_cast<const char*>(Bytes.data() + PayloadOffset), static_cast<size_t>(Header.PayloadSize));

        if (OutHeader) {
            *OutHeader = Header;
        }
        return true;
    }

    /**
     * @brief 按负载编码分派到 JSON 路径或 TaggedFields 路径并还原对象。
     *
     * @detail 两条路径都会用文件头的 RootAssetTypeId 复核实际类;不一致时销毁
     *   已建对象,避免把"内容与头不符"的文件交给业务层。
     */
    MObject* LoadFromBytes(const TByteArray& Bytes, MObject* Outer, MString* OutError) {
        SFileHeader Header;
        size_t      PayloadOffset = 0;
        if (!ReadHeader(Bytes, Header, PayloadOffset, OutError)) {
            return nullptr;
        }

        if (Header.PayloadEncoding == EPayloadEncoding::JsonBridge) {
            MString JsonPayload(reinterpret_cast<const char*>(Bytes.data() + PayloadOffset), static_cast<size_t>(Header.PayloadSize));

            MObject* RootObject = MObjectAssetJson::ImportAssetObjectFromJson(JsonPayload, Outer, OutError);
            if (!RootObject) {
                return nullptr;
            }

            MClass* RootClass = RootObject->GetClass();
            if (!RootClass || RootClass->GetAssetTypeId() != Header.RootAssetTypeId) {
                if (OutError) {
                    *OutError = "asset_binary_root_type_mismatch";
                }
                delete RootObject;
                return nullptr;
            }

            return RootObject;
        }

        TByteArray Payload(Bytes.begin() + static_cast<std::ptrdiff_t>(PayloadOffset), Bytes.begin() + static_cast<std::ptrdiff_t>(PayloadOffset + static_cast<size_t>(Header.PayloadSize)));
        MObject*   RootObject = LoadTaggedPayload(Payload, Outer, OutError);
        if (!RootObject) {
            return nullptr;
        }

        MClass* RootClass = RootObject->GetClass();
        if (!RootClass || RootClass->GetAssetTypeId() != Header.RootAssetTypeId) {
            if (OutError) {
                *OutError = "asset_binary_root_type_mismatch";
            }
            delete RootObject;
            return nullptr;
        }

        return RootObject;
    }
} // namespace MObjectAssetBinary
