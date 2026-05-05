#include "Tools/MObjectEditorService/Http/EditorApiController.h"

#include "Common/Runtime/Json.h"
#include "Tools/MObjectEditorService/Core/EditorPaths.h"
#include "Tools/MObjectEditorService/Documents/MonsterConfigDocument.h"
#include "Tools/MObjectEditorService/Validation/MonsterConfigValidator.h"

#include <filesystem>
#include <iterator>
#include <limits>

namespace
{
namespace fs = std::filesystem;

constexpr const char* EditorWebRoot = "Source/Tools/MObjectEditorService/Web";

struct FMonsterConfigTableRow
{
    FEditorAssetIdentity Identity;
    FEditorAssetPathSet Paths;
    FMonsterConfigEditorModel Model;
    TVector<FValidationIssue> Issues;
    bool bDirty = false;
    bool bLoadSucceeded = false;
    MString LoadError;
};

struct FParsedMonsterConfigDocument
{
    FEditorAssetIdentity Identity;
    FMonsterConfigEditorModel Model;
    MString PreviousSourcePath;
};

bool HasValidationErrors(const TVector<FValidationIssue>& Issues)
{
    for (const FValidationIssue& Issue : Issues)
    {
        if (Issue.Severity == EValidationSeverity::Error)
        {
            return true;
        }
    }
    return false;
}

const MJsonValue* FindField(const MJsonValue& ObjectValue, const char* FieldName)
{
    if (!ObjectValue.IsObject())
    {
        return nullptr;
    }

    const auto It = ObjectValue.ObjectValue.find(FieldName);
    if (It == ObjectValue.ObjectValue.end())
    {
        return nullptr;
    }
    return &It->second;
}

bool StartsWith(const MString& Value, const MString& Prefix)
{
    return Value.size() >= Prefix.size() && Value.compare(0, Prefix.size(), Prefix) == 0;
}

MString SeverityToString(EValidationSeverity Severity)
{
    switch (Severity)
    {
    case EValidationSeverity::Info:
        return "info";
    case EValidationSeverity::Warning:
        return "warning";
    case EValidationSeverity::Error:
    default:
        return "error";
    }
}

MString StatusTextFromCode(int StatusCode)
{
    switch (StatusCode)
    {
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "OK";
    }
}

MString GuessContentType(const fs::path& FilePath)
{
    const MString Extension = FilePath.extension().generic_string();
    if (Extension == ".html")
    {
        return "text/html; charset=utf-8";
    }
    if (Extension == ".css")
    {
        return "text/css; charset=utf-8";
    }
    if (Extension == ".js")
    {
        return "application/javascript; charset=utf-8";
    }
    if (Extension == ".json")
    {
        return "application/json; charset=utf-8";
    }
    if (Extension == ".svg")
    {
        return "image/svg+xml";
    }
    return "text/plain; charset=utf-8";
}

FEditorHttpResponse MakeJsonResponse(const MString& Body, int StatusCode = 200)
{
    FEditorHttpResponse Response;
    Response.StatusCode = StatusCode;
    Response.StatusText = StatusTextFromCode(StatusCode);
    Response.ContentType = "application/json; charset=utf-8";
    Response.Headers["Cache-Control"] = "no-store";
    Response.Body = Body;
    return Response;
}

FEditorHttpResponse MakeTextResponse(const MString& Body, const MString& ContentType, int StatusCode = 200)
{
    FEditorHttpResponse Response;
    Response.StatusCode = StatusCode;
    Response.StatusText = StatusTextFromCode(StatusCode);
    Response.ContentType = ContentType;
    Response.Headers["Cache-Control"] = "no-store";
    Response.Body = Body;
    return Response;
}

FEditorHttpResponse MakeErrorResponse(int StatusCode, const MString& ErrorCode, const MString& Message)
{
    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(false);
    Writer.Key("error");
    Writer.Value(ErrorCode);
    Writer.Key("message");
    Writer.Value(Message);
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString(), StatusCode);
}

