using Mession.Tools.MObjectEditorAvalonia.Models;
using System.Linq;

namespace Mession.Tools.MObjectEditorAvalonia.ViewModels;

public sealed class MonsterConfigRowViewModel : ViewModelBase
{
    public static readonly string[] EditableFieldOrder =
    [
        "assetName",
        "categoryPath",
        "monsterTemplateId",
        "debugName",
        "sceneId",
        "currentHealth",
        "maxHealth",
        "attackPower",
        "defensePower",
        "primarySkillId",
        "experienceReward",
        "goldReward",
        "skillIds",
    ];

    private string _assetName = string.Empty;
    private string _categoryPath = "Combat/Monsters";
    private uint _monsterTemplateId;
    private string _debugName = string.Empty;
    private uint _sceneId;
    private uint _currentHealth;
    private uint _maxHealth;
    private uint _attackPower;
    private uint _defensePower;
    private uint _primarySkillId;
    private uint _experienceReward;
    private uint _goldReward;
    private string _skillIdsText = string.Empty;
    private bool _dirty;
    private bool _hasErrors;
    private string _loadError = string.Empty;
    private string _validationSummary = "待校验";
    private string _sourcePath = string.Empty;
    private string _exportJsonPath = string.Empty;
    private string _exportMobPath = string.Empty;
    private string _exportRoundTripPath = string.Empty;
    private string _publishMobPath = string.Empty;
    private string _originalSourcePath = string.Empty;
    private bool _persisted;

    public string AssetName
    {
        get => _assetName;
        set
        {
            if (SetProperty(ref _assetName, value.Trim()))
            {
                MarkDirtyAndRefreshPaths();
            }
        }
    }

    public string CategoryPath
    {
        get => _categoryPath;
        set
        {
            if (SetProperty(ref _categoryPath, NormalizeCategoryPath(value)))
            {
                MarkDirtyAndRefreshPaths();
            }
        }
    }

    public uint MonsterTemplateId { get => _monsterTemplateId; set => SetEditableUInt(ref _monsterTemplateId, value); }
    public string DebugName { get => _debugName; set => SetEditableString(ref _debugName, value); }
    public uint SceneId { get => _sceneId; set => SetEditableUInt(ref _sceneId, value); }
    public uint CurrentHealth { get => _currentHealth; set => SetEditableUInt(ref _currentHealth, value); }
    public uint MaxHealth { get => _maxHealth; set => SetEditableUInt(ref _maxHealth, value); }
    public uint AttackPower { get => _attackPower; set => SetEditableUInt(ref _attackPower, value); }
    public uint DefensePower { get => _defensePower; set => SetEditableUInt(ref _defensePower, value); }
    public uint PrimarySkillId { get => _primarySkillId; set => SetEditableUInt(ref _primarySkillId, value); }
    public uint ExperienceReward { get => _experienceReward; set => SetEditableUInt(ref _experienceReward, value); }
    public uint GoldReward { get => _goldReward; set => SetEditableUInt(ref _goldReward, value); }

    public string SkillIdsText
    {
        get => _skillIdsText;
        set => SetEditableString(ref _skillIdsText, value);
    }

    public bool Dirty
    {
        get => _dirty;
        set => SetProperty(ref _dirty, value);
    }

    public bool HasErrors
    {
        get => _hasErrors;
        set => SetProperty(ref _hasErrors, value);
    }

    public string LoadError
    {
        get => _loadError;
        set => SetProperty(ref _loadError, value);
    }

    public string ValidationSummary
    {
        get => _validationSummary;
        set => SetProperty(ref _validationSummary, value);
    }

    public string SourcePath
    {
        get => _sourcePath;
        private set => SetProperty(ref _sourcePath, value);
    }

    public string ExportJsonPath
    {
        get => _exportJsonPath;
        private set => SetProperty(ref _exportJsonPath, value);
    }

    public string ExportMobPath
    {
        get => _exportMobPath;
        private set => SetProperty(ref _exportMobPath, value);
    }

    public string ExportRoundTripPath
    {
        get => _exportRoundTripPath;
        private set => SetProperty(ref _exportRoundTripPath, value);
    }

    public string PublishMobPath
    {
        get => _publishMobPath;
        private set => SetProperty(ref _publishMobPath, value);
    }

    public string OriginalSourcePath
    {
        get => _originalSourcePath;
        private set => SetProperty(ref _originalSourcePath, value);
    }

