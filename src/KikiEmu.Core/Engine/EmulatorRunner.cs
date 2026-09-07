using System.Diagnostics;
using KikiEmu.Core.Config;

namespace KikiEmu.Core.Engine;

public class EmulatorRunner
{
    public static Process LaunchInstance(InstanceInfo instance, bool headless = false)
    {
        var engineBinary = EngineManager.FindEngineBinary();
        if (string.IsNullOrEmpty(engineBinary) || !File.Exists(engineBinary))
        {
            throw new FileNotFoundException(
                "KikiEmu engine binary was not found. Please ensure the engine is installed via 'kikiemu run' or KikiEmu setup.");
        }

        var storage = instance.StoragePath;
        var systemImg = Path.Combine(storage, "system.img");
        var vendorImg = Path.Combine(storage, "vendor.img");
        var userdataImg = Path.Combine(storage, "userdata.img");

        var isHabumi = Path.GetFileName(engineBinary).Equals("Habumi.exe", StringComparison.OrdinalIgnoreCase);

        var psi = new ProcessStartInfo
        {
            FileName = engineBinary,
            WorkingDirectory = storage,
            UseShellExecute = false
        };

        if (isHabumi)
        {
            // Habumi engine uses tailored arguments or config
            psi.ArgumentList.Add("--data-dir");
            psi.ArgumentList.Add(storage);
            psi.ArgumentList.Add("--width");
            psi.ArgumentList.Add(instance.DisplayWidth.ToString());
            psi.ArgumentList.Add("--height");
            psi.ArgumentList.Add(instance.DisplayHeight.ToString());
            psi.ArgumentList.Add("--fps");
            psi.ArgumentList.Add(instance.RefreshRate.ToString());
            if (instance.EnablePhysicalKeyboard)
            {
                psi.ArgumentList.Add("--enable-usb-keyboard");
            }
        }
        else
        {
            // Direct QEMU AArch64 with WHPX
            psi.ArgumentList.Add("-M");
            psi.ArgumentList.Add("virt,accel=whpx");
            psi.ArgumentList.Add("-cpu");
            psi.ArgumentList.Add("host");
            psi.ArgumentList.Add("-smp");
            psi.ArgumentList.Add(instance.CpuCores.ToString());
            psi.ArgumentList.Add("-m");
            psi.ArgumentList.Add(instance.MemoryMb.ToString());

            // Storage disks
            if (File.Exists(systemImg))
            {
                psi.ArgumentList.Add("-drive");
                psi.ArgumentList.Add($"file={systemImg},format=raw,if=none,id=system,read-only=on");
                psi.ArgumentList.Add("-device");
                psi.ArgumentList.Add("virtio-blk-pci,drive=system");
            }
            if (File.Exists(vendorImg))
            {
                psi.ArgumentList.Add("-drive");
                psi.ArgumentList.Add($"file={vendorImg},format=raw,if=none,id=vendor,read-only=on");
                psi.ArgumentList.Add("-device");
                psi.ArgumentList.Add("virtio-blk-pci,drive=vendor");
            }
            if (File.Exists(userdataImg))
            {
                psi.ArgumentList.Add("-drive");
                psi.ArgumentList.Add($"file={userdataImg},format=raw,if=none,id=userdata");
                psi.ArgumentList.Add("-device");
                psi.ArgumentList.Add("virtio-blk-pci,drive=userdata");
            }

            // Real Hardware Peripherals
            if (instance.EnablePhysicalKeyboard)
            {
                // Real USB HID Keyboard peripheral - Android Input subsystem will recognize external keyboard attached!
                psi.ArgumentList.Add("-device");
                psi.ArgumentList.Add("usb-kbd");
            }

            if (instance.EnableMultiTouch)
            {
                psi.ArgumentList.Add("-device");
                psi.ArgumentList.Add("usb-tablet");
            }

            // Networking and ADB forward
            psi.ArgumentList.Add("-netdev");
            psi.ArgumentList.Add("user,id=net0,hostfwd=tcp::5555-:5555");
            psi.ArgumentList.Add("-device");
            psi.ArgumentList.Add("virtio-net-pci,netdev=net0");

            // Display & GPU
            if (headless)
            {
                psi.ArgumentList.Add("-nographic");
            }
        }

        var process = Process.Start(psi);
        if (process == null)
        {
            throw new InvalidOperationException("Failed to launch emulator process.");
        }

        return process;
    }
}