void WriteIssueArray(MJsonWriter& Writer, const TVector<FValidationIssue>& Issues)
{
    Writer.BeginArray();
    for (const FValidationIssue& Issue : Issues)
    {
        Writer.BeginObject();
        Writer.Key("severity");
        Writer.Value(SeverityToString(Issue.Severity));
        Writer.Key("fieldPath");
        Writer.Value(Issue.FieldPath);
        Writer.Key("code");
        Writer.Value(Issue.Code);
        Writer.Key("message");
        Writer.Value(Issue.Message);
        Writer.EndObject();
    }
    Writer.EndArray();
}

void WritePathSet(MJsonWriter& Writer, const FEditorAssetPathSet& PathSet)
{
    Writer.BeginObject();
    Writer.Key("sourcePath");
    Writer.Value(PathSet.SourcePath);
    Writer.Key("exportJsonPath");
    Writer.Value(PathSet.ExportJsonPath);
    Writer.Key("exportMobPath");
    Writer.Value(PathSet.ExportMobPath);
    Writer.Key("exportRoundTripPath");
    Writer.Value(PathSet.ExportRoundTripPath);
    Writer.Key("publishMobPath");
    Writer.Value(PathSet.PublishMobPath);
    Writer.EndObject();
}

void WriteIdentity(MJsonWriter& Writer, const FEditorAssetIdentity& Identity)
{
    Writer.BeginObject();
    Writer.Key("assetName");
    Writer.Value(Identity.AssetName);
    Writer.Key("categoryPath");
    Writer.Value(Identity.CategoryPath);
    Writer.Key("sourcePath");
    Writer.Value(Identity.SourcePath);
    Writer.EndObject();
}

void WriteModel(MJsonWriter& Writer, const FMonsterConfigEditorModel& Model)
{
    Writer.BeginObject();
    Writer.Key("monsterTemplateId");
    Writer.Value(static_cast<uint64>(Model.MonsterTemplateId));
    Writer.Key("debugName");
    Writer.Value(Model.DebugName);
    Writer.Key("spawnParams");
    Writer.BeginObject();
    Writer.Key("sceneId");
    Writer.Value(static_cast<uint64>(Model.SpawnParams.SceneId));
    Writer.Key("currentHealth");
    Writer.Value(static_cast<uint64>(Model.SpawnParams.CurrentHealth));
    Writer.Key("maxHealth");
    Writer.Value(static_cast<uint64>(Model.SpawnParams.MaxHealth));
    Writer.Key("attackPower");
    Writer.Value(static_cast<uint64>(Model.SpawnParams.AttackPower));
    Writer.Key("defensePower");
    Writer.Value(static_cast<uint64>(Model.SpawnParams.DefensePower));
    Writer.Key("primarySkillId");
    Writer.Value(static_cast<uint64>(Model.SpawnParams.PrimarySkillId));
    Writer.Key("experienceReward");
    Writer.Value(static_cast<uint64>(Model.SpawnParams.ExperienceReward));
    Writer.Key("goldReward");
    Writer.Value(static_cast<uint64>(Model.SpawnParams.GoldReward));
    Writer.EndObject();
    Writer.Key("skillIds");
    Writer.BeginArray();
    for (uint32 SkillId : Model.SkillIds)
    {
        Writer.Value(static_cast<uint64>(SkillId));
    }
    Writer.EndArray();
    Writer.EndObject();
}

void WriteDocument(MJsonWriter& Writer, const MMonsterConfigDocument& Document)
{
    Writer.BeginObject();
    Writer.Key("assetType");
    Writer.Value("monster-config");
    Writer.Key("identity");
    WriteIdentity(Writer, Document.GetIdentity());
    Writer.Key("paths");
    WritePathSet(Writer, MEditorPaths::BuildMonsterConfigPaths(Document.GetIdentity()));
    Writer.Key("dirty");
    Writer.Value(Document.IsDirty());
    Writer.Key("model");
    WriteModel(Writer, Document.GetModel());
    Writer.EndObject();
}

