using Mession.Tools.MObjectEditorAvalonia.Models;
using Mession.Tools.MObjectEditorAvalonia.Services;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;

namespace Mession.Tools.MObjectEditorAvalonia.ViewModels;

public sealed class MainWindowViewModel : ViewModelBase
{
    public sealed class BatchFieldOption
    {
        public string Key { get; init; } = string.Empty;
        public string Label { get; init; } = string.Empty;
    }

    private readonly EditorApiClient _client;
    private MonsterConfigRowViewModel? _selectedRow;
    private string _batchFieldKey = "debugName";
    private string _pasteStartFieldKey = "debugName";
    private BatchFieldOption? _selectedBatchField;
    private BatchFieldOption? _selectedPasteStartField;
    private bool _isBusy;
    private bool _serviceOnline;
    private string _serviceUrl = "http://127.0.0.1:18081/";
    private string _statusText = "等待连接服务";
    private string _resultText = string.Empty;
    private string _activeAssetTab = "monster";
    private string _lastPasteSummary = "尚未执行";
    private string _saveAsCategoryPath = string.Empty;
    private string _saveAsAssetName = string.Empty;
    private string _saveAsSuffix = "_Copy";

    public MainWindowViewModel()
        : this(new EditorApiClient())
    {
    }

    public MainWindowViewModel(EditorApiClient client)
    {
        _client = client;
        foreach (var fieldKey in MonsterConfigRowViewModel.EditableFieldOrder)
        {
            BatchFieldOptions.Add(new BatchFieldOption
            {
                Key = fieldKey,
                Label = MonsterConfigRowViewModel.GetFieldDisplayName(fieldKey),
            });
        }
        _selectedBatchField = BatchFieldOptions.FirstOrDefault(option => option.Key == _batchFieldKey);
        _selectedPasteStartField = BatchFieldOptions.FirstOrDefault(option => option.Key == _pasteStartFieldKey);
    }

    public ObservableCollection<MonsterConfigRowViewModel> Rows { get; } = [];
    public ObservableCollection<MonsterConfigRowViewModel> SelectedRows { get; } = [];
    public ObservableCollection<BatchFieldOption> BatchFieldOptions { get; } = [];

    public MonsterConfigRowViewModel? SelectedRow
    {
        get => _selectedRow;
        set
        {
            if (SetProperty(ref _selectedRow, value))
            {
                if (value is not null && !SelectedRows.Contains(value))
                {
                    SelectedRows.Clear();
                    SelectedRows.Add(value);
                }

                SyncSaveAsDefaultsFromSelection();
                RaiseSummaryChanged();
            }
        }
    }

    public string BatchFieldKey
    {
        get => _batchFieldKey;
        set
        {
            if (SetProperty(ref _batchFieldKey, value))
            {
                RaiseSummaryChanged();
            }
        }
    }

    public BatchFieldOption? SelectedBatchField
    {
        get => _selectedBatchField;
        set
        {
            if (SetProperty(ref _selectedBatchField, value) && value is not null)
            {
                BatchFieldKey = value.Key;
            }
        }
    }

    public string PasteStartFieldKey
    {
        get => _pasteStartFieldKey;
        set
        {
            if (SetProperty(ref _pasteStartFieldKey, value))
            {
                RaiseSummaryChanged();
            }
        }
    }

    public BatchFieldOption? SelectedPasteStartField
    {
        get => _selectedPasteStartField;
        set
        {
            if (SetProperty(ref _selectedPasteStartField, value) && value is not null)
            {
                PasteStartFieldKey = value.Key;
            }
        }
    }

    public bool IsBusy
    {
        get => _isBusy;
        private set => SetProperty(ref _isBusy, value);
    }

    public bool ServiceOnline
    {
        get => _serviceOnline;
        private set => SetProperty(ref _serviceOnline, value);
    }

    public string ServiceUrl
    {
        get => _serviceUrl;
        private set => SetProperty(ref _serviceUrl, value);
    }

    public string StatusText
    {
        get => _statusText;
        private set => SetProperty(ref _statusText, value);
    }

    public string ResultText
    {
        get => _resultText;
        private set => SetProperty(ref _resultText, value);
    }

