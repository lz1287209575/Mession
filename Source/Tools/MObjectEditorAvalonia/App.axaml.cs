using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Mession.Tools.MObjectEditorAvalonia.Services;
using Mession.Tools.MObjectEditorAvalonia.ViewModels;
using Mession.Tools.MObjectEditorAvalonia.Views;

namespace Mession.Tools.MObjectEditorAvalonia;

public partial class App : Application
{
    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var client = new EditorApiClient();
            var viewModel = new MainWindowViewModel(client);
            desktop.MainWindow = new MainWindow
            {
                DataContext = viewModel,
            };
        }

        base.OnFrameworkInitializationCompleted();
    }
}
