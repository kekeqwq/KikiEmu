using System.Text.Json;
using System.Text.Json.Serialization;

namespace KikiEmu.Core.Config;

public class KikiConfig
{
    public string? DefaultInstanceId { get; set; }
    public Dictionary<string, InstanceInfo> Instances { get; set; } = new(StringComparer.OrdinalIgnoreCase);
    public string? EnginePath { get; set; }
    
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };

    public static string GetConfigDirectory()
    {
        var userDir = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        var kikiDir = Path.Combine(userDir, ".kikiemu");
        if (!Directory.Exists(kikiDir))
        {
            Directory.CreateDirectory(kikiDir);
        }
        return kikiDir;
    }

    public static string GetConfigFilePath() => Path.Combine(GetConfigDirectory(), "config.json");

    public static string GetCacheDirectory()
    {
        var cacheDir = Path.Combine(GetConfigDirectory(), "cache");
        if (!Directory.Exists(cacheDir))
        {
            Directory.CreateDirectory(cacheDir);
        }
        return cacheDir;
    }

    public static KikiConfig Load()
    {
        var path = GetConfigFilePath();
        if (!File.Exists(path))
        {
            var newConfig = new KikiConfig();
            newConfig.Save();
            return newConfig;
        }

        try
        {
            var json = File.ReadAllText(path);
            var config = JsonSerializer.Deserialize<KikiConfig>(json, JsonOptions);
            return config ?? new KikiConfig();
        }
        catch
        {
            return new KikiConfig();
        }
    }

    public void Save()
    {
        var path = GetConfigFilePath();
        var json = JsonSerializer.Serialize(this, JsonOptions);
        File.WriteAllText(path, json);
    }
}
