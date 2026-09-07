using System.Security.Cryptography;
using KikiEmu.Core.Config;

namespace KikiEmu.Core.Storage;

public class CacheManager
{
    private static readonly HttpClient HttpClient = new()
    {
        Timeout = TimeSpan.FromMinutes(60) // Large images may take time to download
    };

    public static string GetCacheDirectory() => KikiConfig.GetCacheDirectory();

    public static string GetCachedFilePath(string fileName) =>
        Path.Combine(GetCacheDirectory(), fileName);

    public static bool IsCached(string fileName, long expectedSize = 0)
    {
        var path = GetCachedFilePath(fileName);
        if (!File.Exists(path)) return false;
        if (expectedSize > 0)
        {
            var fi = new FileInfo(path);
            return fi.Length == expectedSize;
        }
        return true;
    }

    public static async Task<string> EnsureDownloadedAsync(
        string url,
        string fileName,
        long expectedSize = 0,
        IProgress<(long downloadedBytes, long totalBytes)>? progress = null,
        CancellationToken cancellationToken = default)
    {
        var targetPath = GetCachedFilePath(fileName);
        var tempPath = targetPath + ".download";

        if (IsCached(fileName, expectedSize))
        {
            return targetPath;
        }

        if (File.Exists(tempPath))
        {
            File.Delete(tempPath);
        }

        using var response = await HttpClient.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        response.EnsureSuccessStatusCode();

        var totalBytes = response.Content.Headers.ContentLength ?? expectedSize;

        await using (var contentStream = await response.Content.ReadAsStreamAsync(cancellationToken))
        await using (var fileStream = new FileStream(tempPath, FileMode.Create, FileAccess.Write, FileShare.None, 81920, true))
        {
            var buffer = new byte[81920];
            long totalRead = 0;
            int read;

            while ((read = await contentStream.ReadAsync(buffer, 0, buffer.Length, cancellationToken)) > 0)
            {
                await fileStream.WriteAsync(buffer, 0, read, cancellationToken);
                totalRead += read;
                progress?.Report((totalRead, totalBytes));
            }
        }

        if (File.Exists(targetPath))
        {
            File.Delete(targetPath);
        }
        File.Move(tempPath, targetPath);

        return targetPath;
    }

    public static (int fileCount, long totalBytes) GetCacheInfo()
    {
        var dir = new DirectoryInfo(GetCacheDirectory());
        if (!dir.Exists) return (0, 0);

        var files = dir.GetFiles("*", SearchOption.AllDirectories);
        long size = files.Sum(f => f.Length);
        return (files.Length, size);
    }

    public static (int deletedCount, long freedBytes) ClearCache()
    {
        var dir = new DirectoryInfo(GetCacheDirectory());
        if (!dir.Exists) return (0, 0);

        var files = dir.GetFiles("*", SearchOption.AllDirectories);
        int count = 0;
        long freed = 0;

        foreach (var file in files)
        {
            try
            {
                freed += file.Length;
                file.Delete();
                count++;
            }
            catch
            {
                // File might be locked
            }
        }

        return (count, freed);
    }
}