    public string ActiveAssetTab
    {
        get => _activeAssetTab;
        set => SetProperty(ref _activeAssetTab, value);
    }

    public string SaveAsCategoryPath
    {
        get => _saveAsCategoryPath;
        set
        {
            var normalized = NormalizeCategoryPath(value);
            if (SetProperty(ref _saveAsCategoryPath, normalized))
            {
                RaisePropertyChanged(nameof(SaveAsSummary));
            }
        }
    }

    public string SaveAsAssetName
    {
        get => _saveAsAssetName;
        set
        {
            if (SetProperty(ref _saveAsAssetName, value.Trim()))
            {
                RaisePropertyChanged(nameof(SaveAsSummary));
            }
        }
    }

    public string SaveAsSuffix
    {
        get => _saveAsSuffix;
        set
        {
            if (SetProperty(ref _saveAsSuffix, value.Trim()))
            {
                RaisePropertyChanged(nameof(SaveAsSummary));
            }
        }
    }

    public int DirtyCount => Rows.Count(row => row.Dirty);
    public int SelectedCount => SelectedRows.Count;
    public string PasteAnchorRowSummary
    {
        get
        {
            if (SelectedRow is null)
            {
                return "未选择";
            }

            var rowIndex = Rows.IndexOf(SelectedRow);
            var assetName = string.IsNullOrWhiteSpace(SelectedRow.AssetName) ? "未命名" : SelectedRow.AssetName;
            return rowIndex >= 0 ? $"第 {rowIndex + 1} 行 / {assetName}" : assetName;
        }
    }

    public string PasteAnchorColumnSummary => MonsterConfigRowViewModel.GetFieldDisplayName(PasteStartFieldKey);
    public string LastPasteSummary => _lastPasteSummary;
    public string SelectionRangeSummary
    {
        get
        {
            var orderedRows = GetOrderedSelectedRows();
            if (orderedRows.Count == 0)
            {
                return "未选择";
            }

            if (orderedRows.Count == 1)
            {
                var singleIndex = Rows.IndexOf(orderedRows[0]);
                return singleIndex >= 0 ? $"第 {singleIndex + 1} 行" : "单行";
            }

            var firstIndex = Rows.IndexOf(orderedRows[0]);
            var lastIndex = Rows.IndexOf(orderedRows[^1]);
            if (firstIndex >= 0 && lastIndex >= 0)
            {
                return $"第 {firstIndex + 1} - {lastIndex + 1} 行 / 共 {orderedRows.Count} 行";
            }

            return $"共 {orderedRows.Count} 行";
        }
    }

    public string SelectedSummary
    {
        get
        {
            if (SelectedRows.Count == 0)
            {
                return "未选择";
            }

            if (SelectedRows.Count == 1)
            {
                return string.IsNullOrWhiteSpace(SelectedRows[0].AssetName) ? "未命名" : SelectedRows[0].AssetName;
            }

            return $"已选择 {SelectedRows.Count} 行";
        }
    }

    public string SaveAsSummary
    {
        get
        {
            var orderedRows = GetOrderedSelectedRows();
            if (orderedRows.Count == 0)
            {
                return "未选择";
            }

            var targetCategory = string.IsNullOrWhiteSpace(SaveAsCategoryPath)
                ? orderedRows[0].CategoryPath
                : SaveAsCategoryPath;
            if (orderedRows.Count == 1)
            {
                var targetName = !string.IsNullOrWhiteSpace(SaveAsAssetName)
                    ? SaveAsAssetName
                    : BuildSaveAsSeedName(orderedRows[0].AssetName, SaveAsSuffix);
                return $"{targetCategory}/{targetName}";
            }

            var suffix = string.IsNullOrWhiteSpace(SaveAsSuffix) ? "(保留原名)" : SaveAsSuffix;
            return $"{targetCategory} / {orderedRows.Count} 条 / 后缀 {suffix}";
        }
    }

