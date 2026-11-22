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

Every tagged release carries a complete installation, built by GitHub Actions from this
repository alone:

| File | Contents |
|---|---|
| `douglas-boot.img` | kernel 6.1 + device tree + initramfs, packed for the Amazon LK bootloader. Goes to the `boot` partition |
| `douglas-rootfs.img.gz` | Alpine Linux 3.21 with Phosh, NetworkManager, BlueZ, PulseAudio, the Wi-Fi/BT firmware, the kernel modules and the douglas scripts. ext4, grows to the whole partition on first boot. Goes to the `userdata` partition |
| `douglas-mods.tar.gz` | the loadable modules alone, for updating an existing install (`/root/mods/`) |
| `douglas-boot.config`, `SHA256SUMS` | the exact kernel config and checksums |

The kernel job builds the base tree plus `kernel/0001-douglas-port.patch` with Ubuntu's
aarch64 GCC. The rootfs job runs `tools/rootfs-inner.sh` in an aarch64 Alpine container
(QEMU user emulation): it installs the packages in `tools/rootfs-packages.txt`, lays the
`rootfs/` tree from this repo over them, creates the user account and writes the ext4
image. Pushes to `main` rebuild the kernel; the rootfs is built for tags and on demand
(Actions tab, "Run workflow").

**Default login:** user `user`, password `douglas`, passwordless `sudo`. SSH (dropbear)
is on from the first boot; the hostname is `douglas`, so `ssh user@douglas.lan` or
`ssh user@douglas` usually works once the tablet is on Wi-Fi. Change the password with
`passwd` after the first login. There is no root password; use `sudo`.

## Installing from TWRP

Prerequisites: a Fire HD 8 2017 with the bootloader unlocked through
[amonet](https://github.com/chaosmaster/amonet) and TWRP installed (hold Power +
Volume Down to enter it), `adb` on your computer, and the two images from a release.
Partitions: `boot` is `/dev/block/mmcblk0p5`, `userdata` is `/dev/block/mmcblk0p16`
(12 GB). **The rootfs image erases `userdata`, i.e. the Android user data.** The
Android system partitions stay intact, and TWRP stays available.

```sh
# 1. root filesystem (about 1.3 GB of data, streamed and uncompressed on the device)
gunzip -c douglas-rootfs.img.gz | adb exec-in "dd of=/dev/block/mmcblk0p16 bs=4M"
# 2. kernel
adb push douglas-boot.img /tmp/
adb shell "dd if=/tmp/douglas-boot.img of=/dev/block/mmcblk0p5 bs=1M conv=fsync && sync"
adb shell "dd if=/dev/block/mmcblk0p5 bs=$(stat -c %s douglas-boot.img) count=1 | sha256sum"
adb reboot
```

Compare the last hash with `SHA256SUMS`. The first boot takes a little longer while the
filesystem grows to the full partition; then Phosh starts and asks for Wi-Fi.
To update an existing install, flash only `douglas-boot.img` and, if the kernel
version changed, unpack `douglas-mods.tar.gz` into `/root/` on the device.

**Firmware notice.** `rootfs/lib/firmware` holds the MT6625L Wi-Fi/Bluetooth firmware
(`WMT_SOC.cfg`, `ROMv2_lm_patch_*`, `WIFI_RAM_CODE_8163`), the Goodix touch firmware
and the MD32 blobs taken from the device's Amazon firmware; they are the same files
[LibreEcho-mt8163-connectivity](https://github.com/aslater3/LibreEcho-mt8163-connectivity)
and the Amazon GPL kernel sources ship. They are MediaTek/Amazon proprietary binaries,
redistributed here only so the images are installable.

## Repository layout

| Path | Contents |
|---|---|
| `kernel/` | the port as a patch on top of [LibreEcho-Linux-6.1](https://github.com/aslater3/LibreEcho-Linux-6.1), plus the `.config`. Same port as a branch: [bjesus/LibreEcho-Linux-6.1, `douglas`](https://github.com/bjesus/LibreEcho-Linux-6.1/tree/douglas) |
| `douglas-initfs/` | the initramfs (busybox + `init`, switch_root to the rootfs, self-heal logic) |
| `douglas-alsa/` | ALSA machine driver (playback + capture), battery gauge and light sensor modules |
| `douglas-keys/` | Volume Up key driver |
| `rootfs/` | everything laid over the Alpine packages: PID 1 (`sbin/douglas-init`), `start-phosh`, hooks, firmware, defaults |
| `tools/` | rootfs build script and package list used by the workflow |
| `reference/` | the vendor DTB tail and the bootloader's `chosen` node (`atag,devinfo` holds the thermal calibration) |
| `douglas-audio-test/` | audio diagnostics, including vendor-kernel (TWRP) reference dumps |
| `mkboot_douglas.py`, `*.sh`, `*.py` | boot-image packing and amonet/flash helpers |
| `HANDOFF.md` | how to reach, build, flash and debug the device, what works, what is left |
| `TODO.md` | register-level detail per remaining task |

The Wi-Fi/Bluetooth driver itself is built into the kernel.
