using System;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using CommonwealthOnline.Host.ViewModels;

namespace CommonwealthOnline.Host.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
    }

    private async void BrowseStatePath(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null || DataContext is not MainWindowViewModel viewModel) return;

        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            AllowMultiple = false,
            Title = "Choose durable server state file",
            FileTypeFilter = new[]
            {
                new FilePickerFileType("JSON files") { Patterns = new[] { "*.json" } },
                FilePickerFileTypes.All,
            },
        });
        if (files.Count > 0) viewModel.SetDurableStatePath(files[0].Path.LocalPath);
    }

    private async void BrowseClientPackage(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null || DataContext is not MainWindowViewModel viewModel) return;

        var folders = await topLevel.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            AllowMultiple = false,
            Title = "Choose client package folder",
        });
        if (folders.Count > 0)
        {
            viewModel.SetClientPackageFolder(folders[0].Path.LocalPath);
            viewModel.RefreshClientPackageCommand.Execute(null);
        }
    }

    private void ClientCapabilitiesChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel viewModel || sender is not ListBox list) return;
        if (list.SelectedItems is { } selected)
            viewModel.SetSelectedCapabilities(selected.OfType<string>());
    }

    protected override void OnClosed(EventArgs e)
    {
        (DataContext as IDisposable)?.Dispose();
        base.OnClosed(e);
    }
}