void WriteTableRow(MJsonWriter& Writer, const FMonsterConfigTableRow& Row)
{
    Writer.BeginObject();
    Writer.Key("identity");
    WriteIdentity(Writer, Row.Identity);
    Writer.Key("paths");
    WritePathSet(Writer, Row.Paths);
    Writer.Key("dirty");
    Writer.Value(Row.bDirty);
    Writer.Key("loadSucceeded");
    Writer.Value(Row.bLoadSucceeded);
    Writer.Key("loadError");
    Writer.Value(Row.LoadError);
    Writer.Key("hasErrors");
    Writer.Value(HasValidationErrors(Row.Issues));
    Writer.Key("issues");
    WriteIssueArray(Writer, Row.Issues);
    Writer.Key("model");
    WriteModel(Writer, Row.Model);
    Writer.EndObject();
}

bool TryGetString(const MJsonValue& ObjectValue, const char* FieldName, MString& OutValue)
{
    const MJsonValue* Value = FindField(ObjectValue, FieldName);
    if (!Value || !Value->IsString())
    {
        return false;
    }
    OutValue = Value->StringValue;
    return true;
}

bool TryGetBool(const MJsonValue& ObjectValue, const char* FieldName, bool& OutValue)
{
    const MJsonValue* Value = FindField(ObjectValue, FieldName);
    if (!Value || !Value->IsBool())
    {
        return false;
    }
    OutValue = Value->BoolValue;
    return true;
}

bool TryGetUInt32(const MJsonValue& ObjectValue, const char* FieldName, uint32& OutValue)
{
    const MJsonValue* Value = FindField(ObjectValue, FieldName);
    if (!Value || !Value->IsNumber())
    {
        return false;
    }
    if (Value->NumberValue < 0.0 || Value->NumberValue > static_cast<double>((std::numeric_limits<uint32>::max)()))
    {
        return false;
    }

    OutValue = static_cast<uint32>(Value->NumberValue);
    return true;
}

bool TryGetObject(const MJsonValue& ObjectValue, const char* FieldName, const MJsonValue*& OutValue)
{
    OutValue = FindField(ObjectValue, FieldName);
    return OutValue && OutValue->IsObject();
}

bool TryGetArray(const MJsonValue& ObjectValue, const char* FieldName, const MJsonValue*& OutValue)
{
    OutValue = FindField(ObjectValue, FieldName);
    return OutValue && OutValue->IsArray();
}

bool TryReadTextFile(const fs::path& FilePath, MString& OutText)
{
    TIfstream Input(FilePath, std::ios::binary);
    if (!Input.is_open())
    {
        return false;
    }

    OutText.assign(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>());
    return true;
}

bool TryResolveWebFilePath(const MString& RequestPath, fs::path& OutFilePath)
{
    fs::path RootPath = fs::path(EditorWebRoot).lexically_normal();
    MString RelativePath = (RequestPath == "/") ? "index.html" : RequestPath.substr(1);
    if (!RelativePath.empty() && RelativePath.back() == '/')
    {
        RelativePath += "index.html";
    }

    const fs::path CandidatePath = (RootPath / fs::path(RelativePath)).lexically_normal();
    const MString RootString = RootPath.generic_string();
    const MString CandidateString = CandidatePath.generic_string();
    if (CandidateString != RootString && !StartsWith(CandidateString, RootString + "/"))
    {
        return false;
    }

    OutFilePath = CandidatePath;
    return true;
}

bool TryLoadMonsterConfigTableRow(const FEditorAssetIdentity& Identity, FMonsterConfigTableRow& OutRow)
{
    OutRow.Identity = Identity;
    OutRow.Paths = MEditorPaths::BuildMonsterConfigPaths(Identity);

    MMonsterConfigDocument Document;
    MString Error;
    if (!Document.LoadFromFile(Identity.SourcePath, Error))
    {
        OutRow.bLoadSucceeded = false;
        OutRow.LoadError = Error;
        return false;
    }

    OutRow.Model = Document.GetModel();
    OutRow.Issues = MMonsterConfigValidator::Validate(OutRow.Model);
    OutRow.bLoadSucceeded = true;
    return true;
}