    public async Task LoadAsync()
    {
        await RunBusyAsync(async () =>
        {
            StatusText = "正在连接 MObjectEditorService...";
            ResultText = string.Empty;

            var status = await _client.GetStatusAsync();
            ServiceOnline = true;
            ServiceUrl = "http://127.0.0.1:18081/";
            StatusText = $"服务在线：{status.Service}";
            await ReloadRowsFromServiceAsync(resetPasteSummary: true);
            _lastPasteSummary = "尚未执行";
            RaiseSummaryChanged();
            ResultText = $"已加载 {Rows.Count} 条怪物配置。";
        });
    }

    public async Task SaveDirtyRowsAsync()
    {
        var dirtyRows = Rows.Where(row => row.Dirty).ToList();
        if (dirtyRows.Count == 0)
        {
            StatusText = "当前没有待保存的修改。";
            return;
        }

        await RunBusyAsync(async () =>
        {
            StatusText = $"正在保存 {dirtyRows.Count} 条修改...";
            var request = new BatchSaveRequestDto
            {
                Documents = dirtyRows.Select(row => row.ToSaveDocument()).ToList(),
            };

            var response = await _client.SaveMonsterConfigsBatchAsync(request);
            foreach (var row in dirtyRows)
            {
                var saved = response.Results.FirstOrDefault(item =>
                    item.SourcePath == row.SourcePath ||
                    (!string.IsNullOrWhiteSpace(item.PreviousSourcePath) && item.PreviousSourcePath == row.OriginalSourcePath));
                if (saved is not null && saved.Ok)
                {
                    row.MarkSaved();
                }
            }

            RaiseSummaryChanged();
            ResultText = string.Join(Environment.NewLine, response.Results.Select(item =>
                $"{(item.Ok ? "成功" : "失败")}: {(string.IsNullOrWhiteSpace(item.PreviousSourcePath) ? item.SourcePath : item.PreviousSourcePath + " -> " + item.SourcePath)} {item.Error}".Trim()));
            StatusText = response.HasFailures ? "批量保存完成，但存在失败项。" : "批量保存完成。";
        });
    }

    public void AddRow()
    {
        var categoryPath = SelectedRow?.CategoryPath;
        var row = MonsterConfigRowViewModel.CreateNew(categoryPath);
        AttachRow(row);
        Rows.Insert(0, row);
        SelectedRow = row;
        SelectedRows.Clear();
        SelectedRows.Add(row);
        StatusText = "已新增空白怪物配置。";
        RaiseSummaryChanged();
    }

    public async Task SaveAsSelectedAsync()
    {
        await SaveCopiesAsync(copyOnlyToCategory: false);
    }

    public async Task CopySelectedToCategoryAsync()
    {
        await SaveCopiesAsync(copyOnlyToCategory: true);
    }

    public void DuplicateSelectedRows()
    {
        var sourceRows = GetOrderedSelectedRows();
        if (sourceRows.Count == 0)
        {
            StatusText = "请先选择至少一条配置。";
            return;
        }

        var usedNamesByCategory = Rows
            .GroupBy(row => row.CategoryPath, StringComparer.OrdinalIgnoreCase)
            .ToDictionary(
                group => group.Key,
                group => group.Select(row => row.AssetName).ToHashSet(StringComparer.OrdinalIgnoreCase),
                StringComparer.OrdinalIgnoreCase);

        var duplicatedRows = new List<MonsterConfigRowViewModel>(sourceRows.Count);
        foreach (var sourceRow in sourceRows)
        {
            if (!usedNamesByCategory.TryGetValue(sourceRow.CategoryPath, out var usedNames))
            {
                usedNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                usedNamesByCategory[sourceRow.CategoryPath] = usedNames;
            }

            var nextName = BuildDuplicateAssetName(sourceRow.AssetName, usedNames);
            usedNames.Add(nextName);

            var duplicated = sourceRow.CreateDuplicate(nextName);
            AttachRow(duplicated);
            duplicatedRows.Add(duplicated);
        }

        var insertIndex = Rows.IndexOf(sourceRows[^1]);
        insertIndex = insertIndex < 0 ? Rows.Count : insertIndex + 1;
        for (var index = 0; index < duplicatedRows.Count; index += 1)
        {
            Rows.Insert(insertIndex + index, duplicatedRows[index]);
        }

        SelectedRow = duplicatedRows[0];
        ResetSelectedRows(duplicatedRows);
        StatusText = duplicatedRows.Count == 1
            ? $"已复制配置为 {duplicatedRows[0].AssetName}。"
            : $"已批量复制 {duplicatedRows.Count} 条配置。";
        RaiseSummaryChanged();
    }

