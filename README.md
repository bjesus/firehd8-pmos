# Amazon Fire HD 8 (2017, "douglas") on mainline Linux

Mainline Linux 6.1 for the MT8163-based Amazon Fire HD 8 (7th generation, codename
`douglas`), running Alpine Linux with the Phosh mobile shell. This repository holds the
kernel port (as a patch), the kernel config, the initramfs, the out-of-tree modules, the
rootfs scripts, the flashing tools and the working notes.

Every tagged release ships a complete postmarketOS installation (boot image + rootfs)
built by GitHub Actions: see [Releases](../../releases), or the Actions tab for
per-commit builds. Install instructions below.

## Status

### Working

| Component | Notes |
|---|---|
| Kernel and SoC | Linux 6.1, 4x Cortex-A53, 1.4 GB RAM, eMMC; postmarketOS edge userspace |
| Display | 800x1280 DSI panel through `mediatek-drm`, PWM backlight |
| 3D graphics | Mali-T720 with Panfrost, GL ES 2.0 |
| Touchscreen | Goodix GT9271, 5-point |
| Buttons | Power, Volume Up, Volume Down |
| Wi-Fi | MT6625L, WPA2, DHCPv4 and DHCPv6, NetworkManager; stable per-device MAC (locally administered, the factory one isn't read yet) |
| Bluetooth | BR/EDR and BLE through BlueZ, scanning and pairing |
| Audio playback | Speaker and headphone jack with automatic switching, hardware volume |
| Audio capture | Built-in microphone |
| Battery and charging | bq24297 charger at 1.5 A, MT6323 voltage gauge |
| Sensors | BMA253 accelerometer (screen rotation), JSA1212 ambient light |
| CPU frequency scaling | 600 to 1050 MHz, six steps, `schedutil` |
| Thermal | SoC die sensor with calibration, CPU throttling at 75/85 °C |
| Real-time clock | Keeps time across power-off, RTC wake alarm |
| Suspend and resume | s2idle via `loginctl suspend`; power key and RTC wake |
| USB | postmarketOS USB modes (usb-signaller): developer networking by default, MTP/tethering selectable |
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

The port is packaged as a **postmarketOS device** (postmarketOS edge, now called Nura).
`.github/workflows/pmos.yml` runs pmbootstrap on the GitHub runner with the `pmaports/`
overlay from this repository and produces, for every push and every `v*` tag:

| File | Contents |
|---|---|
| `douglas-pmos-boot.img` | kernel 6.1 + vendor DTB + stage-1 initramfs, packed for the Amazon LK bootloader (7.9 MB, the boot partition is 16 MiB). Goes to `boot` |
| `douglas-pmos-rootfs.img.gz` | postmarketOS with Phosh: a 3 GB disk image with a `pmOS_boot` and a `pmOS_root` partition. Goes to `userdata` |
| `SHA256SUMS` | checksums of both |

Userspace is current postmarketOS edge (Phosh 0.59 at the time of writing) and updates
with `sudo apk upgrade`. The device packages (`linux-amazon-douglas`,
`device-amazon-douglas`, `firmware-amazon-douglas`, `amazon-douglas-config`) are built from
`pmaports/` and published to the package repository the image trusts, so kernel and
driver updates arrive the same way.

**Default login:** user `user`, password `douglas`, `sudo`. SSH is enabled; the hostname
is `douglas` (`ssh user@douglas.lan` on most home networks). Change the password with
`passwd`.

**Always a way out:** holding Power for about 8 seconds resets the tablet in hardware
(PMIC), whatever the software is doing; hold Power + Volume Down right after to reach
TWRP.

## Installing from TWRP

Prerequisites: a Fire HD 8 2017 with the bootloader unlocked through
[amonet](https://github.com/chaosmaster/amonet) and TWRP installed (hold Power +
Volume Down from power-on), `adb` on your computer, and the two images from a release.
Partitions: `boot` is `/dev/block/mmcblk0p5`, `userdata` is `/dev/block/mmcblk0p16`
(12 GB). **The rootfs image erases `userdata`, i.e. the Android user data.** The
Android system partitions stay intact and TWRP stays available.

```sh
# in TWRP, over adb
adb shell "umount /data 2>/dev/null; true"
adb push douglas-pmos-boot.img /tmp/
adb shell "dd if=/tmp/douglas-pmos-boot.img of=/dev/block/mmcblk0p5 bs=1M conv=fsync && sync"
gunzip -c douglas-pmos-rootfs.img.gz | adb exec-in "dd of=/dev/block/mmcblk0p16 bs=4M; sync"
adb reboot
```

The rootfs write takes about 15 minutes over USB 2. The first boot grows the root
filesystem to the whole partition, then Phosh starts.

**Updating:** `sudo apk upgrade` on the tablet. A new kernel package regenerates
`/boot/boot.img`; copy it to the boot partition with
`sudo dd if=/boot/boot.img of=/dev/mmcblk0p5 bs=1M conv=fsync` (automating this is on
the list).

**Firmware notice.** `firmware-amazon-douglas` carries the MT6625L Wi-Fi/Bluetooth
firmware (`WMT_SOC.cfg`, `ROMv2_lm_patch_*`, `WIFI_RAM_CODE_8163`), the Goodix touch
firmware and the MD32 blobs taken from the device's Amazon firmware; they are the same
files [LibreEcho-mt8163-connectivity](https://github.com/aslater3/LibreEcho-mt8163-connectivity)
and the Amazon GPL kernel sources ship. They are MediaTek/Amazon proprietary binaries,
redistributed only so the images are installable.

## Building it yourself

With pmbootstrap on any Linux machine (the way postmarketOS devices are built):

```sh
git clone https://gitlab.postmarketos.org/postmarketOS/pmaports.git
rm -rf pmaports/device/archived/*-amazon-douglas      # upstream's old downstream port
cp -r firehd8-pmos/pmaports/. pmaports/
pmbootstrap config aports $PWD/pmaports
pmbootstrap init          # device amazon-douglas, UI phosh, systemd
pmbootstrap install --password douglas && pmbootstrap export
```

The kernel is built from the fork tag named in `linux-amazon-douglas/APKBUILD`
(`bjesus/LibreEcho-Linux-6.1`, branch `douglas`); the same changes are
`kernel/0001-douglas-port.patch`. The earlier hand-rolled Alpine image
(`tools/rootfs-inner.sh`, workflow `build.yml`) is kept as a manual-only fallback.

## Repository layout

| Path | Contents |
|---|---|
| `kernel/` | the port as a patch on top of [LibreEcho-Linux-6.1](https://github.com/aslater3/LibreEcho-Linux-6.1), plus the `.config`. Same port as a branch: [bjesus/LibreEcho-Linux-6.1, `douglas`](https://github.com/bjesus/LibreEcho-Linux-6.1/tree/douglas) |
| `douglas-initfs/` | the initramfs (busybox + `init`, switch_root to the rootfs, self-heal logic) |
| `douglas-alsa/` | ALSA machine driver (playback + capture), battery gauge and light sensor modules |
| `douglas-keys/` | Volume Up key driver |
| `pmaports/` | the postmarketOS device overlay: `device-amazon-douglas`, `linux-amazon-douglas`, `firmware-amazon-douglas`, `amazon-douglas-config` |
| `tools/` | `pmb` (pmbootstrap wrapper), `update-checksums.py`, the legacy Alpine rootfs builder |
| `rootfs/` | legacy: files of the hand-rolled Alpine image |
| `reference/` | the vendor DTB tail and the bootloader's `chosen` node (`atag,devinfo` holds the thermal calibration) |
| `douglas-audio-test/` | audio diagnostics, including vendor-kernel (TWRP) reference dumps |
| `mkboot_douglas.py`, `*.sh`, `*.py` | boot-image packing and amonet/flash helpers |
| `HANDOFF.md` | how to reach, build, flash and debug the device, what works, what is left |
| `TODO.md` | register-level detail per remaining task |

The Wi-Fi/Bluetooth driver itself is built into the kernel.