bool TryParseIdentity(const MJsonValue& RootValue, FEditorAssetIdentity& OutIdentity, MString& OutError)
{
    const MJsonValue* IdentityValue = nullptr;
    if (!TryGetObject(RootValue, "identity", IdentityValue))
    {
        OutError = "identity_required";
        return false;
    }

    MString SourcePath;
    if (TryGetString(*IdentityValue, "sourcePath", SourcePath) && !SourcePath.empty())
    {
        MString ParseError;
        if (!MEditorPaths::TryParseMonsterConfigIdentityFromSourcePath(SourcePath, OutIdentity, &ParseError))
        {
            OutError = ParseError;
            return false;
        }
    }

    MString AssetName;
    if (TryGetString(*IdentityValue, "assetName", AssetName) && !AssetName.empty())
    {
        OutIdentity.AssetName = AssetName;
    }

    MString CategoryPath;
    if (TryGetString(*IdentityValue, "categoryPath", CategoryPath) && !CategoryPath.empty())
    {
        OutIdentity.CategoryPath = MEditorPaths::NormalizeCategoryPath(CategoryPath);
    }

    if (OutIdentity.AssetName.empty())
    {
        OutError = "asset_name_required";
        return false;
    }
    if (OutIdentity.CategoryPath.empty())
    {
        OutError = "category_path_required";
        return false;
    }

    OutIdentity.SourcePath = MEditorPaths::BuildMonsterConfigPaths(OutIdentity).SourcePath;
    return true;
}

bool TryParseModel(const MJsonValue& RootValue, FMonsterConfigEditorModel& OutModel, MString& OutError)
{
    const MJsonValue* ModelValue = nullptr;
    if (!TryGetObject(RootValue, "model", ModelValue))
    {
        OutError = "model_required";
        return false;
    }

    TryGetUInt32(*ModelValue, "monsterTemplateId", OutModel.MonsterTemplateId);
    TryGetString(*ModelValue, "debugName", OutModel.DebugName);

    const MJsonValue* SpawnParamsValue = nullptr;
    if (!TryGetObject(*ModelValue, "spawnParams", SpawnParamsValue))
    {
        OutError = "spawn_params_required";
        return false;
    }

    TryGetUInt32(*SpawnParamsValue, "sceneId", OutModel.SpawnParams.SceneId);
    TryGetUInt32(*SpawnParamsValue, "currentHealth", OutModel.SpawnParams.CurrentHealth);
    TryGetUInt32(*SpawnParamsValue, "maxHealth", OutModel.SpawnParams.MaxHealth);
    TryGetUInt32(*SpawnParamsValue, "attackPower", OutModel.SpawnParams.AttackPower);
    TryGetUInt32(*SpawnParamsValue, "defensePower", OutModel.SpawnParams.DefensePower);
    TryGetUInt32(*SpawnParamsValue, "primarySkillId", OutModel.SpawnParams.PrimarySkillId);
    TryGetUInt32(*SpawnParamsValue, "experienceReward", OutModel.SpawnParams.ExperienceReward);
    TryGetUInt32(*SpawnParamsValue, "goldReward", OutModel.SpawnParams.GoldReward);

    OutModel.SkillIds.clear();
    const MJsonValue* SkillIdsValue = nullptr;
    if (TryGetArray(*ModelValue, "skillIds", SkillIdsValue))
    {
        for (const MJsonValue& SkillValue : SkillIdsValue->ArrayValue)
        {
            if (!SkillValue.IsNumber())
            {
                OutError = "skill_ids_invalid";
                return false;
            }
            if (SkillValue.NumberValue < 0.0 || SkillValue.NumberValue > static_cast<double>((std::numeric_limits<uint32>::max)()))
            {
                OutError = "skill_ids_out_of_range";
                return false;
            }

            OutModel.SkillIds.push_back(static_cast<uint32>(SkillValue.NumberValue));
        }
    }

    OutModel.SpawnParams.MonsterTemplateId = OutModel.MonsterTemplateId;
    OutModel.SpawnParams.DebugName = OutModel.DebugName;
    return true;
}

bool TryParseBodyJson(const FEditorHttpRequest& Request, MJsonValue& OutValue, MString& OutError)
{
    if (Request.Body.empty())
    {
        OutError = "request_body_required";
        return false;
    }

    return MJsonReader::Parse(Request.Body, OutValue, OutError);
}

