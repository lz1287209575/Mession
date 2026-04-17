using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace Mession.Tools.MObjectEditorAvalonia.Models;

public class ApiEnvelope
{
    [JsonPropertyName("ok")]
    public bool Ok { get; set; }

    [JsonPropertyName("error")]
    public string? Error { get; set; }

    [JsonPropertyName("message")]
    public string? Message { get; set; }
}

public sealed class EditorStatusDto : ApiEnvelope
{
    [JsonPropertyName("service")]
    public string Service { get; set; } = string.Empty;

    [JsonPropertyName("assetType")]
    public string AssetType { get; set; } = string.Empty;
}

public sealed class MonsterConfigTableResponseDto : ApiEnvelope
{
    [JsonPropertyName("rows")]
    public List<MonsterConfigTableRowDto> Rows { get; set; } = [];
}

public sealed class BatchSaveResponseDto : ApiEnvelope
{
    [JsonPropertyName("results")]
    public List<BatchSaveItemDto> Results { get; set; } = [];

    [JsonPropertyName("hasFailures")]
    public bool HasFailures { get; set; }
}

public sealed class DeleteResponseDto : ApiEnvelope
{
    [JsonPropertyName("results")]
    public List<DeleteItemDto> Results { get; set; } = [];

    [JsonPropertyName("hasFailures")]
    public bool HasFailures { get; set; }
}

public sealed class BatchSaveItemDto
{
    [JsonPropertyName("previousSourcePath")]
    public string PreviousSourcePath { get; set; } = string.Empty;

    [JsonPropertyName("sourcePath")]
    public string SourcePath { get; set; } = string.Empty;

    [JsonPropertyName("ok")]
    public bool Ok { get; set; }

    [JsonPropertyName("error")]
    public string Error { get; set; } = string.Empty;
}

public sealed class DeleteItemDto
{
    [JsonPropertyName("sourcePath")]
    public string SourcePath { get; set; } = string.Empty;

    [JsonPropertyName("ok")]
    public bool Ok { get; set; }

    [JsonPropertyName("error")]
    public string Error { get; set; } = string.Empty;
}

public sealed class ValidateResponseDto : ApiEnvelope
{
    [JsonPropertyName("hasErrors")]
    public bool HasErrors { get; set; }

    [JsonPropertyName("issues")]
    public List<ValidationIssueDto> Issues { get; set; } = [];
}

public sealed class ExportResponseDto : ApiEnvelope
{
    [JsonPropertyName("result")]
    public ExportResultDto? Result { get; set; }
}

public sealed class ExportResultDto
{
    [JsonPropertyName("jsonPath")]
    public string JsonPath { get; set; } = string.Empty;

    [JsonPropertyName("mobPath")]
    public string MobPath { get; set; } = string.Empty;

    [JsonPropertyName("roundTripPath")]
    public string RoundTripPath { get; set; } = string.Empty;

    [JsonPropertyName("publishPath")]
    public string PublishPath { get; set; } = string.Empty;

    [JsonPropertyName("issues")]
    public List<ValidationIssueDto> Issues { get; set; } = [];
}

public sealed class MonsterConfigTableRowDto
{
    [JsonPropertyName("identity")]
    public AssetIdentityDto Identity { get; set; } = new();

    [JsonPropertyName("paths")]
    public AssetPathsDto Paths { get; set; } = new();

    [JsonPropertyName("model")]
    public MonsterConfigModelDto Model { get; set; } = new();

    [JsonPropertyName("issues")]
    public List<ValidationIssueDto> Issues { get; set; } = [];

    [JsonPropertyName("hasErrors")]
    public bool HasErrors { get; set; }

    [JsonPropertyName("dirty")]
    public bool Dirty { get; set; }

    [JsonPropertyName("loadSucceeded")]
    public bool LoadSucceeded { get; set; }

    [JsonPropertyName("loadError")]
    public string LoadError { get; set; } = string.Empty;
}

public sealed class AssetIdentityDto
{
    [JsonPropertyName("assetName")]
    public string AssetName { get; set; } = string.Empty;

    [JsonPropertyName("categoryPath")]
    public string CategoryPath { get; set; } = string.Empty;

    [JsonPropertyName("sourcePath")]
    public string SourcePath { get; set; } = string.Empty;
}

public sealed class AssetPathsDto
{
    [JsonPropertyName("sourcePath")]
    public string SourcePath { get; set; } = string.Empty;

    [JsonPropertyName("exportJsonPath")]
    public string ExportJsonPath { get; set; } = string.Empty;

    [JsonPropertyName("exportMobPath")]
    public string ExportMobPath { get; set; } = string.Empty;

    [JsonPropertyName("exportRoundTripPath")]
    public string ExportRoundTripPath { get; set; } = string.Empty;

    [JsonPropertyName("publishMobPath")]
    public string PublishMobPath { get; set; } = string.Empty;
}

public sealed class MonsterConfigModelDto
{
    [JsonPropertyName("monsterTemplateId")]
    public uint MonsterTemplateId { get; set; }

    [JsonPropertyName("debugName")]
    public string DebugName { get; set; } = string.Empty;

    [JsonPropertyName("spawnParams")]
    public MonsterSpawnParamsDto SpawnParams { get; set; } = new();

    [JsonPropertyName("skillIds")]
    public List<uint> SkillIds { get; set; } = [];
}

public sealed class MonsterSpawnParamsDto
{
    [JsonPropertyName("sceneId")]
    public uint SceneId { get; set; }

    [JsonPropertyName("currentHealth")]
    public uint CurrentHealth { get; set; }

    [JsonPropertyName("maxHealth")]
    public uint MaxHealth { get; set; }

    [JsonPropertyName("attackPower")]
    public uint AttackPower { get; set; }

    [JsonPropertyName("defensePower")]
    public uint DefensePower { get; set; }

    [JsonPropertyName("primarySkillId")]
    public uint PrimarySkillId { get; set; }

    [JsonPropertyName("experienceReward")]
    public uint ExperienceReward { get; set; }

    [JsonPropertyName("goldReward")]
    public uint GoldReward { get; set; }
}

public sealed class ValidationIssueDto
{
    [JsonPropertyName("severity")]
    public string Severity { get; set; } = string.Empty;

    [JsonPropertyName("fieldPath")]
    public string FieldPath { get; set; } = string.Empty;

    [JsonPropertyName("code")]
    public string Code { get; set; } = string.Empty;

    [JsonPropertyName("message")]
    public string Message { get; set; } = string.Empty;
}

public sealed class BatchSaveRequestDto
{
    [JsonPropertyName("documents")]
    public List<SaveDocumentDto> Documents { get; set; } = [];
}

public sealed class DeleteRequestDto
{
    [JsonPropertyName("sourcePaths")]
    public List<string> SourcePaths { get; set; } = [];
}

public class SaveDocumentDto
{
    [JsonPropertyName("identity")]
    public AssetIdentityDto Identity { get; set; } = new();

    [JsonPropertyName("model")]
    public MonsterConfigModelDto Model { get; set; } = new();

    [JsonPropertyName("previousSourcePath")]
    public string PreviousSourcePath { get; set; } = string.Empty;
}

public sealed class ExportRequestDto : SaveDocumentDto
{
    [JsonPropertyName("exportJson")]
    public bool ExportJson { get; set; }

    [JsonPropertyName("exportMob")]
    public bool ExportMob { get; set; }

    [JsonPropertyName("exportRoundTripJson")]
    public bool ExportRoundTripJson { get; set; }

    [JsonPropertyName("publishMob")]
    public bool PublishMob { get; set; }
}
