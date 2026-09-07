// Copyright (c) 2026 KikiEmu Authors.
// Licensed under the GNU General Public License v3.0 (GPL-3.0).

using System.Runtime.InteropServices;

namespace KikiEmu.Core.Engine;

/// <summary>
/// P/Invoke bindings for Microsoft Windows Host Compute System (computecore.dll)
/// and Virtual Disk Service (virtdisk.dll).
/// </summary>
public static class HcsInterop
{
    private const string ComputeCoreDll = "computecore.dll";
    private const string VirtDiskDll = "virtdisk.dll";

    [DllImport(ComputeCoreDll, ExactSpelling = true)]
    public static extern IntPtr HcsCreateOperation(IntPtr context, IntPtr callback);

    [DllImport(ComputeCoreDll, ExactSpelling = true)]
    public static extern void HcsCloseOperation(IntPtr operation);

    [DllImport(ComputeCoreDll, ExactSpelling = true)]
    public static extern int HcsWaitForOperationResult(
        IntPtr operation,
        uint timeoutMs,
        out IntPtr resultDocument);

    [DllImport(ComputeCoreDll, ExactSpelling = true, CharSet = CharSet.Unicode)]
    public static extern int HcsCreateComputeSystem(
        string id,
        string configuration,
        IntPtr operation,
        IntPtr securityDescriptor,
        out IntPtr computeSystem);

    [DllImport(ComputeCoreDll, ExactSpelling = true, CharSet = CharSet.Unicode)]
    public static extern int HcsStartComputeSystem(
        IntPtr computeSystem,
        IntPtr operation,
        string? options);

    [DllImport(ComputeCoreDll, ExactSpelling = true, CharSet = CharSet.Unicode)]
    public static extern int HcsGetComputeSystemProperties(
        IntPtr computeSystem,
        IntPtr operation,
        string? propertyQuery);

    [DllImport(ComputeCoreDll, ExactSpelling = true, CharSet = CharSet.Unicode)]
    public static extern int HcsTerminateComputeSystem(
        IntPtr computeSystem,
        IntPtr operation,
        string? options);

    [DllImport(ComputeCoreDll, ExactSpelling = true)]
    public static extern void HcsCloseComputeSystem(IntPtr computeSystem);

    // virtdisk.dll definitions
    public const uint VIRTUAL_STORAGE_TYPE_DEVICE_VHDX = 2;

    [StructLayout(LayoutKind.Sequential)]
    public struct VIRTUAL_STORAGE_TYPE
    {
        public uint DeviceId;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] VendorId;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct CREATE_VIRTUAL_DISK_PARAMETERS_V2
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] UniqueId;
        public ulong MaximumSize;
        public uint BlockSizeInBytes;
        public uint SectorSizeInBytes;
        public uint PhysicalSectorSizeInBytes;
        public string? ParentPath;
        public string? SourcePath;
        public uint OpenFlags;
        public VIRTUAL_STORAGE_TYPE ParentVirtualStorageType;
        public VIRTUAL_STORAGE_TYPE SourceVirtualStorageType;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] ResiliencyGuid;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct CREATE_VIRTUAL_DISK_PARAMETERS
    {
        public uint Version;
        public CREATE_VIRTUAL_DISK_PARAMETERS_V2 Version2;
    }

    [DllImport(VirtDiskDll, ExactSpelling = true, CharSet = CharSet.Unicode)]
    public static extern int CreateVirtualDisk(
        ref VIRTUAL_STORAGE_TYPE virtualStorageType,
        string path,
        uint virtualDiskAccessMask,
        IntPtr securityDescriptor,
        uint flags,
        uint providerSpecificFlags,
        ref CREATE_VIRTUAL_DISK_PARAMETERS parameters,
        IntPtr overlapped,
        out IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool CloseHandle(IntPtr hObject);
}