bool TryParseDocumentArray(const MJsonValue& RootValue, TVector<FParsedMonsterConfigDocument>& OutDocuments, MString& OutError)
{
    const MJsonValue* DocumentsValue = nullptr;
    if (!TryGetArray(RootValue, "documents", DocumentsValue))
    {
        OutError = "documents_required";
        return false;
    }

    OutDocuments.clear();
    for (const MJsonValue& DocumentValue : DocumentsValue->ArrayValue)
    {
        if (!DocumentValue.IsObject())
        {
            OutError = "document_entry_invalid";
            return false;
        }

        FParsedMonsterConfigDocument ParsedDocument;
        if (!TryParseIdentity(DocumentValue, ParsedDocument.Identity, OutError))
        {
            return false;
        }
        if (!TryParseModel(DocumentValue, ParsedDocument.Model, OutError))
        {
            return false;
        }
        TryGetString(DocumentValue, "previousSourcePath", ParsedDocument.PreviousSourcePath);

        OutDocuments.push_back(std::move(ParsedDocument));
    }

    return true;
}

bool TryParseSourcePathArray(const MJsonValue& RootValue, TVector<FEditorAssetIdentity>& OutAssets, MString& OutError)
{
    const MJsonValue* SourcePathsValue = nullptr;
    if (!TryGetArray(RootValue, "sourcePaths", SourcePathsValue))
    {
        OutError = "source_paths_required";
        return false;
    }

    OutAssets.clear();
    for (const MJsonValue& EntryValue : SourcePathsValue->ArrayValue)
    {
        if (!EntryValue.IsString() || EntryValue.StringValue.empty())
        {
            OutError = "source_path_invalid";
            return false;
        }

        FEditorAssetIdentity Identity;
        if (!MEditorPaths::TryParseMonsterConfigIdentityFromSourcePath(EntryValue.StringValue, Identity, &OutError))
        {
            return false;
        }
        OutAssets.push_back(std::move(Identity));
    }

    return true;
}

void WriteIssuesResult(MJsonWriter& Writer, const TVector<FValidationIssue>& Issues)
{
    Writer.Key("hasErrors");
    Writer.Value(HasValidationErrors(Issues));
    Writer.Key("issues");
    WriteIssueArray(Writer, Issues);
}
}

FEditorHttpResponse MEditorApiController::HandleRequest(const FEditorHttpRequest& Request)
{
    if (StartsWith(Request.Path, "/api/"))
    {
        if (Request.Path == "/api/status")
        {
            if (Request.Method != EEditorHttpMethod::Get)
            {
                return MakeErrorResponse(405, "method_not_allowed", "Use GET for /api/status");
            }
            return HandleStatus();
        }

        if (Request.Path == "/api/assets/monster-configs")
        {
            if (Request.Method != EEditorHttpMethod::Get)
            {
                return MakeErrorResponse(405, "method_not_allowed", "Use GET for /api/assets/monster-configs");
            }
            return HandleListMonsterConfigs();
        }

        if (Request.Path == "/api/monster-configs/table")
        {
            if (Request.Method != EEditorHttpMethod::Get)
            {
                return MakeErrorResponse(405, "method_not_allowed", "Use GET for /api/monster-configs/table");
            }
            return HandleMonsterConfigTable();
        }

        if (Request.Path == "/api/monster-config")
        {
            if (Request.Method != EEditorHttpMethod::Get)
            {
                return MakeErrorResponse(405, "method_not_allowed", "Use GET for /api/monster-config");
            }
            return HandleOpenMonsterConfig(Request);
        }

        if (Request.Path == "/api/monster-config/save")
        {
            if (Request.Method != EEditorHttpMethod::Post)
            {
                return MakeErrorResponse(405, "method_not_allowed", "Use POST for /api/monster-config/save");
            }
            return HandleSaveMonsterConfig(Request);
        }

        if (Request.Path == "/api/monster-configs/batch-save")
        {
            if (Request.Method != EEditorHttpMethod::Post)
            {
                return MakeErrorResponse(405, "method_not_allowed", "Use POST for /api/monster-configs/batch-save");
            }
            return HandleBatchSaveMonsterConfigs(Request);
        }

        if (Request.Path == "/api/monster-configs/delete")
        {
            if (Request.Method != EEditorHttpMethod::Post)
            {
                return MakeErrorResponse(405, "method_not_allowed", "Use POST for /api/monster-configs/delete");
            }
            return HandleDeleteMonsterConfigs(Request);
        }

        if (Request.Path == "/api/monster-config/validate")
        {
            if (Request.Method != EEditorHttpMethod::Post)
            {
                return MakeErrorResponse(405, "method_not_allowed", "Use POST for /api/monster-config/validate");
            }
            return HandleValidateMonsterConfig(Request);
        }

        if (Request.Path == "/api/monster-config/export")
        {
            if (Request.Method != EEditorHttpMethod::Post)
            {
                return MakeErrorResponse(405, "method_not_allowed", "Use POST for /api/monster-config/export");
            }
            return HandleExportMonsterConfig(Request);
        }

        return MakeErrorResponse(404, "route_not_found", "Route does not exist");
    }

    if (Request.Method != EEditorHttpMethod::Get)
    {
        return MakeErrorResponse(405, "method_not_allowed", "Static resources only support GET");
    }

    return HandleStaticFile(Request);
}

