using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Mession.Tools.MObjectEditorAvalonia.ViewModels;
using System.Text.Json;

namespace Mession.Tools.MObjectEditorAvalonia.Views;

public partial class MainWindow : Window
{
    private readonly string _columnLayoutPath = Path.Combine(AppContext.BaseDirectory, "MObjectEditorAvalonia.columns.json");

    public MainWindow()
    {
        InitializeComponent();
        Opened += async (_, _) =>
        {
            LoadColumnLayout();
            if (DataContext is MainWindowViewModel vm)
            {
                await vm.LoadAsync();
            }
        };
        Closing += (_, _) => SaveColumnLayout();
        KeyDown += OnWindowKeyDown;
    }

    private MainWindowViewModel? ViewModel => DataContext as MainWindowViewModel;

    private async void OnRefreshClick(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is not null)
        {
            await ViewModel.LoadAsync();
        }
    }

    private async void OnSaveAllClick(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is not null)
        {
            await ViewModel.SaveDirtyRowsAsync();
        }
    }

    private void OnAddClick(object? sender, RoutedEventArgs e)
    {
        ViewModel?.AddRow();
    }

    private void OnDuplicateClick(object? sender, RoutedEventArgs e)
    {
        ViewModel?.DuplicateSelectedRows();
    }

    private async void OnDeleteClick(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is not null)
        {
            await ViewModel.DeleteSelectedAsync();
        }
    }

    private async void OnValidateClick(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is not null)
        {
            await ViewModel.ValidateSelectedAsync();
        }
    }

    private async void OnExportClick(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is not null)
        {
            await ViewModel.ExportSelectedAsync(false);
        }
    }

    private async void OnPublishClick(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is not null)
        {
            await ViewModel.ExportSelectedAsync(true);
        }
    }

    private async void OnSaveAsClick(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is not null)
        {
            await ViewModel.SaveAsSelectedAsync();
        }
    }

    private async void OnCopyToCategoryClick(object? sender, RoutedEventArgs e)
    {
        if (ViewModel is not null)
        {
            await ViewModel.CopySelectedToCategoryAsync();
        }
    }

    private void OnFillDownClick(object? sender, RoutedEventArgs e)
    {
        ViewModel?.FillDownSelectedRows();
    }

    private void OnIncrementFillClick(object? sender, RoutedEventArgs e)
    {
        ViewModel?.IncrementFillSelectedRows();
    }

    private void OnClearFieldClick(object? sender, RoutedEventArgs e)
    {
        ViewModel?.ClearSelectedField();
    }

    private async void OnPasteClick(object? sender, RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel?.Clipboard is null || ViewModel is null)
        {
            return;
        }

        var text = await topLevel.Clipboard.TryGetTextAsync();
        if (text is not null)
        {
            ViewModel.PasteGridText(text);
        }
    }

    private void OnMonsterTableSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (ViewModel is null)
        {
            return;
        }

        var selectedRows = MonsterTable.SelectedItems?
            .OfType<MonsterConfigRowViewModel>()
            .ToList() ?? [];
        ViewModel.UpdateSelectedRows(selectedRows);
    }

    private void OnMonsterTabClick(object? sender, RoutedEventArgs e) => ViewModel?.ActivateAssetTab("monster");
    private void OnSkillTabClick(object? sender, RoutedEventArgs e) => ViewModel?.ActivateAssetTab("skill");
    private void OnBuffTabClick(object? sender, RoutedEventArgs e) => ViewModel?.ActivateAssetTab("buff");
    private void OnDropTabClick(object? sender, RoutedEventArgs e) => ViewModel?.ActivateAssetTab("drop");

    private async void OnWindowKeyDown(object? sender, KeyEventArgs e)
    {
        if (ViewModel is null)
        {
            return;
        }

        if (e.KeyModifiers.HasFlag(KeyModifiers.Control) && e.Key == Key.S)
        {
            e.Handled = true;
            await ViewModel.SaveDirtyRowsAsync();
            return;
        }

        if (e.KeyModifiers.HasFlag(KeyModifiers.Control) &&
            e.KeyModifiers.HasFlag(KeyModifiers.Shift) &&
            e.Key == Key.D)
        {
            e.Handled = true;
            ViewModel.IncrementFillSelectedRows();
            return;
        }

        if (e.KeyModifiers.HasFlag(KeyModifiers.Control) && e.Key == Key.D)
        {
            e.Handled = true;
            ViewModel.FillDownSelectedRows();
            return;
        }

        if (e.Key == Key.Delete)
        {
            e.Handled = true;
            ViewModel.ClearSelectedField();
            return;
        }

        if (e.Key == Key.F5)
        {
            e.Handled = true;
            await ViewModel.LoadAsync();
        }
    }

    private void LoadColumnLayout()
    {
        try
        {
            if (!File.Exists(_columnLayoutPath))
            {
                return;
            }

            var json = File.ReadAllText(_columnLayoutPath);
            var widths = JsonSerializer.Deserialize<Dictionary<string, double>>(json);
            if (widths is null)
            {
                return;
            }

            foreach (var column in MonsterTable.Columns)
            {
                var key = column.Header?.ToString() ?? string.Empty;
                if (widths.TryGetValue(key, out var width) && width > 0)
                {
                    column.Width = new DataGridLength(width);
                }
            }
        }
        catch
        {
        }
    }

    private void SaveColumnLayout()
    {
        try
        {
            var widths = MonsterTable.Columns.ToDictionary(
                column => column.Header?.ToString() ?? string.Empty,
                column => column.ActualWidth);
            File.WriteAllText(_columnLayoutPath, JsonSerializer.Serialize(widths, new JsonSerializerOptions
            {
                WriteIndented = true,
            }));
        }
        catch
        {
        }
    }
}
