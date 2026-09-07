using System.Net.Http.Headers;
using System.Text.Json;
using System.Xml.Linq;
using KikiEmu.Core.Config;

namespace KikiEmu.Core.Repository;

public class GoogleRepoClient
{
    private static readonly HttpClient HttpClient = new()
    {
        Timeout = TimeSpan.FromSeconds(20)
    };

    private const string BaseRepoUrl = "https://dl.google.com/android/repository/sys-img/google_apis_playstore/";
    private const string ManifestXmlUrl = BaseRepoUrl + "sys-img2-3.xml";

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true
    };

    public static List<SystemImageInfo> GetDefaultSystemImages()
    {
        return new List<SystemImageInfo>
        {
            new()
            {
                Id = "android17",
                DisplayName = "Android 17.0 (API 37) Google Play",
                Version = "17.0",
                ApiLevel = 37,
                Architecture = "arm64-v8a",
                PageSize = "4KB",
                HasGooglePlay = true,
                Revision = "r06",
                BuildDate = new DateTime(2026, 7, 27, 20, 53, 21, DateTimeKind.Utc),
                DownloadUrl = BaseRepoUrl + "arm64-v8a-37.0_r06.zip",
                ArchiveFileName = "arm64-v8a-37.0_r06.zip",
                FileSizeBytes = 2257751510,
                Sha1Checksum = "642cf494464b4d1d4d37fa50630fe153599be3fb"
            },
            new()
            {
                Id = "android17-16k",
                DisplayName = "Android 17.2 (API 37) Google Play (16KB Page)",
                Version = "17.2",
                ApiLevel = 37,
                Architecture = "arm64-v8a",
                PageSize = "16KB",
                HasGooglePlay = true,
                Revision = "r04",
                BuildDate = new DateTime(2026, 8, 28, 22, 0, 13, DateTimeKind.Utc),
                DownloadUrl = BaseRepoUrl + "arm64-v8a-playstore-ps16k-37.2_r04.zip",
                ArchiveFileName = "arm64-v8a-playstore-ps16k-37.2_r04.zip",
                FileSizeBytes = 2379947595,
                Sha1Checksum = "f61a73f05de750fc158aeab8205f692ce6364e6c"
            },
            new()
            {
                Id = "android-canary",
                DisplayName = "Android Canary Google Play (16KB Page)",
                Version = "Canary",
                ApiLevel = 37,
                Architecture = "arm64-v8a",
                PageSize = "16KB",
                HasGooglePlay = true,
                Revision = "r15",
                BuildDate = new DateTime(2026, 8, 7, 17, 27, 23, DateTimeKind.Utc),
                DownloadUrl = BaseRepoUrl + "arm64-v8a-playstore-ps16k-CANARY_r15.zip",
                ArchiveFileName = "arm64-v8a-playstore-ps16k-CANARY_r15.zip",
                FileSizeBytes = 2235010994,
                Sha1Checksum = "83b56b336a7d2e4bc3228de357fdabb718f2afff"
            }
        };
    }

    public static string GetCatalogCacheFilePath() =>
        Path.Combine(KikiConfig.GetConfigDirectory(), "systems.json");

    public static List<SystemImageInfo> LoadAvailableSystems(bool forceRefresh = false, Action<string>? onProgress = null)
    {
        var cachePath = GetCatalogCacheFilePath();

        if (!forceRefresh && File.Exists(cachePath))
        {
            try
            {
                var cached = JsonSerializer.Deserialize<List<SystemImageInfo>>(File.ReadAllText(cachePath), JsonOpts);
                if (cached != null && cached.Count > 0)
                {
                    return cached;
                }
            }
            catch
            {
                // Fall back to refresh or defaults
            }
        }

        if (forceRefresh)
        {
            try
            {
                var updated = FetchFromGoogleAsync(onProgress).GetAwaiter().GetResult();
                if (updated != null && updated.Count > 0)
                {
                    File.WriteAllText(cachePath, JsonSerializer.Serialize(updated, JsonOpts));
                    return updated;
                }
            }
            catch (Exception ex)
            {
                onProgress?.Invoke($"Warning: Online update failed ({ex.Message}), falling back to cached catalog.");
            }
        }

        var defaults = GetDefaultSystemImages();
        try
        {
            File.WriteAllText(cachePath, JsonSerializer.Serialize(defaults, JsonOpts));
        }
        catch { }
        return defaults;
    }

    public static async Task<List<SystemImageInfo>> FetchFromGoogleAsync(Action<string>? onProgress = null)
    {
        onProgress?.Invoke("Querying Google Android repository manifest...");
        using var request = new HttpRequestMessage(HttpMethod.Get, ManifestXmlUrl);
        request.Headers.UserAgent.Add(new ProductInfoHeaderValue("KikiEmu", "1.0"));

        var response = await HttpClient.SendAsync(request);
        response.EnsureSuccessStatusCode();

        var xmlBytes = await response.Content.ReadAsByteArrayAsync();
        using var stream = new MemoryStream(xmlBytes);
        var doc = XDocument.Load(stream);

        var ns = doc.Root?.Name.Namespace ?? XNamespace.None;
        var packages = doc.Descendants(ns + "remotePackage").ToList();

        var list = new List<SystemImageInfo>();

        // 1. Android 17 Standard (4KB)
        var a17Std = packages.FirstOrDefault(p =>
            (string?)p.Attribute("path") == "system-images;android-37.0;google_apis_playstore;arm64-v8a");
        if (a17Std != null)
        {
            var item = ParsePackage(a17Std, "android17", "Android 17.0 (API 37) Google Play", "17.0", 37, "4KB");
            if (item != null) list.Add(item);
        }

        // 2. Android 17.2 ps16k (Latest 16KB preview)
        var a17_16k = packages
            .Where(p => ((string?)p.Attribute("path") ?? "").Contains("android-37") &&
                        ((string?)p.Attribute("path") ?? "").Contains("ps16k;arm64-v8a"))
            .OrderByDescending(p => (string?)p.Attribute("path"))
            .FirstOrDefault();
        if (a17_16k != null)
        {
            var item = ParsePackage(a17_16k, "android17-16k", "Android 17.2 (API 37) Google Play (16KB Page)", "17.2", 37, "16KB");
            if (item != null) list.Add(item);
        }

        // 3. Android CANARY ps16k
        var canary = packages.FirstOrDefault(p =>
            (string?)p.Attribute("path") == "system-images;android-CANARY;google_apis_playstore_ps16k;arm64-v8a");
        if (canary != null)
        {
            var item = ParsePackage(canary, "android-canary", "Android Canary Google Play (16KB Page)", "Canary", 37, "16KB");
            if (item != null) list.Add(item);
        }

        // Fetch official HTTP headers for timestamps in parallel
        foreach (var img in list)
        {
            onProgress?.Invoke($"Checking build date for {img.Id}...");
            try
            {
                using var headReq = new HttpRequestMessage(HttpMethod.Head, img.DownloadUrl);
                headReq.Headers.UserAgent.Add(new ProductInfoHeaderValue("KikiEmu", "1.0"));
                using var headResp = await HttpClient.SendAsync(headReq);
                if (headResp.IsSuccessStatusCode)
                {
                    if (headResp.Content.Headers.LastModified.HasValue)
                    {
                        img.BuildDate = headResp.Content.Headers.LastModified.Value.UtcDateTime;
                    }
                    if (headResp.Content.Headers.ContentLength.HasValue)
                    {
                        img.FileSizeBytes = headResp.Content.Headers.ContentLength.Value;
                    }
                }
            }
            catch
            {
                // If HEAD fails, keep existing date
            }
        }

        return list;
    }

    private static SystemImageInfo? ParsePackage(XElement pkg, string id, string displayName, string version, int api, string pageSize)
    {
        var ns = pkg.Name.Namespace;
        var archive = pkg.Descendants(ns + "archive")
            .FirstOrDefault(a => (string?)a.Element(ns + "host-os") == null || (string?)a.Element(ns + "host-os") == "");
        if (archive == null)
        {
            archive = pkg.Descendants(ns + "archive").FirstOrDefault();
        }
        if (archive == null) return null;

        var url = (string?)archive.Descendants(ns + "url").FirstOrDefault();
        if (string.IsNullOrEmpty(url)) return null;

        var checksum = (string?)archive.Descendants(ns + "checksum").FirstOrDefault() ?? string.Empty;
        var sizeStr = (string?)archive.Descendants(ns + "size").FirstOrDefault();
        long.TryParse(sizeStr, out var size);

        var revElem = pkg.Element(ns + "revision");
        var rev = revElem != null ? string.Join(".", revElem.Elements().Select(e => e.Value)) : "";

        return new SystemImageInfo
        {
            Id = id,
            DisplayName = displayName,
            Version = version,
            ApiLevel = api,
            Architecture = "arm64-v8a",
            PageSize = pageSize,
            HasGooglePlay = true,
            Revision = $"r{rev}",
            DownloadUrl = url.StartsWith("http") ? url : BaseRepoUrl + url,
            ArchiveFileName = Path.GetFileName(url),
            FileSizeBytes = size,
            Sha1Checksum = checksum
        };
    }
}