    public async Task DeleteSelectedAsync()
    {
        var targetRows = GetOrderedSelectedRows();
        if (targetRows.Count == 0)
        {
            StatusText = "请先选择至少一条配置。";
            return;
        }

        var transientRows = targetRows
            .Where(row => !row.Persisted && string.IsNullOrWhiteSpace(row.OriginalSourcePath))
            .ToList();
        var persistedRows = targetRows.Except(transientRows).ToList();

        await RunBusyAsync(async () =>
        {
            StatusText = targetRows.Count == 1
                ? $"正在删除 {targetRows[0].AssetName}..."
                : $"正在删除 {targetRows.Count} 条配置...";

            var deletedRows = new List<MonsterConfigRowViewModel>();
            var resultLines = new List<string>();

            foreach (var transientRow in transientRows)
            {
                deletedRows.Add(transientRow);
                resultLines.Add($"成功: {transientRow.SourcePath} (本地未保存)");
            }

            if (persistedRows.Count > 0)
            {
                var sourcePathToRow = persistedRows.ToDictionary(
                    row => row.GetDeleteSourcePath(),
                    row => row,
                    StringComparer.OrdinalIgnoreCase);

                var response = await _client.DeleteMonsterConfigsAsync(new DeleteRequestDto
                {
                    SourcePaths = sourcePathToRow.Keys.ToList(),
                });

                foreach (var deleteResult in response.Results)
                {
                    if (deleteResult.Ok &&
                        sourcePathToRow.TryGetValue(deleteResult.SourcePath, out var deletedRow))
                    {
                        deletedRows.Add(deletedRow);
                        resultLines.Add($"成功: {deleteResult.SourcePath}");
                    }
                    else
                    {
                        var failedPath = string.IsNullOrWhiteSpace(deleteResult.SourcePath) ? "<unknown>" : deleteResult.SourcePath;
                        resultLines.Add($"失败: {failedPath} {deleteResult.Error}".Trim());
                    }
                }

                var returnedPaths = response.Results
                    .Select(item => item.SourcePath)
                    .Where(path => !string.IsNullOrWhiteSpace(path))
                    .ToHashSet(StringComparer.OrdinalIgnoreCase);
                foreach (var sourcePath in sourcePathToRow.Keys.Where(path => !returnedPaths.Contains(path)))
                {
                    resultLines.Add($"失败: {sourcePath} 服务未返回删除结果");
                }
            }

            foreach (var row in deletedRows.Distinct().ToList())
            {
                Rows.Remove(row);
            }

            var nextSelectedRow = Rows.FirstOrDefault();
            SelectedRow = nextSelectedRow;
            ResetSelectedRows(nextSelectedRow is null ? [] : [nextSelectedRow]);

            ResultText = string.Join(Environment.NewLine, resultLines);
            StatusText = deletedRows.Count == 0
                ? "删除失败。"
                : resultLines.Any(line => line.StartsWith("失败:", StringComparison.Ordinal))
                    ? $"删除完成，成功 {deletedRows.Count} 条，存在失败项。"
                    : $"删除完成，共 {deletedRows.Count} 条。";

            RaiseSummaryChanged();
        });
    }

    public void UpdateSelectedRows(IReadOnlyList<MonsterConfigRowViewModel> rows)
    {
        var orderedRows = rows
            .Where(row => row is not null)
            .OrderBy(row => Rows.IndexOf(row))
            .ToList();
        ResetSelectedRows(orderedRows);
        if (SelectedRows.Count > 0)
        {
            _selectedRow = SelectedRows[0];
            RaisePropertyChanged(nameof(SelectedRow));
        }
        else
        {
            _selectedRow = null;
            RaisePropertyChanged(nameof(SelectedRow));
        }
        RaiseSummaryChanged();
    }

