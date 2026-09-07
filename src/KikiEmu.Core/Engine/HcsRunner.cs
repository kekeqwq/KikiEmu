// Copyright (c) 2026 KikiEmu Authors.
// Licensed under the GNU General Public License v3.0 (GPL-3.0).

using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using KikiEmu.Core.Config;

namespace KikiEmu.Core.Engine;

/// <summary>
/// Native Windows 11 Hyper-V Host Compute System (HCS) MicroVM Runner for Android.
/// Boots ARM64 GKI Linux kernel directly with microsecond latency, bypasses WHPX/QEMU crashes.
/// </summary>
public class HcsRunner : IDisposable
{
    private IntPtr _computeSystem = IntPtr.Zero;
    private readonly string _systemId;
    private bool _isDisposed;

    public HcsRunner(string systemId)
    {
        _systemId = systemId;
    }

    public static HcsRunner LaunchInstance(InstanceInfo instance)
    {
        var runner = new HcsRunner($"kikiemu-{instance.Id}");
        runner.Start(instance);
        return runner;
    }

    public static void CleanExistingSystem(string systemId)
    {
        try
        {
            var openHr = HcsInterop.HcsOpenComputeSystem(systemId, 0x10000000, out var existingSys);
            if (openHr == 0 && existingSys != IntPtr.Zero)
            {
                var termOp = HcsInterop.HcsCreateOperation(IntPtr.Zero, IntPtr.Zero);
                try
                {
                    HcsInterop.HcsTerminateComputeSystem(existingSys, termOp, null);
                    HcsInterop.HcsWaitForOperationResult(termOp, 2000, out _);
                }
                finally
                {
                    HcsInterop.HcsCloseOperation(termOp);
                    HcsInterop.HcsCloseComputeSystem(existingSys);
                }
            }
        }
        catch { }
    }

    public void Start(InstanceInfo instance)
    {
        // Always ensure any stale system with this ID is terminated
        CleanExistingSystem(_systemId);

        var storage = instance.StoragePath;
        var kernelPath = Path.Combine(storage, "kernel");
        var ramdiskPath = Path.Combine(storage, "ramdisk.img");

        if (!File.Exists(kernelPath))
        {
            var ranchu = Path.Combine(storage, "kernel-ranchu");
            if (File.Exists(ranchu))
            {
                DecompressKernelIfGzip(ranchu, kernelPath);
            }
            else
            {
                throw new FileNotFoundException($"Android kernel not found in storage directory: {storage}");
            }
        }

        // Grant VM group permissions to storage files
        GrantVmPermissions(kernelPath);
        if (File.Exists(ramdiskPath))
        {
            GrantVmPermissions(ramdiskPath);
        }

        var hcsConfig = new Dictionary<string, object>
        {
            ["Owner"] = "KikiEmu",
            ["SchemaVersion"] = new { Major = 2, Minor = 2 },
            ["VirtualMachine"] = new Dictionary<string, object>
            {
                ["StopOnReset"] = false,
                ["Chipset"] = new
                {
                    LinuxKernelDirect = new
                    {
                        KernelFilePath = kernelPath,
                        InitRdPath = File.Exists(ramdiskPath) ? ramdiskPath : null,
                        KernelCmdLine = "console=hvc0 panic=1 androidboot.hardware=ranchu earlycon"
                    }
                },
                ["ComputeTopology"] = new
                {
                    Memory = new { SizeInMB = instance.MemoryMb },
                    Processor = new { Count = instance.CpuCores }
                }
            }
        };

        var configJson = JsonSerializer.Serialize(hcsConfig, new JsonSerializerOptions { WriteIndented = true });

        // 1. Create Compute System
        var createOp = HcsInterop.HcsCreateOperation(IntPtr.Zero, IntPtr.Zero);
        try
        {
            var hr = HcsInterop.HcsCreateComputeSystem(_systemId, configJson, createOp, IntPtr.Zero, out _computeSystem);
            if (hr != 0)
            {
                throw new InvalidOperationException($"HcsCreateComputeSystem failed with HRESULT: 0x{hr:X8}");
            }

            var waitHr = HcsInterop.HcsWaitForOperationResult(createOp, 10000, out var resultDocPtr);
            if (waitHr != 0)
            {
                var errDoc = Marshal.PtrToStringUni(resultDocPtr);
                throw new InvalidOperationException($"HcsCreateComputeSystem operation failed (0x{waitHr:X8}): {errDoc}");
            }
        }
        finally
        {
            HcsInterop.HcsCloseOperation(createOp);
        }

        // 2. Start Compute System
        var startOp = HcsInterop.HcsCreateOperation(IntPtr.Zero, IntPtr.Zero);
        try
        {
            var hr = HcsInterop.HcsStartComputeSystem(_computeSystem, startOp, null);
            if (hr != 0)
            {
                throw new InvalidOperationException($"HcsStartComputeSystem failed with HRESULT: 0x{hr:X8}");
            }

            var waitHr = HcsInterop.HcsWaitForOperationResult(startOp, 15000, out var resultDocPtr);
            if (waitHr != 0)
            {
                var errDoc = Marshal.PtrToStringUni(resultDocPtr);
                throw new InvalidOperationException($"HcsStartComputeSystem operation failed (0x{waitHr:X8}): {errDoc}");
            }
        }
        finally
        {
            HcsInterop.HcsCloseOperation(startOp);
        }
    }

