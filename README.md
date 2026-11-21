# Amazon Fire HD 8 (2017, "douglas") on mainline Linux / postmarketOS

Working notes, out-of-tree modules, initramfs, flashing tools and reference data for
running mainline Linux 6.1 (Alpine/postmarketOS + Phosh) on the MT8163-based Fire HD 8.

Start with **[HANDOFF.md](HANDOFF.md)** (how to reach, build, flash and debug the device,
what works, what is left) and **[TODO.md](TODO.md)** (register-level detail per task).

| Path | Contents |
|---|---|
| `kernel/` | the kernel port as a patch on top of [LibreEcho-Linux-6.1](https://github.com/aslater3/LibreEcho-Linux-6.1), plus the `.config` |
| `douglas-alsa/` | ALSA driver (playback + capture) and battery/ALS modules |
| `douglas-keys/` | Volume Up key driver |
| `douglas-initfs/` | the real initramfs (switch_root to the rootfs) |
| `douglas-audio-test/` | audio diagnostics, incl. the vendor-kernel (TWRP) reference dumps |
| `reference/` | the device's vendor DTB and the bootloader FDT `chosen` node (`atag,devinfo` holds the thermal calibration) |
| `mkboot_douglas.py`, `*.sh`, `*.py` | boot-image packing and amonet/flash helpers |

Wi-Fi/Bluetooth use the separately maintained
[LibreEcho-mt8163-connectivity](https://github.com/aslater3/LibreEcho-mt8163-connectivity) modules.
