﻿using KikiEmu.Core.Config;
using KikiEmu.Core.Engine;
using KikiEmu.Core.Repository;
using KikiEmu.Core.Storage;

namespace KikiEmu.Cli;

class Program
{
    static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = System.Text.Encoding.UTF8;

        if (args.Length == 0 || args[0] is "-h" or "--help" or "help")
        {
            PrintHelp();
            return 0;
        }

        var command = args[0].ToLowerInvariant();

        try
        {
            switch (command)
            {
                case "list":
                    return await HandleListAsync(args.Skip(1).ToArray());

                case "create":
                    return await HandleCreateAsync(args.Skip(1).ToArray());

                case "set":
                    return HandleSet(args.Skip(1).ToArray());

                case "delete":
                    return HandleDelete(args.Skip(1).ToArray());

                case "run":
                    return await HandleRunAsync(args.Skip(1).ToArray());

                default:
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine($"Unknown command: '{command}'");
                    Console.ResetColor();
                    PrintHelp();
                    return 1;
            }
        }
        catch (Exception ex)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine($"Error: {ex.Message}");
            Console.ResetColor();
            return 1;
        }
    }

    private static void PrintHelp()
    {
        Console.ForegroundColor = ConsoleColor.Cyan;
        Console.WriteLine(@"
 _  ___ _    _ _____                 
| |/ (_) |  (_) ____|_ __ ___  _   _ 
| ' /| | |  | |  _| | '_ ` _ \| | | |
| . \| | |__| | |___| | | | | | |_| |
|_|\_\_|\_____|_____|_| |_| |_|\__,_|
Lightweight Native ARM64 Android Emulator for Windows on ARM (Snapdragon)
Direct Windows 11 Hyper-V MicroVM Architecture
");
        Console.ResetColor();

        Console.WriteLine("Usage:");
        Console.WriteLine("  kikiemu list                          List all created Android instances");
        Console.WriteLine("  kikiemu list --system [--update]       List available Android system images (Google Play 17+)");
        Console.WriteLine("  kikiemu create --storage <dir> --system <name> --id <id>   Create a new instance");
        Console.WriteLine("  kikiemu set --default <id>             Set the default instance for GUI launcher");
        Console.WriteLine("  kikiemu run [--id <id>]               Launch an instance via CLI");
        Console.WriteLine("  kikiemu delete <id> [-y|--yes]        Delete an instance and its storage directory");
        Console.WriteLine("  kikiemu delete --cache                Clear temporary download cache archives");
        Console.WriteLine();
    }

    private static async Task<int> HandleListAsync(string[] args)
    {
        bool isSystem = args.Any(a => a.Equals("--system", StringComparison.OrdinalIgnoreCase) || a.Equals("-s", StringComparison.OrdinalIgnoreCase));
        bool isUpdate = args.Any(a => a.Equals("--update", StringComparison.OrdinalIgnoreCase) || a.Equals("-u", StringComparison.OrdinalIgnoreCase));

        if (isSystem)
        {
            Console.ForegroundColor = ConsoleColor.Yellow;
            if (isUpdate)
            {
                Console.WriteLine("Updating system images from Google official repository...");
            }
            Console.ResetColor();

            var systems = GoogleRepoClient.LoadAvailableSystems(isUpdate, msg => Console.WriteLine($"  -> {msg}"));

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"{"System ID",-16} {"Version",-8} {"Page",-6} {"Build / Release Date",-24} {"Status",-12} {"Display Name"}");
            Console.WriteLine(new string('-', 95));
            Console.ResetColor();

            foreach (var sys in systems)
            {
                var isCached = CacheManager.IsCached(sys.ArchiveFileName, sys.FileSizeBytes);
                var statusStr = isCached ? "[Cached]" : "[Online]";
                var dateStr = sys.BuildDate?.ToString("yyyy-MM-dd HH:mm UTC") ?? "Unknown";

                var statusColor = isCached ? ConsoleColor.Cyan : ConsoleColor.DarkGray;
                Console.Write($"{sys.Id,-16} {sys.Version,-8} {sys.PageSize,-6} {dateStr,-24} ");
                Console.ForegroundColor = statusColor;
                Console.Write($"{statusStr,-12} ");
                Console.ResetColor();
                Console.WriteLine(sys.DisplayName);
            }
            Console.WriteLine();
            return 0;
        }

        // List created instances
        var config = KikiConfig.Load();
        if (config.Instances.Count == 0)
        {
            Console.WriteLine("No instances created yet.");
            Console.WriteLine("Run 'kikiemu create --storage <dir> --system android17 --id 1' to create one.");
            return 0;
        }

        Console.WriteLine();
        Console.ForegroundColor = ConsoleColor.Green;
        Console.WriteLine($"{"ID",-8} {"Default",-8} {"System",-16} {"Refresh",-8} {"Storage Path"}");
        Console.WriteLine(new string('-', 85));
        Console.ResetColor();

        foreach (var (id, inst) in config.Instances)
        {
            var isDefault = string.Equals(config.DefaultInstanceId, id, StringComparison.OrdinalIgnoreCase);
            var defaultMark = isDefault ? "*" : "";

            if (isDefault) Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine($"{id,-8} {defaultMark,-8} {inst.SystemId,-16} {inst.RefreshRate}Hz     {inst.StoragePath}");
            if (isDefault) Console.ResetColor();
        }
        Console.WriteLine();
        return 0;
    }

    private static async Task<int> HandleCreateAsync(string[] args)
    {
        string? storage = null;
        string? system = null;
        string? id = null;

        for (int i = 0; i < args.Length; i++)
        {
            if (args[i].Equals("--storage", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                storage = args[++i];
            }
            else if (args[i].Equals("--system", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                system = args[++i];
            }
            else if (args[i].Equals("--id", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                id = args[++i];
            }
        }

        if (string.IsNullOrWhiteSpace(storage) || string.IsNullOrWhiteSpace(system) || string.IsNullOrWhiteSpace(id))
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("Missing required arguments for 'create'.");
            Console.ResetColor();
            Console.WriteLine("Example: kikiemu create --storage ~/myandroid --system android17 --id 1");
            return 1;
        }

        if (storage.StartsWith("~"))
        {
            var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            storage = Path.Combine(home, storage.TrimStart('~', '/', '\\'));
        }

        Console.ForegroundColor = ConsoleColor.Cyan;
        Console.WriteLine($"Creating KikiEmu Instance '{id}'...");
        Console.WriteLine($"  System:  {system}");
        Console.WriteLine($"  Storage: {Path.GetFullPath(storage)}");
        Console.ResetColor();

        var lastPercent = -1;
        var progress = new Progress<(long current, long total)>(p =>
        {
            if (p.total > 0)
            {
                var percent = (int)(p.current * 100 / p.total);
                if (percent != lastPercent && percent % 5 == 0)
                {
                    lastPercent = percent;
                    var mbRead = p.current / (1024 * 1024);
                    var mbTotal = p.total / (1024 * 1024);
                    Console.Write($"\rDownloading: {percent}% [{mbRead} MB / {mbTotal} MB]    ");
                }
            }
        });

        await InstanceManager.CreateAsync(id, system, storage, msg =>
        {
            Console.WriteLine();
            Console.WriteLine($"[*] {msg}");
        }, progress);

        Console.WriteLine();
        Console.ForegroundColor = ConsoleColor.Green;
        Console.WriteLine($"[✔] Instance '{id}' successfully created and ready to run!");
        Console.WriteLine($"Run 'kikiemu set --default {id}' to set it as primary launcher target.");
        Console.ResetColor();

        return 0;
    }

    private static int HandleSet(string[] args)
    {
        string? defaultId = null;

        for (int i = 0; i < args.Length; i++)
        {
            if (args[i].Equals("--default", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                defaultId = args[++i];
            }
        }

        if (string.IsNullOrWhiteSpace(defaultId))
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("Usage: kikiemu set --default <id>");
            Console.ResetColor();
            return 1;
        }

        var config = KikiConfig.Load();
        if (!config.Instances.ContainsKey(defaultId))
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine($"Error: Instance with ID '{defaultId}' does not exist.");
            Console.ResetColor();
            Console.WriteLine("Use 'kikiemu list' to view available instances.");
            return 1;
        }

        config.DefaultInstanceId = defaultId;
        config.Save();

        Console.ForegroundColor = ConsoleColor.Green;
        Console.WriteLine($"[✔] Default instance set to '{defaultId}'.");
        Console.ResetColor();
        return 0;
    }

    private static int HandleDelete(string[] args)
    {
        if (args.Length == 0)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("Usage: kikiemu delete <id> [-y]  OR  kikiemu delete --cache");
            Console.ResetColor();
            return 1;
        }

        if (args.Any(a => a.Equals("--cache", StringComparison.OrdinalIgnoreCase)))
        {
            var (count, freedBytes) = CacheManager.ClearCache();
            var freedMb = freedBytes / (1024.0 * 1024.0);
            var freedGb = freedMb / 1024.0;
            var formatted = freedGb >= 1.0 ? $"{freedGb:F2} GB" : $"{freedMb:F1} MB";

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"[✔] Cache cleared. Removed {count} archive file(s), freed {formatted} of disk space.");
            Console.ResetColor();
            return 0;
        }

        var id = args.FirstOrDefault(a => !a.StartsWith("-"));
        if (string.IsNullOrEmpty(id))
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("Please specify an instance ID to delete.");
            Console.ResetColor();
            return 1;
        }

        var config = KikiConfig.Load();
        if (!config.Instances.TryGetValue(id, out var instance))
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine($"Error: Instance with ID '{id}' was not found.");
            Console.ResetColor();
            return 1;
        }

        var force = args.Any(a => a.Equals("-y", StringComparison.OrdinalIgnoreCase) || a.Equals("--yes", StringComparison.OrdinalIgnoreCase));

        if (!force)
        {
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.Write($"Are you sure you want to delete instance '{id}' at '{instance.StoragePath}'? All data will be wiped. [y/N]: ");
            Console.ResetColor();

            var input = Console.ReadLine()?.Trim();
            if (!string.Equals(input, "y", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(input, "yes", StringComparison.OrdinalIgnoreCase))
            {
                Console.WriteLine("Operation canceled.");
                return 0;
            }
        }

        if (InstanceManager.Delete(id, out var storagePath))
        {
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"[✔] Instance '{id}' and storage at '{storagePath}' have been deleted.");
            Console.ResetColor();
            return 0;
        }
        else
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine($"Failed to delete instance '{id}'.");
            Console.ResetColor();
            return 1;
        }
    }

    private static async Task<int> HandleRunAsync(string[] args)
    {
        string? id = null;

        for (int i = 0; i < args.Length; i++)
        {
            if (args[i].Equals("--id", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                id = args[++i];
            }
        }

        var config = KikiConfig.Load();
        if (string.IsNullOrEmpty(id))
        {
            id = config.DefaultInstanceId;
        }

        if (string.IsNullOrEmpty(id) || !config.Instances.TryGetValue(id, out var instance))
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("No valid instance specified or configured as default.");
            Console.ResetColor();
            Console.WriteLine("Use 'kikiemu list' to check created instances or 'kikiemu create' to make one.");
            return 1;
        }

        Console.ForegroundColor = ConsoleColor.Green;
        Console.WriteLine($"Launching instance '{id}' ({instance.SystemId}) via Native Hyper-V MicroVM...");
        Console.WriteLine($"  Storage: {instance.StoragePath}");
        Console.WriteLine($"  Cores:   {instance.CpuCores} Oryon cores");
        Console.WriteLine($"  Memory:  {instance.MemoryMb} MB");
        Console.WriteLine($"  Display: {instance.DisplayWidth}x{instance.DisplayHeight} @ {instance.RefreshRate}Hz");
        Console.WriteLine($"  Hardware Keyboard: {(instance.EnablePhysicalKeyboard ? "Passthrough Active" : "Disabled")}");
        Console.ResetColor();

        using var runner = HcsRunner.LaunchInstance(instance);
        Console.ForegroundColor = ConsoleColor.Cyan;
        Console.WriteLine($"[✔] Hyper-V MicroVM 'kikiemu-{id}' is running cleanly. Press Ctrl+C to shut down.");
        Console.ResetColor();

        var tcs = new TaskCompletionSource();
        Console.CancelKeyPress += (s, e) =>
        {
            e.Cancel = true;
            tcs.TrySetResult();
        };

        await tcs.Task;
        Console.WriteLine("Shutting down Hyper-V MicroVM...");
        return 0;
    }
}
