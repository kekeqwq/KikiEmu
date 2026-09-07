// Copyright (c) 2026 KikiEmu Authors.
// Licensed under the GNU General Public License v3.0 (GPL-3.0).

using System.Diagnostics;
using KikiEmu.Core.Config;

namespace KikiEmu.Core.Engine;

public class EmulatorRunner
{
    public static HcsRunner LaunchInstance(InstanceInfo instance)
    {
        return HcsRunner.LaunchInstance(instance);
    }
}
