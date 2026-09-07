using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using KikiEmu.Core.Config;
using KikiEmu.Core.Engine;

namespace KikiEmu.UI;

public partial class MainWindow : Window
{
    private InstanceInfo? _currentInstance;
    private HcsRunner? _hcsRunner;
    private bool _isTabletFullscreen = false;
    private Rect _savedWindowBounds;

    public MainWindow()
    {
        try
        {
            File.AppendAllText("ui_trace.log", $"[{DateTime.Now:HH:mm:ss.fff}] MainWindow constructor start\n");
            InitializeComponent();
            File.AppendAllText("ui_trace.log", $"[{DateTime.Now:HH:mm:ss.fff}] MainWindow InitializeComponent complete\n");
            Loaded += MainWindow_Loaded;
            Closing += MainWindow_Closing;
        }
        catch (Exception ex)
        {
            File.AppendAllText("ui_trace.log", $"[{DateTime.Now:HH:mm:ss.fff}] InitializeComponent EXCEPTION: {ex}\n");
            throw;
        }
    }

    private void MainWindow_Loaded(object sender, RoutedEventArgs e)
    {
        File.AppendAllText("ui_trace.log", $"[{DateTime.Now:HH:mm:ss.fff}] MainWindow_Loaded start\n");
        InitializeEmulatorSession();
    }

    private void MainWindow_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        File.AppendAllText("ui_trace.log", $"[{DateTime.Now:HH:mm:ss.fff}] MainWindow_Closing\n");
        if (_hcsRunner != null)
        {
            try
            {
                _hcsRunner.Dispose();
                _hcsRunner = null;
            }
            catch { }
        }
    }

    private void InitializeEmulatorSession()
    {
        var config = KikiConfig.Load();
        if (string.IsNullOrEmpty(config.DefaultInstanceId) ||
            !config.Instances.TryGetValue(config.DefaultInstanceId, out var instance))
        {
            File.AppendAllText("ui_trace.log", $"[{DateTime.Now:HH:mm:ss.fff}] No default instance\n");
            EmptyStatePanel.Visibility = Visibility.Visible;
            BootingOverlay.Visibility = Visibility.Collapsed;
            TitleTextBlock.Text = "KikiEmu - 待配置";
            return;
        }

        _currentInstance = instance;
        EmptyStatePanel.Visibility = Visibility.Collapsed;
        BootingOverlay.Visibility = Visibility.Visible;
        BootingStatusText.Text = $"正在启动 Hyper-V MicroVM [{instance.Id}] {instance.SystemId}...";
        TitleTextBlock.Text = $"KikiEmu - [{instance.Id}] {instance.SystemId}";

        File.AppendAllText("ui_trace.log", $"[{DateTime.Now:HH:mm:ss.fff}] Launching HcsRunner for instance {instance.Id}\n");
        Task.Run(async () =>
        {
            try
            {
                var runner = HcsRunner.LaunchInstance(_currentInstance);
                _hcsRunner = runner;
                File.AppendAllText("ui_trace.log", $"[{DateTime.Now:HH:mm:ss.fff}] HcsRunner launched successfully\n");

                await Dispatcher.InvokeAsync(() =>
                {
                    BootingOverlay.Visibility = Visibility.Collapsed;
                    KeyboardStatusText.Text = "物理外设直通中 (120Hz)";
                });
            }
            catch (Exception ex)
            {
                File.AppendAllText("ui_trace.log", $"[{DateTime.Now:HH:mm:ss.fff}] HcsRunner launch failed: {ex.Message}\n");
                await Dispatcher.InvokeAsync(() =>
                {
                    BootingOverlay.Visibility = Visibility.Collapsed;
                    MessageBox.Show($"Hyper-V 启动失败: {ex.Message}", "KikiEmu 错误", MessageBoxButton.OK, MessageBoxImage.Error);
                });
            }
        });
    }

    private void RetryCheckBtn_Click(object sender, RoutedEventArgs e)
    {
        InitializeEmulatorSession();
    }

    private void TitleBar_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton == MouseButton.Left)
        {
            if (e.ClickCount == 2)
            {
                ToggleTabletFullscreen();
            }
            else
            {
                DragMove();
            }
        }
    }

    private void FullscreenBtn_Click(object sender, RoutedEventArgs e)
    {
        ToggleTabletFullscreen();
    }

    private void MinimizeBtn_Click(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState.Minimized;
    }

    private void CloseBtn_Click(object sender, RoutedEventArgs e)
    {
        Close();
    }

    private void ToggleTabletFullscreen()
    {
        _isTabletFullscreen = !_isTabletFullscreen;

        if (_isTabletFullscreen)
        {
            _savedWindowBounds = new Rect(Left, Top, Width, Height);
            WindowStyle = WindowStyle.None;
            WindowState = WindowState.Maximized;
            ModeBadge.Text = "平板全屏 (Surface 3:2)";
            ModeBadge.Foreground = System.Windows.Media.Brushes.Cyan;
        }
        else
        {
            WindowState = WindowState.Normal;
            WindowStyle = WindowStyle.SingleBorderWindow;
            Left = _savedWindowBounds.Left;
            Top = _savedWindowBounds.Top;
            Width = _savedWindowBounds.Width;
            Height = _savedWindowBounds.Height;
            ModeBadge.Text = "竖屏手机";
            ModeBadge.Foreground = System.Windows.Media.Brushes.LightGray;
        }
    }

    private void Window_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.F11)
        {
            ToggleTabletFullscreen();
            e.Handled = true;
            return;
        }
    }

    private void Window_KeyUp(object sender, KeyEventArgs e)
    {
    }

    private void DisplayContainer_TouchDown(object sender, TouchEventArgs e)
    {
    }

    private void DisplayContainer_TouchMove(object sender, TouchEventArgs e)
    {
    }

    private void DisplayContainer_TouchUp(object sender, TouchEventArgs e)
    {
    }

    private void DisplayContainer_MouseDown(object sender, MouseButtonEventArgs e)
    {
        DisplayContainer.Focus();
    }
}