    public bool Persisted
    {
        get => _persisted;
        private set => SetProperty(ref _persisted, value);
    }

    public static MonsterConfigRowViewModel CreateNew(string? categoryPath = null)
    {
        var row = new MonsterConfigRowViewModel
        {
            _categoryPath = NormalizeCategoryPath(string.IsNullOrWhiteSpace(categoryPath) ? "Combat/Monsters" : categoryPath),
            _primarySkillId = 1001,
            _validationSummary = "待保存",
            _dirty = true,
            _persisted = false,
            _originalSourcePath = string.Empty,
        };
        row.RefreshPaths();
        row.RaiseAllProperties();
        return row;
    }

    public void LoadFromDto(MonsterConfigTableRowDto dto)
    {
        _assetName = dto.Identity.AssetName;
        _categoryPath = NormalizeCategoryPath(dto.Identity.CategoryPath);
        _monsterTemplateId = dto.Model.MonsterTemplateId;
        _debugName = dto.Model.DebugName;
        _sceneId = dto.Model.SpawnParams.SceneId;
        _currentHealth = dto.Model.SpawnParams.CurrentHealth;
        _maxHealth = dto.Model.SpawnParams.MaxHealth;
        _attackPower = dto.Model.SpawnParams.AttackPower;
        _defensePower = dto.Model.SpawnParams.DefensePower;
        _primarySkillId = dto.Model.SpawnParams.PrimarySkillId;
        _experienceReward = dto.Model.SpawnParams.ExperienceReward;
        _goldReward = dto.Model.SpawnParams.GoldReward;
        _skillIdsText = string.Join(", ", dto.Model.SkillIds);
        _dirty = dto.Dirty;
        _hasErrors = dto.HasErrors;
        _loadError = dto.LoadError;
        _validationSummary = BuildValidationSummary(dto);
        _sourcePath = dto.Paths.SourcePath;
        _exportJsonPath = dto.Paths.ExportJsonPath;
        _exportMobPath = dto.Paths.ExportMobPath;
        _exportRoundTripPath = dto.Paths.ExportRoundTripPath;
        _publishMobPath = dto.Paths.PublishMobPath;
        _originalSourcePath = dto.Identity.SourcePath;
        _persisted = true;

        RaiseAllProperties();
    }

    public SaveDocumentDto ToSaveDocument()
    {
        return new SaveDocumentDto
        {
            Identity = new AssetIdentityDto
            {
                AssetName = AssetName,
                CategoryPath = CategoryPath,
                SourcePath = SourcePath,
            },
            PreviousSourcePath = OriginalSourcePath,
            Model = new MonsterConfigModelDto
            {
                MonsterTemplateId = MonsterTemplateId,
                DebugName = DebugName,
                SpawnParams = new MonsterSpawnParamsDto
                {
                    SceneId = SceneId,
                    CurrentHealth = CurrentHealth,
                    MaxHealth = MaxHealth,
                    AttackPower = AttackPower,
                    DefensePower = DefensePower,
                    PrimarySkillId = PrimarySkillId,
                    ExperienceReward = ExperienceReward,
                    GoldReward = GoldReward,
                },
                SkillIds = ParseSkillIds(),
            },
        };
    }

    public MonsterConfigRowViewModel CreateDuplicate(string assetName, string? categoryPath = null)
    {
        var duplicated = new MonsterConfigRowViewModel
        {
            _assetName = assetName,
            _categoryPath = NormalizeCategoryPath(string.IsNullOrWhiteSpace(categoryPath) ? CategoryPath : categoryPath),
            _monsterTemplateId = MonsterTemplateId,
            _debugName = DebugName,
            _sceneId = SceneId,
            _currentHealth = CurrentHealth,
            _maxHealth = MaxHealth,
            _attackPower = AttackPower,
            _defensePower = DefensePower,
            _primarySkillId = PrimarySkillId,
            _experienceReward = ExperienceReward,
            _goldReward = GoldReward,
            _skillIdsText = SkillIdsText,
            _dirty = true,
            _hasErrors = false,
            _loadError = string.Empty,
            _validationSummary = "待保存",
            _originalSourcePath = string.Empty,
            _persisted = false,
        };
        duplicated.RefreshPaths();
        duplicated.RaiseAllProperties();
        return duplicated;
    }

    public ExportRequestDto ToExportRequest(bool publish)
    {
        var document = ToSaveDocument();
        return new ExportRequestDto
        {
            Identity = document.Identity,
            PreviousSourcePath = document.PreviousSourcePath,
            Model = document.Model,
            ExportJson = true,
            ExportMob = true,
            ExportRoundTripJson = false,
            PublishMob = publish,
        };
    }

