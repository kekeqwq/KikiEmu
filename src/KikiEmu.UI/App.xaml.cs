using System.IO;
using System.Windows;

namespace KikiEmu.UI;

/// <summary>
/// Interaction logic for App.xaml
/// </summary>
public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        AppDomain.CurrentDomain.UnhandledException += (s, args) =>
        {
            var msg = args.ExceptionObject.ToString();
            File.WriteAllText("ui_crash.log", msg);
            Console.WriteLine("CRASH: " + msg);
        };

        DispatcherUnhandledException += (s, args) =>
        {
            var msg = args.Exception.ToString();
            File.WriteAllText("ui_crash.log", msg);
            Console.WriteLine("DISPATCHER CRASH: " + msg);
            args.Handled = false;
        };

        base.OnStartup(e);
    }
}