FEditorHttpResponse MEditorApiController::HandleStatus() const
{
    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(true);
    Writer.Key("service");
    Writer.Value("MObjectEditorService");
    Writer.Key("assetType");
    Writer.Value("monster-config");
    Writer.Key("webRoot");
    Writer.Value(EditorWebRoot);
    Writer.Key("endpoints");
    Writer.BeginArray();
    Writer.Value("GET /api/status");
    Writer.Value("GET /api/assets/monster-configs");
    Writer.Value("GET /api/monster-configs/table");
    Writer.Value("GET /api/monster-config?sourcePath=EditorAssets/...");
    Writer.Value("POST /api/monster-config/save");
    Writer.Value("POST /api/monster-configs/batch-save");
    Writer.Value("POST /api/monster-configs/delete");
    Writer.Value("POST /api/monster-config/validate");
    Writer.Value("POST /api/monster-config/export");
    Writer.EndArray();
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString());
}

FEditorHttpResponse MEditorApiController::HandleStaticFile(const FEditorHttpRequest& Request) const
{
    fs::path FilePath;
    if (!TryResolveWebFilePath(Request.Path, FilePath))
    {
        return MakeErrorResponse(404, "static_file_not_found", "Static resource path is invalid");
    }

    MString FileText;
    if (!TryReadTextFile(FilePath, FileText))
    {
        return MakeErrorResponse(404, "static_file_not_found", "Static resource was not found");
    }

    return MakeTextResponse(FileText, GuessContentType(FilePath));
}

FEditorHttpResponse MEditorApiController::HandleListMonsterConfigs() const
{
    MString Error;
    const TVector<FEditorAssetIdentity> Assets = App.ListMonsterConfigs(Error);
    if (!Error.empty())
    {
        return MakeErrorResponse(500, "list_assets_failed", Error);
    }

    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(true);
    Writer.Key("assets");
    Writer.BeginArray();
    for (const FEditorAssetIdentity& Asset : Assets)
    {
        Writer.BeginObject();
        Writer.Key("identity");
        WriteIdentity(Writer, Asset);
        Writer.Key("paths");
        WritePathSet(Writer, MEditorPaths::BuildMonsterConfigPaths(Asset));
        Writer.EndObject();
    }
    Writer.EndArray();
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString());
}

FEditorHttpResponse MEditorApiController::HandleMonsterConfigTable() const
{
    MString Error;
    const TVector<FEditorAssetIdentity> Assets = App.ListMonsterConfigs(Error);
    if (!Error.empty())
    {
        return MakeErrorResponse(500, "table_assets_failed", Error);
    }

    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(true);
    Writer.Key("rows");
    Writer.BeginArray();
    for (const FEditorAssetIdentity& Asset : Assets)
    {
        FMonsterConfigTableRow Row;
        TryLoadMonsterConfigTableRow(Asset, Row);
        WriteTableRow(Writer, Row);
    }
    Writer.EndArray();
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString());
}