    public void FillDownSelectedRows()
    {
        if (SelectedRows.Count < 2)
        {
            StatusText = "向下填充至少需要选择 2 行。";
            return;
        }

        var sourceRow = SelectedRow ?? SelectedRows[0];
        var sourceValue = sourceRow.GetEditableFieldText(BatchFieldKey);
        foreach (var row in SelectedRows)
        {
            if (!ReferenceEquals(row, sourceRow))
            {
                row.TrySetEditableFieldValue(BatchFieldKey, sourceValue);
            }
        }

        StatusText = $"已向下填充 {MonsterConfigRowViewModel.GetFieldDisplayName(BatchFieldKey)}。";
        RaiseSummaryChanged();
    }

    public void IncrementFillSelectedRows()
    {
        if (!MonsterConfigRowViewModel.IsNumericField(BatchFieldKey))
        {
            StatusText = $"当前列 {MonsterConfigRowViewModel.GetFieldDisplayName(BatchFieldKey)} 不支持递增填充。";
            return;
        }

        if (SelectedRows.Count < 2)
        {
            StatusText = "递增填充至少需要选择 2 行。";
            return;
        }

        var orderedRows = SelectedRows
            .OrderBy(row => Rows.IndexOf(row))
            .ToList();
        var firstValue = (long)orderedRows[0].GetEditableFieldUInt(BatchFieldKey);
        var secondValue = (long)orderedRows[1].GetEditableFieldUInt(BatchFieldKey);
        var step = secondValue != firstValue ? secondValue - firstValue : 1L;

        for (var index = 0; index < orderedRows.Count; index += 1)
        {
            var nextValue = Math.Max(0L, firstValue + (index * step));
            orderedRows[index].TrySetEditableFieldValue(BatchFieldKey, nextValue.ToString());
        }

        StatusText = $"已对 {MonsterConfigRowViewModel.GetFieldDisplayName(BatchFieldKey)} 执行递增填充。";
        RaiseSummaryChanged();
    }

    public void ClearSelectedField()
    {
        if (SelectedRows.Count == 0)
        {
            StatusText = "请先选择至少一行。";
            return;
        }

        var clearedValue = MonsterConfigRowViewModel.IsNumericField(BatchFieldKey) ? "0" : string.Empty;
        foreach (var row in SelectedRows)
        {
            row.TrySetEditableFieldValue(BatchFieldKey, clearedValue);
        }

        StatusText = $"已清空 {MonsterConfigRowViewModel.GetFieldDisplayName(BatchFieldKey)}。";
        RaiseSummaryChanged();
    }