    public void Terminate()
    {
        if (_computeSystem != IntPtr.Zero)
        {
            var termOp = HcsInterop.HcsCreateOperation(IntPtr.Zero, IntPtr.Zero);
            try
            {
                HcsInterop.HcsTerminateComputeSystem(_computeSystem, termOp, null);
                HcsInterop.HcsWaitForOperationResult(termOp, 5000, out _);
            }
            catch { }
            finally
            {
                HcsInterop.HcsCloseOperation(termOp);
                HcsInterop.HcsCloseComputeSystem(_computeSystem);
                _computeSystem = IntPtr.Zero;
            }
        }
    }

    public static void GrantVmPermissions(string filePath)
    {
        try
        {
            using var proc = Process.Start(new ProcessStartInfo
            {
                FileName = "icacls.exe",
                Arguments = $"\"{filePath}\" /grant *S-1-5-83-0:(F)",
                UseShellExecute = false,
                CreateNoWindow = true
            });
            proc?.WaitForExit(3000);
        }
        catch { }
    }

    public static void CreateDynamicVhdx(string vhdxPath, ulong sizeBytes)
    {
        if (File.Exists(vhdxPath))
        {
            File.Delete(vhdxPath);
        }

        var st = new HcsInterop.VIRTUAL_STORAGE_TYPE
        {
            DeviceId = HcsInterop.VIRTUAL_STORAGE_TYPE_DEVICE_VHDX,
            VendorId = new byte[] { 0xec, 0xa2, 0x6f, 0xec, 0xa0, 0xcb, 0xe5, 0x42, 0xb2, 0xe5, 0xda, 0x7e, 0x4e, 0x6b, 0x45, 0x63 }
        };

        var p = new HcsInterop.CREATE_VIRTUAL_DISK_PARAMETERS
        {
            Version = 2,
            Version2 = new HcsInterop.CREATE_VIRTUAL_DISK_PARAMETERS_V2
            {
                MaximumSize = sizeBytes,
                SectorSizeInBytes = 512,
                UniqueId = new byte[16],
                ResiliencyGuid = new byte[16]
            }
        };

        var hr = HcsInterop.CreateVirtualDisk(
            ref st,
            vhdxPath,
            0,
            IntPtr.Zero,
            0,
            0,
            ref p,
            IntPtr.Zero,
            out var handle);

        if (hr != 0)
        {
            throw new InvalidOperationException($"CreateVirtualDisk failed with error: 0x{hr:X8}");
        }

        HcsInterop.CloseHandle(handle);
        GrantVmPermissions(vhdxPath);
    }

    private static void DecompressKernelIfGzip(string sourceGzip, string destDecompressed)
    {
        using var inFs = File.OpenRead(sourceGzip);
        var header = new byte[2];
        inFs.ReadExactly(header, 0, 2);
        inFs.Position = 0;

        if (header[0] == 0x1f && header[1] == 0x8b)
        {
            using var gz = new System.IO.Compression.GZipStream(inFs, System.IO.Compression.CompressionMode.Decompress);
            using var outFs = File.Create(destDecompressed);
            gz.CopyTo(outFs);
        }
        else
        {
            File.Copy(sourceGzip, destDecompressed, overwrite: true);
        }
    }

    public void Dispose()
    {
        if (!_isDisposed)
        {
            Terminate();
            _isDisposed = true;
            GC.SuppressFinalize(this);
        }
    }
}