    public void MarkSaved()
    {
        Dirty = false;
        Persisted = true;
        OriginalSourcePath = SourcePath;
        ValidationSummary = HasErrors ? ValidationSummary : "已保存";
    }

    public string GetDeleteSourcePath()
    {
        return string.IsNullOrWhiteSpace(OriginalSourcePath) ? SourcePath : OriginalSourcePath;
    }

    public string GetEditableFieldText(string fieldKey)
    {
        return fieldKey switch
        {
            "assetName" => AssetName,
            "categoryPath" => CategoryPath,
            "monsterTemplateId" => MonsterTemplateId.ToString(),
            "debugName" => DebugName,
            "sceneId" => SceneId.ToString(),
            "currentHealth" => CurrentHealth.ToString(),
            "maxHealth" => MaxHealth.ToString(),
            "attackPower" => AttackPower.ToString(),
            "defensePower" => DefensePower.ToString(),
            "primarySkillId" => PrimarySkillId.ToString(),
            "experienceReward" => ExperienceReward.ToString(),
            "goldReward" => GoldReward.ToString(),
            "skillIds" => SkillIdsText,
            _ => string.Empty,
        };
    }

    public bool TrySetEditableFieldValue(string fieldKey, string rawValue)
    {
        switch (fieldKey)
        {
        case "assetName":
            AssetName = rawValue;
            return true;
        case "categoryPath":
            CategoryPath = rawValue;
            return true;
        case "monsterTemplateId":
            if (TryParseUInt(rawValue, out var monsterTemplateId))
            {
                MonsterTemplateId = monsterTemplateId;
                return true;
            }
            return false;
        case "debugName":
            DebugName = rawValue;
            return true;
        case "sceneId":
            if (TryParseUInt(rawValue, out var sceneId))
            {
                SceneId = sceneId;
                return true;
            }
            return false;
        case "currentHealth":
            if (TryParseUInt(rawValue, out var currentHealth))
            {
                CurrentHealth = currentHealth;
                return true;
            }
            return false;
        case "maxHealth":
            if (TryParseUInt(rawValue, out var maxHealth))
            {
                MaxHealth = maxHealth;
                return true;
            }
            return false;
        case "attackPower":
            if (TryParseUInt(rawValue, out var attackPower))
            {
                AttackPower = attackPower;
                return true;
            }
            return false;
        case "defensePower":
            if (TryParseUInt(rawValue, out var defensePower))
            {
                DefensePower = defensePower;
                return true;
            }
            return false;
        case "primarySkillId":
            if (TryParseUInt(rawValue, out var primarySkillId))
            {
                PrimarySkillId = primarySkillId;
                return true;
            }
            return false;
        case "experienceReward":
            if (TryParseUInt(rawValue, out var experienceReward))
            {
                ExperienceReward = experienceReward;
                return true;
            }
            return false;
        case "goldReward":
            if (TryParseUInt(rawValue, out var goldReward))
            {
                GoldReward = goldReward;
                return true;
            }
            return false;
        case "skillIds":
            SkillIdsText = rawValue;
            return true;
        default:
            return false;
        }
    }

    public static string GetFieldDisplayName(string fieldKey)
    {
        return fieldKey switch
        {
            "assetName" => "资产",
            "categoryPath" => "分类",
            "monsterTemplateId" => "模板",
            "debugName" => "调试名",
            "sceneId" => "场景",
            "currentHealth" => "当前生命",
            "maxHealth" => "最大生命",
            "attackPower" => "攻击",
            "defensePower" => "防御",
            "primarySkillId" => "主技能",
            "experienceReward" => "经验",
            "goldReward" => "金币",
            "skillIds" => "技能列表",
            _ => fieldKey,
        };
    }

    public static bool IsNumericField(string fieldKey)
    {
        return fieldKey is "monsterTemplateId" or
            "sceneId" or
            "currentHealth" or
            "maxHealth" or
            "attackPower" or
            "defensePower" or
            "primarySkillId" or
            "experienceReward" or
            "goldReward";
    }

    public uint GetEditableFieldUInt(string fieldKey)
    {
        return fieldKey switch
        {
            "monsterTemplateId" => MonsterTemplateId,
            "sceneId" => SceneId,
            "currentHealth" => CurrentHealth,
            "maxHealth" => MaxHealth,
            "attackPower" => AttackPower,
            "defensePower" => DefensePower,
            "primarySkillId" => PrimarySkillId,
            "experienceReward" => ExperienceReward,
            "goldReward" => GoldReward,
            _ => 0,
        };
    }

