namespace KikiEmu.Core.Config;

public class InstanceInfo
{
    public string Id { get; set; } = string.Empty;
    public string Name { get; set; } = string.Empty;
    public string SystemId { get; set; } = string.Empty;
    public string StoragePath { get; set; } = string.Empty;
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
    
    // Hardware configuration
    public int CpuCores { get; set; } = 4;
    public int MemoryMb { get; set; } = 4096;
    public int DisplayWidth { get; set; } = 1080;
    public int DisplayHeight { get; set; } = 2400;
    public int DisplayDpi { get; set; } = 420;
    public int RefreshRate { get; set; } = 120;
    
    // Peripherals
    public bool EnablePhysicalKeyboard { get; set; } = true;
    public bool EnableMultiTouch { get; set; } = true;
}
