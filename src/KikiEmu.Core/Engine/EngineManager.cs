using System.Diagnostics;
using KikiEmu.Core.Config;
using KikiEmu.Core.Storage;

namespace KikiEmu.Core.Engine;

public class EngineManager
{
    public const string EngineReleaseUrl = "https://github.com/Goribesh/Habumi/releases/download/0.2.3/Habumi-0.2.3.zip";
    public const string EngineZipName = "KikiEmu-Engine-ARM64-v0.2.3.zip";

    public static string GetEngineDirectory()
    {
        var dir = Path.Combine(KikiConfig.GetConfigDirectory(), "engine");
        if (!Directory.Exists(dir))
        {
            Directory.CreateDirectory(dir);
        }
        return dir;
    }

    public static string? FindEngineBinary()
    {
        var config = KikiConfig.Load();
        if (!string.IsNullOrEmpty(config.EnginePath) && File.Exists(config.EnginePath))
        {
            return config.EnginePath;
        }

        var env = Environment.GetEnvironmentVariable("KIKIEMU_ENGINE");
        if (!string.IsNullOrEmpty(env) && File.Exists(env))
        {
            return env;
        }

        // Check local engine directory
        var engineDir = GetEngineDirectory();
        var candidates = new[]
        {
            Path.Combine(engineDir, "bin", "Habumi.exe"),
            Path.Combine(engineDir, "runtime", "bin", "Habumi.exe"),
            Path.Combine(engineDir, "qemu-system-aarch64.exe"),
            Path.Combine(engineDir, "bin", "qemu-system-aarch64.exe"),
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "engine", "qemu-system-aarch64.exe")
        };

        foreach (var c in candidates)
        {
            if (File.Exists(c)) return c;
        }

        return null;
    }

    public static async Task<string> EnsureEngineInstalledAsync(
        Action<string>? statusCallback = null,
        IProgress<(long current, long total)>? progress = null)
    {
        var existing = FindEngineBinary();
        if (existing != null) return existing;

        statusCallback?.Invoke("Engine not found locally. Downloading native ARM64 WHPX virtualization engine...");
        var cachedZip = await CacheManager.EnsureDownloadedAsync(
            EngineReleaseUrl,
            EngineZipName,
            progress: progress
        );

        var engineDir = GetEngineDirectory();
        statusCallback?.Invoke($"Deploying engine into {engineDir}...");
        System.IO.Compression.ZipFile.ExtractToDirectory(cachedZip, engineDir, overwriteFiles: true);

        var binary = FindEngineBinary();
        if (binary == null)
        {
            throw new FileNotFoundException("Failed to locate engine binary after extraction.");
        }

        var config = KikiConfig.Load();
        config.EnginePath = binary;
        config.Save();

        statusCallback?.Invoke("ARM64 WHPX engine installed successfully.");
        return binary;
    }
}