    public void ApplyValidation(ValidateResponseDto response)
    {
        HasErrors = response.HasErrors;
        ValidationSummary = response.HasErrors
            ? $"错误 {response.Issues.Count}"
            : response.Issues.Count > 0 ? $"警告 {response.Issues.Count}" : "通过";
    }

    private void SetEditableUInt(ref uint field, uint value, string? propertyName = null)
    {
        if (SetProperty(ref field, value, propertyName))
        {
            MarkDirty();
        }
    }

    private void SetEditableString(ref string field, string value, string? propertyName = null)
    {
        if (SetProperty(ref field, value.Trim(), propertyName))
        {
            MarkDirty();
        }
    }

    private void MarkDirtyAndRefreshPaths()
    {
        RefreshPaths();
        MarkDirty();
    }

    private void MarkDirty()
    {
        Dirty = true;
        ValidationSummary = "待保存";
    }

    private void RefreshPaths()
    {
        var normalizedCategory = NormalizeCategoryPath(CategoryPath);
        var assetName = string.IsNullOrWhiteSpace(AssetName) ? "<asset>" : AssetName.Trim();
        var prefix = string.IsNullOrWhiteSpace(normalizedCategory) ? string.Empty : normalizedCategory + "/";
        SourcePath = $"EditorAssets/{prefix}{assetName}.masset.json";
        ExportJsonPath = $"Build/Generated/Assets/{prefix}{assetName}.json";
        ExportMobPath = $"Build/Generated/Assets/{prefix}{assetName}.mob";
        ExportRoundTripPath = $"Build/Generated/Assets/{prefix}{assetName}.roundtrip.json";
        PublishMobPath = $"GameData/{prefix}{assetName}.mob";
    }

    private List<uint> ParseSkillIds()
    {
        return SkillIdsText
            .Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
            .Select(value => uint.TryParse(value, out var parsed) ? parsed : 0)
            .Where(value => value > 0)
            .ToList();
    }

    private static string BuildValidationSummary(MonsterConfigTableRowDto dto)
    {
        if (!string.IsNullOrWhiteSpace(dto.LoadError))
        {
            return "加载失败";
        }

        if (dto.HasErrors)
        {
            return $"错误 {dto.Issues.Count}";
        }

        return dto.Issues.Count > 0 ? $"警告 {dto.Issues.Count}" : "通过";
    }

    private static string NormalizeCategoryPath(string? value)
    {
        return string.Join('/', (value ?? string.Empty)
            .Replace('\\', '/')
            .Split('/', StringSplitOptions.RemoveEmptyEntries));
    }

    private static bool TryParseUInt(string rawValue, out uint value)
    {
        return uint.TryParse(rawValue.Trim(), out value);
    }

    private void RaiseAllProperties()
    {
        RaisePropertyChanged(nameof(AssetName));
        RaisePropertyChanged(nameof(CategoryPath));
        RaisePropertyChanged(nameof(MonsterTemplateId));
        RaisePropertyChanged(nameof(DebugName));
        RaisePropertyChanged(nameof(SceneId));
        RaisePropertyChanged(nameof(CurrentHealth));
        RaisePropertyChanged(nameof(MaxHealth));
        RaisePropertyChanged(nameof(AttackPower));
        RaisePropertyChanged(nameof(DefensePower));
        RaisePropertyChanged(nameof(PrimarySkillId));
        RaisePropertyChanged(nameof(ExperienceReward));
        RaisePropertyChanged(nameof(GoldReward));
        RaisePropertyChanged(nameof(SkillIdsText));
        RaisePropertyChanged(nameof(Dirty));
        RaisePropertyChanged(nameof(HasErrors));
        RaisePropertyChanged(nameof(LoadError));
        RaisePropertyChanged(nameof(ValidationSummary));
        RaisePropertyChanged(nameof(SourcePath));
        RaisePropertyChanged(nameof(ExportJsonPath));
        RaisePropertyChanged(nameof(ExportMobPath));
        RaisePropertyChanged(nameof(ExportRoundTripPath));
        RaisePropertyChanged(nameof(PublishMobPath));
        RaisePropertyChanged(nameof(OriginalSourcePath));
        RaisePropertyChanged(nameof(Persisted));
    }
}