    public void PasteGridText(string clipboardText)
    {
        if (SelectedRow is null)
        {
            StatusText = "请先选择起始行。";
            return;
        }

        var grid = ParseClipboardGrid(clipboardText);
        if (grid.Count == 0)
        {
            StatusText = "剪贴板没有可粘贴内容。";
            return;
        }

        var startRowIndex = Rows.IndexOf(SelectedRow);
        var startColumnIndex = Array.IndexOf(MonsterConfigRowViewModel.EditableFieldOrder, PasteStartFieldKey);
        if (startRowIndex < 0 || startColumnIndex < 0)
        {
            StatusText = "当前起始位置无效。";
            return;
        }

        var appliedCells = 0;
        var appliedRowCount = 0;
        var appliedColumnCount = 0;
        var requestedColumnCount = grid.Max(row => row.Count);
        var warnings = new StringBuilder();
        for (var rowOffset = 0; rowOffset < grid.Count; rowOffset += 1)
        {
            var targetRowIndex = startRowIndex + rowOffset;
            if (targetRowIndex >= Rows.Count)
            {
                break;
            }

            var targetRow = Rows[targetRowIndex];
            var cells = grid[rowOffset];
            var rowApplied = false;
            for (var columnOffset = 0; columnOffset < cells.Count; columnOffset += 1)
            {
                var targetFieldIndex = startColumnIndex + columnOffset;
                if (targetFieldIndex >= MonsterConfigRowViewModel.EditableFieldOrder.Length)
                {
                    break;
                }

                var fieldKey = MonsterConfigRowViewModel.EditableFieldOrder[targetFieldIndex];
                if (targetRow.TrySetEditableFieldValue(fieldKey, cells[columnOffset]))
                {
                    appliedCells += 1;
                    appliedColumnCount = Math.Max(appliedColumnCount, columnOffset + 1);
                    rowApplied = true;
                    continue;
                }

                warnings.AppendLine($"跳过 {targetRow.AssetName}/{MonsterConfigRowViewModel.GetFieldDisplayName(fieldKey)}: {cells[columnOffset]}");
            }

            if (rowApplied)
            {
                appliedRowCount += 1;
            }
        }

        var maxAvailableRows = Math.Max(0, Rows.Count - startRowIndex);
        var maxAvailableColumns = Math.Max(0, MonsterConfigRowViewModel.EditableFieldOrder.Length - startColumnIndex);
        var effectiveRows = Math.Min(grid.Count, maxAvailableRows);
        var effectiveColumns = Math.Min(requestedColumnCount, maxAvailableColumns);
        _lastPasteSummary = $"请求 {grid.Count}x{requestedColumnCount}，实际作用 {effectiveRows}x{effectiveColumns}，生效 {appliedCells} 格";

        var resultLines = new List<string>
        {
            $"起始行: {PasteAnchorRowSummary}",
            $"起始列: {PasteAnchorColumnSummary}",
            $"粘贴尺寸: {grid.Count} 行 x {requestedColumnCount} 列",
            $"实际作用: {effectiveRows} 行 x {effectiveColumns} 列",
            $"成功写入: {appliedCells} 个单元格",
        };
        if (warnings.Length > 0)
        {
            resultLines.Add("警告:");
            resultLines.Add(warnings.ToString().TrimEnd());
        }

        ResultText = string.Join(Environment.NewLine, resultLines);
        StatusText = appliedCells > 0
            ? $"已粘贴 {appliedCells} 个单元格，覆盖 {appliedRowCount} 行 {appliedColumnCount} 列。"
            : "粘贴内容未产生任何变更。";
        RaiseSummaryChanged();
    }

    public async Task ValidateSelectedAsync()
    {
        if (SelectedRow is null)
        {
            StatusText = "请先选择一条配置。";
            return;
        }

        await RunBusyAsync(async () =>
        {
            StatusText = $"正在校验 {SelectedRow.AssetName}...";
            var response = await _client.ValidateMonsterConfigAsync(SelectedRow.ToSaveDocument());
            SelectedRow.ApplyValidation(response);
            ResultText = response.Issues.Count == 0
                ? "校验通过。"
                : string.Join(Environment.NewLine, response.Issues.Select(issue => $"[{issue.Severity}] {issue.FieldPath} {issue.Message}".Trim()));
            StatusText = response.HasErrors ? "校验结束，存在错误。" : "校验结束。";
        });
    }

    public async Task ExportSelectedAsync(bool publish)
    {
        if (SelectedRow is null)
        {
            StatusText = "请先选择一条配置。";
            return;
        }

        await RunBusyAsync(async () =>
        {
            StatusText = publish ? "正在发布当前配置..." : "正在导出当前配置...";
            var response = await _client.ExportMonsterConfigAsync(SelectedRow.ToExportRequest(publish));
            if (response.Result is null)
            {
                ResultText = string.Empty;
                StatusText = "导出接口未返回结果。";
                return;
            }

            ResultText = string.Join(Environment.NewLine, new[]
            {
                $"JSON: {response.Result.JsonPath}",
                $"MOB: {response.Result.MobPath}",
                $"发布: {response.Result.PublishPath}",
            }.Where(line => !line.EndsWith(": ")));
            StatusText = publish ? "发布完成。" : "导出完成。";
        });
    }

    public void ActivateAssetTab(string assetTab)
    {
        ActiveAssetTab = assetTab;
        if (assetTab != "monster")
        {
            StatusText = "当前版本只接入怪物配置，其他资产类型先保留骨架。";
        }
    }

