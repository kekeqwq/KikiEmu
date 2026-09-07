using System.IO;
using System.Windows;
using System.Windows.Input;
using KikiEmu.Core.Config;
using KikiEmu.Core.Engine;

namespace KikiEmu.UI;

public partial class MainWindow : Window
{
    private bool _isFullscreen = false;
    private bool _isLandscape = false;
    private bool _isHudVisible = true;
    private HcsRunner? _hcsRunner;

    public MainWindow()
    {
        App.Log("MainWindow constructor entered");
        InitializeComponent();
        App.Log("MainWindow.InitializeComponent succeeded");

        Loaded += MainWindow_Loaded;
        Closing += MainWindow_Closing;
        App.Log("MainWindow constructor finished");
    }

    private void MainWindow_Loaded(object sender, RoutedEventArgs e)
    {
        App.Log("MainWindow_Loaded called");
        InitializeEmulatorSession();
    }

    private void InitializeEmulatorSession()
    {
        App.Log("InitializeEmulatorSession started");
        try
        {
            var config = KikiConfig.Load();
            var instance = config.Instances.Values.FirstOrDefault(i => i.Id == "1") ?? config.Instances.Values.FirstOrDefault();

            if (instance != null)
            {
                BootStatusText.Text = $"正在启动 Android 17 [{instance.SystemId}]...";
                BootDetailText.Text = "准备 Hyper-V MicroVM 拓扑配置...";

                Task.Run(() =>
                {
                    try
                    {
                        _hcsRunner = HcsRunner.LaunchInstance(instance, log =>
                        {
                            Dispatcher.InvokeAsync(() =>
                            {
                                BootDetailText.Text = log;
                                if (log.Contains("SCSI"))
                                {
                                    BootStatusText.Text = "正在挂载虚拟磁盘总线...";
                                }
                                else if (log.Contains("Direct Boot"))
                                {
                                    BootStatusText.Text = "配置 Linux 7.3 直接引导与硬件直通...";
                                }
                                else if (log.Contains("HCS"))
                                {
                                    BootStatusText.Text = "正在调起 Windows 11 Hyper-V 算力系统...";
                                }
                                else if (log.Contains("点火启动"))
                                {
                                    BootStatusText.Text = "正在点火启动 ARM64 MicroVM 核心...";
                                }
                                else if (log.Contains("运行中"))
                                {
                                    BootStatusText.Text = "Linux 7.3 Mainline 已就绪 (6 Oryon 核心运行)";
                                    BootDetailText.Text = "Android Init 初始化中，点击屏幕可进入纯画布";
                                }
                            });
                        });
                        App.Log("HcsRunner launched successfully!");
                    }
                    catch (Exception ex)
                    {
                        App.Log($"HcsRunner launch error: {ex.Message}");
                        Dispatcher.InvokeAsync(() =>
                        {
                            BootStatusText.Text = "MicroVM 启动遇到异常";
                            BootDetailText.Text = ex.Message;
                        });
                    }
                });
            }
            else
            {
                BootStatusText.Text = "未找到已创建的实例";
                BootDetailText.Text = "请在终端中运行: dotnet run --project src/KikiEmu.Cli -- create";
            }
        }
        catch (Exception ex)
        {
            App.Log($"InitializeEmulatorSession error: {ex.Message}");
        }
    }

    private void MainWindow_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        try
        {
            _hcsRunner?.Dispose();
        }
        catch { }
    }

    private void BootSplashGrid_MouseDown(object sender, MouseButtonEventArgs e)
    {
        // 允许用户直接点击蒙版随时隐藏蒙版，进入纯 Viewport 画布
        BootSplashGrid.Visibility = BootSplashGrid.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
        e.Handled = true;
    }

    private void Window_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (e.LeftButton == MouseButtonState.Pressed && !_isFullscreen)
        {
            DragMove();
        }
    }

    private void Window_KeyDown(object sender, KeyEventArgs e)
    {
        // Space: 切换显示/隐藏蒙版
        if (e.Key == Key.Space)
        {
            BootSplashGrid.Visibility = BootSplashGrid.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
            e.Handled = true;
        }
        // F11: 切换全屏平板模式
        else if (e.Key == Key.F11)
        {
            ToggleFullscreen();
            e.Handled = true;
        }
        // Ctrl + R 或 F10: 旋转屏幕 (横屏/竖屏)
        else if ((e.Key == Key.R && Keyboard.Modifiers.HasFlag(ModifierKeys.Control)) || e.Key == Key.F10)
        {
            ToggleOrientation();
            e.Handled = true;
        }
        // F12 或 Ctrl + H: 开关测试诊断 HUD
        else if (e.Key == Key.F12 || (e.Key == Key.H && Keyboard.Modifiers.HasFlag(ModifierKeys.Control)))
        {
            ToggleHud();
            e.Handled = true;
        }
        // Escape: 全屏下退出全屏
        else if (e.Key == Key.Escape && _isFullscreen)
        {
            ToggleFullscreen();
            e.Handled = true;
        }
    }

    private void ToggleFullscreen()
    {
        _isFullscreen = !_isFullscreen;
        if (_isFullscreen)
        {
            WindowState = WindowState.Normal;
            WindowStyle = WindowStyle.None;
            ResizeMode = ResizeMode.NoResize;
            WindowState = WindowState.Maximized;

            // 在 Surface Pro 11 全屏下，默认切换为平板比例 (2880x1920 3:2)
            ViewportContainer.Width = 2880;
            ViewportContainer.Height = 1920;
            HudResolutionText.Text = "Mode: 2880x1920 (Surface Pro 11 Native 3:2)";
        }
        else
        {
            WindowState = WindowState.Normal;
            ResizeMode = ResizeMode.CanResizeWithGrip;
            Width = _isLandscape ? 1120 : 430;
            Height = _isLandscape ? 740 : 860;
            UpdateViewportSize();
        }
    }

    private void ToggleOrientation()
    {
        _isLandscape = !_isLandscape;
        if (!_isFullscreen)
        {
            var temp = Width;
            Width = Height;
            Height = temp;
        }
        UpdateViewportSize();
    }

    private void UpdateViewportSize()
    {
        if (_isLandscape)
        {
            ViewportContainer.Width = 2400;
            ViewportContainer.Height = 1080;
            HudResolutionText.Text = "Mode: 2400x1080 (Landscape Tablet)";
        }
        else
        {
            ViewportContainer.Width = 1080;
            ViewportContainer.Height = 2400;
            HudResolutionText.Text = "Mode: 1080x2400 (Phone 9:20)";
        }
    }

    private void ToggleHud()
    {
        _isHudVisible = !_isHudVisible;
        DiagnosticsHud.Visibility = _isHudVisible ? Visibility.Visible : Visibility.Collapsed;
    }

    private void Window_PreviewTouchDown(object sender, TouchEventArgs e)
    {
        // 触控输入直通
    }

    private void Window_PreviewTouchMove(object sender, TouchEventArgs e)
    {
        // 触控移动直通
    }

    private void Window_PreviewTouchUp(object sender, TouchEventArgs e)
    {
        // 触控释放直通
    }
}
