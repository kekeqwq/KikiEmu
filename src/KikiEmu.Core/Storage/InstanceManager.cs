using System.IO.Compression;
using System.Text.Json;
using KikiEmu.Core.Config;
using KikiEmu.Core.Repository;

namespace KikiEmu.Core.Storage;

public class InstanceManager
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    public static async Task<InstanceInfo> CreateAsync(
        string id,
        string systemId,
        string storagePath,
        Action<string>? statusCallback = null,
        IProgress<(long current, long total)>? downloadProgress = null)
    {
        var config = KikiConfig.Load();
        if (config.Instances.ContainsKey(id))
        {
            throw new InvalidOperationException($"An instance with ID '{id}' already exists. Use another ID or delete it first.");
        }

        // 1. Resolve system image
        var availableSystems = GoogleRepoClient.LoadAvailableSystems();
        var sysImg = availableSystems.FirstOrDefault(s => string.Equals(s.Id, systemId, StringComparison.OrdinalIgnoreCase));
        if (sysImg == null)
        {
            var validIds = string.Join(", ", availableSystems.Select(s => s.Id));
            throw new ArgumentException($"System '{systemId}' is not recognized. Valid options: {validIds}");
        }

        // 2. Prepare storage folder
        var fullStoragePath = Path.GetFullPath(storagePath);
        if (!Directory.Exists(fullStoragePath))
        {
            Directory.CreateDirectory(fullStoragePath);
        }

        // 3. Ensure archive is downloaded to cache
        statusCallback?.Invoke($"Downloading system image for {sysImg.DisplayName} into cache...");
        var archivePath = await CacheManager.EnsureDownloadedAsync(
            sysImg.DownloadUrl,
            sysImg.ArchiveFileName,
            sysImg.FileSizeBytes,
            downloadProgress
        );

        // 4. Extract runtime files to storage directory
        statusCallback?.Invoke($"Unpacking boot images to {fullStoragePath}...");
        ExtractArchiveToStorage(archivePath, fullStoragePath, statusCallback);

        // 5. Ensure userdata.img exists
        var userdataPath = Path.Combine(fullStoragePath, "userdata.img");
        if (!File.Exists(userdataPath))
        {
            statusCallback?.Invoke("Initializing empty userdata disk (8 GB)...");
            CreateEmptyDisk(userdataPath, 8L * 1024 * 1024 * 1024); // 8GB sparse
        }

        // 6. Create instance metadata
        var instance = new InstanceInfo
        {
            Id = id,
            Name = $"KikiInstance-{id}",
            SystemId = sysImg.Id,
            StoragePath = fullStoragePath,
            CreatedAt = DateTime.UtcNow,
            CpuCores = 4,
            MemoryMb = 4096,
            DisplayWidth = 1080,
            DisplayHeight = 2400,
            DisplayDpi = 420,
            RefreshRate = 120,
            EnablePhysicalKeyboard = true,
            EnableMultiTouch = true
        };

        var metaPath = Path.Combine(fullStoragePath, "instance.json");
        File.WriteAllText(metaPath, JsonSerializer.Serialize(instance, JsonOptions));

        // 7. Update global config
        config.Instances[id] = instance;
        if (string.IsNullOrEmpty(config.DefaultInstanceId))
        {
            config.DefaultInstanceId = id;
        }
        config.Save();

        statusCallback?.Invoke($"Instance '{id}' successfully created at {fullStoragePath}.");
        return instance;
    }

    private static void ExtractArchiveToStorage(string archivePath, string storagePath, Action<string>? statusCallback)
    {
        using var zip = ZipFile.OpenRead(archivePath);

        foreach (var entry in zip.Entries)
        {
            if (string.IsNullOrEmpty(entry.Name)) continue; // Directory entry

            // We only need root system files: system.img, vendor.img, kernel-ranchu, ramdisk.img, etc.
            var fileName = entry.Name;
            var destPath = Path.Combine(storagePath, fileName);

            // Extract file directly
            statusCallback?.Invoke($"Extracting {fileName}...");
            entry.ExtractToFile(destPath, overwrite: true);
        }
    }

    public static void CreateEmptyDisk(string filePath, long sizeBytes)
    {
        // Creates a sparse file on NTFS or standard sized file
        using var fs = new FileStream(filePath, FileMode.Create, FileAccess.Write, FileShare.None);
        fs.SetLength(sizeBytes);
    }

    public static bool Delete(string id, out string storagePath)
    {
        var config = KikiConfig.Load();
        if (!config.Instances.TryGetValue(id, out var instance))
        {
            storagePath = string.Empty;
            return false;
        }

        storagePath = instance.StoragePath;

        // Clean up storage directory
        if (Directory.Exists(storagePath))
        {
            try
            {
                Directory.Delete(storagePath, recursive: true);
            }
            catch
            {
                // In case any files are temporarily locked
            }
        }

        // Remove from config
        config.Instances.Remove(id);
        if (config.DefaultInstanceId == id)
        {
            config.DefaultInstanceId = config.Instances.Keys.FirstOrDefault();
        }
        config.Save();

        return true;
    }
}