    private async Task SaveCopiesAsync(bool copyOnlyToCategory)
    {
        var sourceRows = GetOrderedSelectedRows();
        if (sourceRows.Count == 0)
        {
            StatusText = "请先选择至少一条配置。";
            return;
        }

        var targetCategory = string.IsNullOrWhiteSpace(SaveAsCategoryPath)
            ? sourceRows[0].CategoryPath
            : NormalizeCategoryPath(SaveAsCategoryPath);
        if (string.IsNullOrWhiteSpace(targetCategory))
        {
            StatusText = "目标分类不能为空。";
            return;
        }

        var usedNames = Rows
            .Where(row => string.Equals(row.CategoryPath, targetCategory, StringComparison.OrdinalIgnoreCase))
            .Select(row => row.AssetName)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        var draftRows = new List<MonsterConfigRowViewModel>(sourceRows.Count);
        foreach (var sourceRow in sourceRows)
        {
            var requestedName = ResolveCopyTargetName(sourceRow, sourceRows.Count, copyOnlyToCategory);
            if (string.IsNullOrWhiteSpace(requestedName))
            {
                StatusText = "另存为名称不能为空。";
                return;
            }

            var targetName = EnsureUniqueAssetName(requestedName, usedNames);
            usedNames.Add(targetName);

            var copiedRow = sourceRow.CreateDuplicate(targetName, targetCategory);
            draftRows.Add(copiedRow);
        }

        var selectedPaths = new List<string>();
        await RunBusyAsync(async () =>
        {
            StatusText = copyOnlyToCategory
                ? $"正在复制 {draftRows.Count} 条配置到 {targetCategory}..."
                : $"正在另存为 {draftRows.Count} 条配置...";

            var request = new BatchSaveRequestDto
            {
                Documents = draftRows.Select(row => row.ToSaveDocument()).ToList(),
            };
            var response = await _client.SaveMonsterConfigsBatchAsync(request);

            ResultText = string.Join(Environment.NewLine, response.Results.Select(item =>
                $"{(item.Ok ? "成功" : "失败")}: {item.SourcePath} {item.Error}".Trim()));
            selectedPaths = response.Results
                .Where(item => item.Ok && !string.IsNullOrWhiteSpace(item.SourcePath))
                .Select(item => item.SourcePath)
                .ToList();

            StatusText = response.HasFailures
                ? "保存副本完成，但存在失败项。"
                : copyOnlyToCategory
                    ? $"已复制到 {targetCategory}。"
                    : "另存为完成。";
        });

        if (selectedPaths.Count > 0)
        {
            await ReloadRowsFromServiceAsync(selectedPaths, resetPasteSummary: false);
        }
    }

    private async Task RunBusyAsync(Func<Task> action)
    {
        if (IsBusy)
        {
            return;
        }

        try
        {
            IsBusy = true;
            await action();
        }
        catch (Exception ex)
        {
            ServiceOnline = false;
            StatusText = ex.Message;
        }
        finally
        {
            IsBusy = false;
        }
    }

    private void RaiseSummaryChanged()
    {
        RaisePropertyChanged(nameof(DirtyCount));
        RaisePropertyChanged(nameof(SelectedCount));
        RaisePropertyChanged(nameof(SelectedSummary));
        RaisePropertyChanged(nameof(PasteAnchorRowSummary));
        RaisePropertyChanged(nameof(PasteAnchorColumnSummary));
        RaisePropertyChanged(nameof(SelectionRangeSummary));
        RaisePropertyChanged(nameof(LastPasteSummary));
        RaisePropertyChanged(nameof(SaveAsSummary));
    }

    private void AttachRow(MonsterConfigRowViewModel row)
    {
        row.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(MonsterConfigRowViewModel.Dirty))
            {
                RaisePropertyChanged(nameof(DirtyCount));
            }

