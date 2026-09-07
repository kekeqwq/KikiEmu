namespace KikiEmu.Core.Repository;

public class SystemImageInfo
{
    public string Id { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public string Version { get; set; } = string.Empty;
    public int ApiLevel { get; set; } = 37;
    public string Architecture { get; set; } = "arm64-v8a";
    public string PageSize { get; set; } = "4KB";
    public bool HasGooglePlay { get; set; } = true;
    public string Revision { get; set; } = string.Empty;
    public DateTime? BuildDate { get; set; }
    public string DownloadUrl { get; set; } = string.Empty;
    public string ArchiveFileName { get; set; } = string.Empty;
    public long FileSizeBytes { get; set; }
    public string Sha1Checksum { get; set; } = string.Empty;
}