FEditorHttpResponse MEditorApiController::HandleOpenMonsterConfig(const FEditorHttpRequest& Request)
{
    const auto It = Request.QueryParams.find("sourcePath");
    if (It == Request.QueryParams.end() || It->second.empty())
    {
        return MakeErrorResponse(400, "source_path_required", "sourcePath query parameter is required");
    }

    MString Error;
    if (!App.OpenMonsterConfig(It->second, Error))
    {
        if (Error.rfind("editor_asset_open_failed:", 0) == 0)
        {
            return MakeErrorResponse(404, "asset_not_found", Error);
        }
        return MakeErrorResponse(400, "open_asset_failed", Error);
    }

    const MMonsterConfigDocument* Document = App.GetCurrentDocument();
    if (!Document)
    {
        return MakeErrorResponse(500, "document_missing", "Document was not loaded");
    }

    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(true);
    Writer.Key("document");
    WriteDocument(Writer, *Document);
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString());
}

FEditorHttpResponse MEditorApiController::HandleSaveMonsterConfig(const FEditorHttpRequest& Request)
{
    MJsonValue RootValue;
    MString Error;
    if (!TryParseBodyJson(Request, RootValue, Error))
    {
        return MakeErrorResponse(400, "invalid_json", Error);
    }

    FEditorAssetIdentity Identity;
    if (!TryParseIdentity(RootValue, Identity, Error))
    {
        return MakeErrorResponse(400, "invalid_identity", Error);
    }

    FMonsterConfigEditorModel Model;
    if (!TryParseModel(RootValue, Model, Error))
    {
        return MakeErrorResponse(400, "invalid_model", Error);
    }

    MString PreviousSourcePath;
    TryGetString(RootValue, "previousSourcePath", PreviousSourcePath);

    if (!App.SaveMonsterConfig(Identity, Model, PreviousSourcePath, Error))
    {
        return MakeErrorResponse(400, "save_failed", Error);
    }

    const MMonsterConfigDocument* Document = App.GetCurrentDocument();
    if (!Document)
    {
        return MakeErrorResponse(500, "document_missing", "Document was not saved");
    }

    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(true);
    Writer.Key("document");
    WriteDocument(Writer, *Document);
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString());
}

FEditorHttpResponse MEditorApiController::HandleBatchSaveMonsterConfigs(const FEditorHttpRequest& Request)
{
    MJsonValue RootValue;
    MString Error;
    if (!TryParseBodyJson(Request, RootValue, Error))
    {
        return MakeErrorResponse(400, "invalid_json", Error);
    }

    TVector<FParsedMonsterConfigDocument> Documents;
    if (!TryParseDocumentArray(RootValue, Documents, Error))
    {
        return MakeErrorResponse(400, "invalid_documents", Error);
    }

    TVector<FMonsterConfigSaveRequest> SaveRequests;
    SaveRequests.reserve(Documents.size());
    for (const FParsedMonsterConfigDocument& Document : Documents)
    {
        FMonsterConfigSaveRequest RequestItem;
        RequestItem.Identity = Document.Identity;
        RequestItem.Model = Document.Model;
        RequestItem.PreviousSourcePath = Document.PreviousSourcePath;
        SaveRequests.push_back(std::move(RequestItem));
    }

    bool bAnyFailure = false;
    MString BatchError;
    const TVector<FMonsterConfigSaveResult> SaveResults = App.SaveMonsterConfigsBatch(SaveRequests, bAnyFailure, BatchError);
    if (!BatchError.empty())
    {
        return MakeErrorResponse(500, "batch_save_failed", BatchError);
    }

    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(true);
    Writer.Key("results");
    Writer.BeginArray();
    for (const FMonsterConfigSaveResult& SaveResult : SaveResults)
    {
        Writer.BeginObject();
        Writer.Key("previousSourcePath");
        Writer.Value(SaveResult.PreviousSourcePath);
        Writer.Key("sourcePath");
        Writer.Value(SaveResult.SourcePath);
        Writer.Key("ok");
        Writer.Value(SaveResult.bOk);
        Writer.Key("error");
        Writer.Value(SaveResult.Error);
        Writer.EndObject();
    }
    Writer.EndArray();
    Writer.Key("hasFailures");
    Writer.Value(bAnyFailure);
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString());
}

