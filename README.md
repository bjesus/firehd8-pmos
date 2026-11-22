# Amazon Fire HD 8 (2017, "douglas") on mainline Linux

Mainline Linux 6.1 for the MT8163-based Amazon Fire HD 8 (7th generation, codename
`douglas`), running Alpine Linux with the Phosh mobile shell. This repository holds the
kernel port (as a patch), the kernel config, the initramfs, the out-of-tree modules, the
rootfs scripts, the flashing tools and the working notes.

The **[build workflow](.github/workflows/build.yml)** produces a boot image you can flash
from TWRP: see [Releases](../../releases) for tagged builds and the Actions tab for
per-commit builds.

## Status

### Working

| Component | Notes |
|---|---|
| Kernel and SoC | Linux 6.1, 4x Cortex-A53, 1.4 GB RAM, eMMC |
| Display | 800x1280 DSI panel through `mediatek-drm`, PWM backlight |
| 3D graphics | Mali-T720 with Panfrost, GL ES 2.0 |
| Touchscreen | Goodix GT9271, 5-point |
| Buttons | Power, Volume Up, Volume Down |
| Wi-Fi | MT6625L, WPA2, DHCPv4 and DHCPv6, NetworkManager |
| Bluetooth | BR/EDR and BLE through BlueZ, scanning and pairing |
| Audio playback | Speaker and headphone jack with automatic switching, hardware volume |
| Audio capture | Built-in microphone |
| Battery and charging | bq24297 charger at 1.5 A, MT6323 voltage gauge |
| Sensors | BMA253 accelerometer (screen rotation), JSA1212 ambient light |
| CPU frequency scaling | 600 to 1050 MHz, six steps, `schedutil` |
| Thermal | SoC die sensor with calibration, CPU throttling at 75/85 °C |
| Real-time clock | Keeps time across power-off, RTC wake alarm |
| Suspend and resume | s2idle via `loginctl suspend`; power key and RTC wake |
| USB | Ethernet gadget for development access (usb0) |
| Watchdog | Hardware watchdog with pretimeout panic |
| Recovery | Self-healing boot: three failed boots reflash the last known-good image |

### Not tested

| Component | Why |
|---|---|
| MicroSD slot | Fully specified in `TODO.md`, no card was available to test |
| Headset microphone | Only the built-in mic input is wired up; the headset mic mux exists but was not tried |
| USB host (OTG) | Only gadget mode has been used |
| Charging from a weak or non-standard charger | Only a standard 5 V supply was used |

### Not working or not supported

| Component | Status |
|---|---|
| CPU above 1050 MHz | The vendor runs 1133/1216/1300 MHz at higher voltages; raising the CPU rail is blocked on an unresolved PMIC question |
| Cameras | No driver work done |
| Hardware video decoding | Not attempted, needs the IOMMU |
| IOMMU (M4U) | Enabling it breaks the GPU's probe order; disabled, nothing currently needs it |
| TMP103 board temperature sensors | Nothing answers on the I2C bus; either unpowered or not fitted |
| Deep sleep | Only s2idle; the Wi-Fi chip is powered off around sleep, so no wake-on-WLAN |
| Changing the Wi-Fi MAC address | The vendor driver has no set-MAC support |
| Serial console | No test points identified |

## What the build produces

The workflow builds the kernel (base tree plus `kernel/0001-douglas-port.patch`, config
`kernel/douglas-6.1.config`), embeds the initramfs from `douglas-initfs/`, and packs it
into an Amazon-LK-compatible `douglas-boot.img`. It also builds the loadable modules
(`douglas-mods.tar.gz`: the display stack and the out-of-tree audio, keys and battery
modules) and publishes the exact `.config`.

The boot image contains only the kernel. The initramfs mounts `/dev/mmcblk0p16`
and runs `/sbin/douglas-init` from there, so you need a root filesystem on that
partition. The scripts that rootfs expects are in `rootfs/` (`sbin/douglas-init`,
`usr/local/sbin/start-phosh`, the NetworkManager and elogind hooks); the modules from
`douglas-mods.tar.gz` go to `/root/mods/` on it. Preparing that rootfs is not automated
yet. `HANDOFF.md` describes the setup that is running today.

## Flashing from TWRP

Prerequisites: a Fire HD 8 2017 with the bootloader unlocked through
[amonet](https://github.com/chaosmaster/amonet) and TWRP installed, and a rootfs on
`mmcblk0p16` as described above. The boot image goes to the `boot` partition,
`/dev/block/mmcblk0p5`.

```sh
adb push douglas-boot.img /tmp/
adb shell "dd if=/tmp/douglas-boot.img of=/dev/block/mmcblk0p5 bs=1M conv=fsync && sync"
adb shell "dd if=/dev/block/mmcblk0p5 bs=$(stat -c %s douglas-boot.img) count=1 | sha256sum"
adb reboot
```

Compare the last hash with `SHA256SUMS`. Flashing only replaces the kernel; the
Android system partitions are left untouched, and TWRP remains available with
Power + Volume Down.

## Repository layout

| Path | Contents |
|---|---|
| `kernel/` | the port as a patch on top of [LibreEcho-Linux-6.1](https://github.com/aslater3/LibreEcho-Linux-6.1), plus the `.config`. Same port as a branch: [bjesus/LibreEcho-Linux-6.1, `douglas`](https://github.com/bjesus/LibreEcho-Linux-6.1/tree/douglas) |
| `douglas-initfs/` | the initramfs (busybox + `init`, switch_root to the rootfs, self-heal logic) |
| `douglas-alsa/` | ALSA machine driver (playback + capture), battery gauge and light sensor modules |
| `douglas-keys/` | Volume Up key driver |
| `rootfs/` | scripts and config installed on the Alpine rootfs |
| `reference/` | the vendor DTB tail and the bootloader's `chosen` node (`atag,devinfo` holds the thermal calibration) |
| `douglas-audio-test/` | audio diagnostics, including vendor-kernel (TWRP) reference dumps |
| `mkboot_douglas.py`, `*.sh`, `*.py` | boot-image packing and amonet/flash helpers |
| `HANDOFF.md` | how to reach, build, flash and debug the device, what works, what is left |
| `TODO.md` | register-level detail per remaining task |

The Wi-Fi/Bluetooth driver is built into the kernel; its firmware files on the rootfs
(`/lib/firmware`: `WMT_SOC.cfg`, `ROMv2_lm_patch_*`, `WIFI_RAM_CODE_8163`) come from
[LibreEcho-mt8163-connectivity](https://github.com/aslater3/LibreEcho-mt8163-connectivity).