            if (args.PropertyName is nameof(MonsterConfigRowViewModel.Dirty) or
                nameof(MonsterConfigRowViewModel.AssetName) or
                nameof(MonsterConfigRowViewModel.CategoryPath))
            {
                RaiseSummaryChanged();
            }
        };
    }

    private static List<List<string>> ParseClipboardGrid(string text)
    {
        var normalized = (text ?? string.Empty).Replace("\r", string.Empty);
        var rows = normalized
            .Split('\n', StringSplitOptions.RemoveEmptyEntries)
            .Select(line => line.Split('\t').ToList())
            .Where(line => line.Count > 0)
            .ToList();
        return rows;
    }

    private async Task ReloadRowsFromServiceAsync(IReadOnlyList<string>? preferredSourcePaths = null, bool resetPasteSummary = false)
    {
        var table = await _client.GetMonsterConfigsTableAsync();
        Rows.Clear();
        foreach (var dto in table.Rows)
        {
            var row = new MonsterConfigRowViewModel();
            row.LoadFromDto(dto);
            AttachRow(row);
            Rows.Add(row);
        }

        if (resetPasteSummary)
        {
            _lastPasteSummary = "尚未执行";
        }

        var selectedRows = preferredSourcePaths is null || preferredSourcePaths.Count == 0
            ? new List<MonsterConfigRowViewModel>()
            : Rows
                .Where(row => preferredSourcePaths.Contains(row.SourcePath, StringComparer.OrdinalIgnoreCase))
                .ToList();

        if (selectedRows.Count == 0)
        {
            var firstRow = Rows.FirstOrDefault();
            if (firstRow is not null)
            {
                selectedRows.Add(firstRow);
            }
        }

        ResetSelectedRows(selectedRows);
        _selectedRow = SelectedRows.FirstOrDefault();
        RaisePropertyChanged(nameof(SelectedRow));
        SyncSaveAsDefaultsFromSelection();
        RaiseSummaryChanged();
    }

    private List<MonsterConfigRowViewModel> GetOrderedSelectedRows()
    {
        return SelectedRows
            .Where(row => row is not null)
            .OrderBy(row => Rows.IndexOf(row))
            .ToList();
    }

    private string ResolveCopyTargetName(MonsterConfigRowViewModel sourceRow, int selectedCount, bool copyOnlyToCategory)
    {
        if (!copyOnlyToCategory &&
            selectedCount == 1 &&
            !string.IsNullOrWhiteSpace(SaveAsAssetName))
        {
            return SaveAsAssetName;
        }

        return BuildSaveAsSeedName(sourceRow.AssetName, SaveAsSuffix);
    }

    private static string BuildDuplicateAssetName(string sourceAssetName, ISet<string> usedNames)
    {
        var seedName = string.IsNullOrWhiteSpace(sourceAssetName) ? "Monster_Copy" : sourceAssetName + "_Copy";
        var nextName = seedName;
        var suffix = 1;
        while (usedNames.Contains(nextName))
        {
            nextName = $"{seedName}_{suffix}";
            suffix += 1;
        }

        return nextName;
    }

    private static string BuildSaveAsSeedName(string sourceAssetName, string suffix)
    {
        var baseName = string.IsNullOrWhiteSpace(sourceAssetName) ? "Monster" : sourceAssetName.Trim();
        return string.IsNullOrWhiteSpace(suffix) ? baseName : baseName + suffix.Trim();
    }

    private static string EnsureUniqueAssetName(string requestedName, ISet<string> usedNames)
    {
        var baseName = string.IsNullOrWhiteSpace(requestedName) ? "Monster" : requestedName.Trim();
        var nextName = baseName;
        var suffix = 1;
        while (usedNames.Contains(nextName))
        {
            nextName = $"{baseName}_{suffix}";
            suffix += 1;
        }

        return nextName;
    }

    private static string NormalizeCategoryPath(string? value)
    {
        return string.Join('/', (value ?? string.Empty)
            .Replace('\\', '/')
            .Split('/', StringSplitOptions.RemoveEmptyEntries));
    }

    private void SyncSaveAsDefaultsFromSelection()
    {
        if (SelectedRows.Count == 0)
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(_saveAsCategoryPath))
        {
            _saveAsCategoryPath = SelectedRows[0].CategoryPath;
            RaisePropertyChanged(nameof(SaveAsCategoryPath));
        }
    }

    private void ResetSelectedRows(IReadOnlyList<MonsterConfigRowViewModel> rows)
    {
        SelectedRows.Clear();
        foreach (var row in rows)
        {
            SelectedRows.Add(row);
        }
    }
}