FEditorHttpResponse MEditorApiController::HandleDeleteMonsterConfigs(const FEditorHttpRequest& Request)
{
    MJsonValue RootValue;
    MString Error;
    if (!TryParseBodyJson(Request, RootValue, Error))
    {
        return MakeErrorResponse(400, "invalid_json", Error);
    }

    TVector<FEditorAssetIdentity> Assets;
    if (!TryParseSourcePathArray(RootValue, Assets, Error))
    {
        return MakeErrorResponse(400, "invalid_source_paths", Error);
    }

    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(true);
    Writer.Key("results");
    Writer.BeginArray();

    bool bAnyFailure = false;
    for (const FEditorAssetIdentity& Asset : Assets)
    {
        MString DeleteError;
        const bool bDeleted = App.DeleteMonsterConfig(Asset, DeleteError);

        Writer.BeginObject();
        Writer.Key("sourcePath");
        Writer.Value(Asset.SourcePath);
        Writer.Key("ok");
        Writer.Value(bDeleted);
        Writer.Key("error");
        Writer.Value(DeleteError);
        Writer.EndObject();

        bAnyFailure = bAnyFailure || !bDeleted;
    }

    Writer.EndArray();
    Writer.Key("hasFailures");
    Writer.Value(bAnyFailure);
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString());
}

FEditorHttpResponse MEditorApiController::HandleValidateMonsterConfig(const FEditorHttpRequest& Request) const
{
    MJsonValue RootValue;
    MString Error;
    if (!TryParseBodyJson(Request, RootValue, Error))
    {
        return MakeErrorResponse(400, "invalid_json", Error);
    }

    FMonsterConfigEditorModel Model;
    if (!TryParseModel(RootValue, Model, Error))
    {
        return MakeErrorResponse(400, "invalid_model", Error);
    }

    const TVector<FValidationIssue> Issues = App.ValidateMonsterConfig(Model);

    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(true);
    WriteIssuesResult(Writer, Issues);
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString());
}

FEditorHttpResponse MEditorApiController::HandleExportMonsterConfig(const FEditorHttpRequest& Request) const
{
    MJsonValue RootValue;
    MString Error;
    if (!TryParseBodyJson(Request, RootValue, Error))
    {
        return MakeErrorResponse(400, "invalid_json", Error);
    }

    FEditorAssetIdentity Identity;
    if (!TryParseIdentity(RootValue, Identity, Error))
    {
        return MakeErrorResponse(400, "invalid_identity", Error);
    }

    FMonsterConfigEditorModel Model;
    if (!TryParseModel(RootValue, Model, Error))
    {
        return MakeErrorResponse(400, "invalid_model", Error);
    }

    FAssetExportOptions Options;
    TryGetBool(RootValue, "exportJson", Options.bExportJson);
    TryGetBool(RootValue, "exportMob", Options.bExportMob);
    TryGetBool(RootValue, "exportRoundTripJson", Options.bExportRoundTripJson);
    TryGetBool(RootValue, "publishMob", Options.bPublishMob);

    const FAssetExportResult Result = App.ExportMonsterConfig(Identity, Model, Options);
    if (!Result.bSuccess)
    {
        MJsonWriter Writer = MJsonWriter::Object();
        Writer.Key("ok");
        Writer.Value(false);
        Writer.Key("error");
        Writer.Value(Result.Error);
        WriteIssuesResult(Writer, Result.Issues);
        Writer.EndObject();
        return MakeJsonResponse(Writer.ToString(), 400);
    }

    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(true);
    Writer.Key("result");
    Writer.BeginObject();
    Writer.Key("jsonPath");
    Writer.Value(Result.JsonPath);
    Writer.Key("mobPath");
    Writer.Value(Result.MobPath);
    Writer.Key("roundTripPath");
    Writer.Value(Result.RoundTripPath);
    Writer.Key("publishPath");
    Writer.Value(Result.PublishPath);
    Writer.Key("issues");
    WriteIssueArray(Writer, Result.Issues);
    Writer.EndObject();
    Writer.EndObject();
    return MakeJsonResponse(Writer.ToString());
}
