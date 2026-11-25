# Amazon Fire HD 8 (2017, `douglas`) — Mainline Linux Port: Working Handoff

Companion to **`TODO.md`**, which holds the per-task, register-level detail. This
file is the practical entry point: how to reach the device, how to build and
flash, how to debug it, what works today, and what is left.

Written 2026-11-21, updated through 2026-11-25. Current image: **`douglas-boot60.img`**, kernel `#32`, on the CI-built Alpine rootfs.

---

## 1. The device

| | |
|---|---|
| SoC | MediaTek MT8163, 4x Cortex-A53, 1.41 GB RAM |
| PMIC | MT6323 (via `pwrap` at `0x1000d000`) |
| Kernel | Mainline Linux 6.1.178, `arm64` |
| Userspace | Alpine 3.21 / postmarketOS + Phosh, rootfs on `/dev/mmcblk0p16` |
| Bootloader | Amazon LK (unlocked via amonet), boot image on `/dev/mmcblk0p5` |

The SoC is close to MT8173/MT8167/MT6735; when mainline lacks MT8163 support,
those are the drivers to read.

---

## 2. Reaching the device — read this first

The tablet hangs off a **relay laptop** called `stockholm-aladdin`. You cannot
reach the tablet directly.

```
you  ->  ssh root@stockholm-aladdin  ->  ssh root@10.42.0.2  (tablet, USB ethernet)
```

### The USB link wedges after every tablet reboot

This will waste your time if you do not know it. After the tablet reboots, the
host-side `cdc_ether` comes up with `carrier=1` but **frames never leave the
interface** (`tx` counter frozen, `tcpdump` sees nothing). `ip link set ... up`
does not fix it. Forcing USB re-enumeration does.

**Always run this on the relay before trying to reach the tablet:**

```sh
ssh root@stockholm-aladdin '/root/usbkick.sh'
```

It toggles `authorized` on the `0525:a4a1` gadget, re-adds `10.42.0.1/24`, and
waits for ping. It prints `link up on <iface>` or `link OK`. A typical one-liner:

```sh
ssh root@stockholm-aladdin '/root/usbkick.sh >/dev/null 2>&1; ssh root@10.42.0.2 "uptime"'
```

After a reboot, sleep ~70 s first, then retry `usbkick.sh` in a loop.

### Direct route over Wi-Fi (preferred since 2026-11-22)

Once Wi-Fi associates (see §8 "Wi-Fi association"), the tablet is reachable directly:
`ssh -i ~/.ssh/id_tablet root@douglas.lan` (the router's DNS resolves the DHCP hostname;
the IPv4 lease itself changes on every reconnect/resume, so do not hard-code it). The
DHCPv6 address `fdfc:d767:207::87b` also works but has been seen to stop answering after
a resume while v4 kept working. This is
faster than the relay and survives relay-side USB wedges. `scp` does not work (no
sftp-server on the tablet): stream with `cat file | ssh ... "cat > dest"`. Lesson from the night: tablet
commands that outlive a dropped USB link pin SSH channels on the relay's single
authenticated connection; wrap remote commands in `timeout`, and never `nmcli con down`
over a Wi-Fi session.

### Other routes

* **Wi-Fi**: see "Direct route over Wi-Fi" above (real MAC `00:08:22:a4:b1:fe`, no pinning).
* **TWRP / adb** — the recovery path, needs physical buttons. `adb` lives at
  `/nix/store/qalfgcmwsd2mmj652qk6ysajv20h2yzg-android-tools-35.0.2/bin/adb`
  on the relay. TWRP's kernel has **no `CONFIG_DEVMEM`**, so you cannot read
  physical memory from there.

### Shell quoting

Everything is nested SSH, and the tablet runs **busybox** (no `pgrep -c`,
no `ip -br`, no `xxd`, no `i2c-tools`). Deeply nested quoting breaks constantly.
**Write the script to a local file and pipe it in**:

```sh
ssh root@stockholm-aladdin 'cat > /tmp/s.sh; cat /tmp/s.sh | \
  ssh root@10.42.0.2 "cat > /tmp/s.sh; sh /tmp/s.sh"' < /tmp/myscript.sh
```

`python3` **is** on the tablet and is the most reliable tool for register pokes.

---

## 3. Layout

| Path | What |
|---|---|
| `/home/bjesus/firehd8-pmos/libreecho-kernel` | kernel source (git repo, 1 commit, **all port work uncommitted**) |
| `/home/bjesus/firehd8-pmos/kbuild` | out-of-tree build dir (holds `.config`) |
| `/home/bjesus/firehd8-pmos/tc/usr/bin/aarch64-linux-gnu-` | cross toolchain |
| `/home/bjesus/firehd8-pmos/douglas-initfs` | **the real initramfs** (see §5) |
| `/home/bjesus/firehd8-pmos/mkboot_douglas.py` | packs `Image` into an LK boot image |
| `/home/bjesus/firehd8-pmos/douglas-alsa/` | out-of-tree audio + battery + jsa1212 modules |
| `/home/bjesus/firehd8-pmos/douglas-keys/` | out-of-tree key driver |
| `/home/bjesus/douglas-port-backup/` | boot images, module backups, vendor DTB |
| `stockholm-aladdin:/root/douglas-flash/` | boot images + modules staged for flashing |
| `/root/mods/` (tablet) | loaded `.ko` files, each with a rollback copy |

The device tree is a **single file**: `libreecho-kernel/arch/arm64/boot/dts/mediatek/mt8163-douglas.dts`.
It is compiled into the kernel via `arch/arm64/kernel/dtb_builtin.S`; `setup.c`
unconditionally points `__fdt_pointer` at it, because LK passes an unusable
3.18-era DTB. **Changing the DT therefore requires a full kernel rebuild and
reflash.**

---

## 4. Build and flash

### Zero-setup: GitHub Actions

`.github/workflows/build.yml` builds the same thing from the repo alone (base commit +
`kernel/0001-douglas-port.patch`, `kernel/douglas-6.1.config`, `douglas-initfs/`) and
uploads `douglas-boot.img`, `douglas-mods.tar.gz` (in-tree display modules + out-of-tree
modules, for `/root/mods/`), the `.config` and `SHA256SUMS`; a `v*` tag makes a Release.
About 10 minutes per run. The first CI image was flashed and verified on 2026-11-22.
Since the same morning a second job (`rootfs`, tags and manual runs) builds the whole OS:
`tools/rootfs-inner.sh` runs in an aarch64 Alpine container under QEMU, installs
`tools/rootfs-packages.txt`, overlays `rootfs/` (which now also holds the firmware, the
udev/polkit/sudoers/phoc snippets, dconf defaults and every helper script), creates
`user`/`douglas`, drops the kernel modules into `/root/mods` and writes a 3 GB ext4 image
(`douglas-rootfs.img.gz`) that `douglas-init` grows on first boot. Install procedure in
README.md. A tar backup of the hand-built rootfs that ran until then is at
`/home/bjesus/douglas-port-backup/rootfs-p16-backup-2026-11-22.tar.gz`.
Trap met the same morning: `rootfs/usr/local/sbin/start-phosh` carried an empty `else`
branch (shell syntax error) that killed the session start after module load. `sh -n`
every rootfs script before copying it to the tablet.

**Incident 2026-11-22 midday (rootfs image).** The first CI rootfs was flashed to p16 and
booted to Phosh, but `apk` installing into `--root` under docker/QEMU had dropped every
setuid/setgid bit: no `sudo`, no D-Bus launch helper (so polkit and elogind could never be
activated: no lock-screen unlock, no power menu, "Insufficient privileges" in nmcli), and
no PAM helper. Root is locked by design, so from the shell there was no way to reboot or
return to TWRP, and our DT had the PMIC long-press reset disabled: the only exit was to
drain the battery. Lessons, all applied: (1) `tools/rootfs-inner.sh` restores Alpine's
setuid set explicitly and the build fails unless `sudo`, `passwd`, `unix_chkpwd` and the
dbus helper carry their modes; (2) the DT now enables the MT6323 long-press reset
(`mediatek,long-press-mode = <1>`, 8 s, `douglas-boot60.img`) so a Power hold always
resets the board; (3) never flash a rootfs you cannot log into as root over the USB link,
install the test key from TWRP first (`/root/douglas-flash/flash-both.sh` on the relay
does boot + rootfs + key in one go); (4) the ext4 image is built without
`orphan_file`/`metadata_csum_seed` so TWRP's 3.18 kernel can mount it.

**Resolved 2026-11-25 evening.** Getting back to TWRP took three days: LK boots the kernel
by itself as soon as a drained battery passes its threshold, so the key combination has
to be held from the moment the charger is plugged in. `douglas-boot60.img` (PMIC
long-press reset) went on first, then the fixed CI rootfs (run 45014477532) with
recovery aids written from TWRP before the first boot: root ssh key (`id_tablet`), root
password `douglas` (**this device only**, the release image keeps root locked), the home
Wi-Fi profile, and the dead-man switch re-armed at 240 s, disarmed over Wi-Fi 40 s after
boot once root login, `sudo`, polkit, elogind, Wi-Fi IPv4 and Bluetooth had checked out.
`/root/douglas-boot-good.img` on the device is boot60. The rootfs build now also ships
its own kernel as that self-heal image. Flashing from this laptop: `adb` is
`/opt/android-sdk/platform-tools/adb`; the tablet's USB network needs
`sudo ip link set <enp0s20f0uN> up && sudo ip addr add 10.42.0.1/24 dev <enp0s20f0uN>`.
Password SSH without sshpass: `expect` (see `.work/` notes).

### Build

```sh
cd /home/bjesus/firehd8-pmos
K="make -C libreecho-kernel O=/home/bjesus/firehd8-pmos/kbuild ARCH=arm64 \
   CROSS_COMPILE=/home/bjesus/firehd8-pmos/tc/usr/bin/aarch64-linux-gnu-"
$K -j$(nproc) dtbs Image
python3 mkboot_douglas.py kbuild/arch/arm64/boot/Image \
        /home/bjesus/douglas-port-backup/douglas-bootNN.img
```

`dtbs` must succeed before `Image`, since `dtb_builtin.o` `.incbin`s the DTB.

### Out-of-tree modules (no reflash needed — prefer this!)

```sh
cd /home/bjesus/firehd8-pmos/douglas-alsa     # or douglas-keys
make -C /home/bjesus/firehd8-pmos/kbuild M=$PWD ARCH=arm64 \
     CROSS_COMPILE=/home/bjesus/firehd8-pmos/tc/usr/bin/aarch64-linux-gnu- modules
```

Copy the `.ko` to `/root/mods/` on the tablet and reboot. Audio, keys and the
battery gauge are all modules — iterate there rather than reflashing when you can.

### Flash

Always stage via the relay and **verify the readback hash**:

```sh
SZ=$(stat -c %s douglas-port-backup/douglas-bootNN.img)
cat douglas-port-backup/douglas-bootNN.img | \
  ssh root@stockholm-aladdin "cat > /root/douglas-flash/douglas-bootNN.img"
ssh root@stockholm-aladdin "/root/usbkick.sh >/dev/null 2>&1; \
  cat /root/douglas-flash/douglas-bootNN.img | ssh root@10.42.0.2 \
  \"cat > /tmp/b.img; dd if=/tmp/b.img of=/dev/mmcblk0p5 bs=1M conv=fsync; sync; \
    dd if=/dev/mmcblk0p5 bs=$SZ count=1 2>/dev/null | sha256sum\""
```

Then `nohup sh -c 'sleep 2; reboot -f' >/dev/null 2>&1 &`, wait ~70 s, `usbkick.sh`.

### Recovery if it does not boot

1. `douglas-boot31.img` is the **pre-session known-good** image;
   `douglas-boot47.img` is current. Both are staged on the relay.
2. Hold **Power + Volume Down** for TWRP, then `adb push` the image and
   `dd` it to `/dev/block/mmcblk0p5` (note: `/dev/block/...` in TWRP,
   `/dev/mmcblk0p5` in the running system).
3. The real initramfs has a safety net: if the rootfs fails to mount it writes
   `boot-recovery` into the BCB on `/dev/mmcblk0p12` and reboots into TWRP
   automatically. That only covers mount failures, not earlier hangs.

---

## 5. THE TRAP THAT WILL COST YOU A BOOT CYCLE

`CONFIG_INITRAMFS_SOURCE` in `kbuild/.config` must point at
**`/home/bjesus/firehd8-pmos/douglas-initfs`**.

There is a second, nearly identically named tree,
`/home/bjesus/douglas-port-backup/le-initfs`, which is the **boot-#1 diagnostic**
initramfs: it mounts proc/sys/dev, dumps logs, and then runs `sleep 1000000`. It
never touches `/dev/mmcblk0p16`. A kernel built against it boots, registers the
USB gadget, and then sits there forever with no rootfs and no network — looking
exactly like a hang. This already happened once.

The correct initramfs `/init` ends with:

```sh
exec /bin/busybox switch_root /mnt/root /sbin/douglas-init
```

**Before every flash, diff your config against the running kernel's.** You can
extract the config from any boot image — find the `IKCFG_ST` marker inside the
gzipped `Image` and inflate what follows. You can recover a boot image's built-in
DTB the same way by scanning the decompressed `Image` for the `d00dfeed` FDT
magic. That is how the known-good `bootargs` were confirmed.

More generally: **do not bundle several untested changes into one flash.** The
hard debugging session in this project was caused by one image carrying five
unverified changes at once.

---

## 6. Debugging techniques that work here

### `dmesg` is nearly useless — use pstore

The Wi-Fi driver emits `ECHO_WLAN_HIF` debug spam that **wraps the kernel log
buffer within minutes**, so boot messages vanish. `ramoops` is in the DT and
`CONFIG_PSTORE_{RAM,CONSOLE,PMSG}=y`, but nothing mounts it:

```sh
mkdir -p /mnt/pstore && mount -t pstore pstore /mnt/pstore
ls /mnt/pstore/     # console-ramoops-0, dmesg-ramoops-N (crash records)
```

This is how the `bq24190_pm_resume` use-after-free and the post-resume RCU stall
were found. It survives reboots.

Also: **write test logs to `/root` (ext4), never `/tmp`** (tmpfs — a reset wipes
it), and run anything that may drop the network under `setsid nohup`.

### Register access from userspace

`python3` + `mmap` on `/dev/mem` is the workhorse. Two traps:

* **Pinctrl registers are not at the pio node's `reg`.** `pinctrl@1000b000` is
  only the EINT block. The pin registers live in the
  `mediatek,pctl-regmap` syscon at **`0x10005000`** (`dir 0x000`, `pullen 0x100`,
  `pullsel 0x200`, `dout 0x400`, `din 0x500`, `pinmux 0x600`;
  `port = ((pin>>4)&0xf)<<4`, `bit = pin&0xf`; mode reg
  `((pin/5)<<4)+0x600`, field shift `3*(pin%5)`). Reading the wrong base returns
  plausible-looking garbage — **always validate against a pin whose state you
  know** (pins 57/58 must read mode 1 as `SDA1`/`SCL1`).
* **Use the driver's real offsets.** For `mtk_thermal`, `TEMP_MSR0..3` are at
  `0x090/0x094/0x098/0x0B8`, not the `0x030` block.

### The PMIC is not in `/dev/mem`

It is behind `pwrap`. Read it through regmap debugfs:

```sh
mount -t debugfs none /sys/kernel/debug
grep -E '^(0700|4000):' /sys/kernel/debug/regmap/1000d000.pwrap/registers
```

I2C devices can be driven from userspace with `python3` + `I2C_RDWR` ioctl (two
messages, so the register-address write and the read share a repeated start — a
write followed by a separate read returns garbage). There are no `i2c-tools`.

### The hardware watchdog is a usable safety net

`/dev/watchdog0` (`mtk-wdt`, `nowayout=0`, ~31 s). The bootloader leaves it
running and the kernel adopts it as `WDOG_HW_RUNNING`. Open it, `WDIOC_SETTIMEOUT`,
stop petting it, and the board resets — verified. Arm it around anything risky
(live PLL reprogram, DVFS transitions) so a CPU hang reboots instead of needing a
physical power-hold. Disarm with a magic `V` write before closing.

**Caveat since the suspend fix:** `mtk_wdt_suspend()` now stops the watchdog
across suspend (it has to — otherwise it reset the board mid-sleep), so it no
longer protects the suspend path itself. It is restarted on resume.

### Timing when probing a failing driver

If a userspace tool fails fast (`arecord` does), start your register sampler
**before** launching it — otherwise `close()` has already torn the registers down
and you will only see the disabled state and draw the wrong conclusion.

---

## 7. What works today

Kernel `#31` / `douglas-boot59.img` (boot47 + BT bridge fix + `m4u` disabled + thermal calibration + display domain always-on + watchdog bark IRQ/pretimeout panic + self-healing initramfs + uinput + `CONFIG_NETDEVICES`/`TUN`):

* **Display** `mtk-drm` 800x1280@60 DSI + **Mali-T720 Panfrost** GL ES 2.0,
  PWM backlight.
* **Touch** Goodix GT9271; **keys** Power + Volume Down (interrupt-driven via
  in-tree `mtk-pmic-keys`) and Volume Up (polled, `douglas-keys.ko`).
* **Wi-Fi + Bluetooth** MT6625L (vendor consys stack), DHCPv4 + DHCPv6 via NetworkManager,
  BlueZ. BT works since boot57 (the HCI bridge registered too early and bluetoothd never saw
  the adapter); IPv4 works since boot59 (see §8 "Wi-Fi IPv4"). `apk add` works again.
* **Audio playback + capture** MT8163 AFE DL1/VUL + MT6323 DAC/ADC, speaker/headphone
  switching, jack detect, hardware volume, built-in mic (left channel, ACC mode).
* **Battery + charger** bq24297 at 1.5 A + MT6323 AUXADC gauge.
* **Sensors** BMA253 accelerometer, JSA1212 ALS, SoC AUXADC (IIO).
* **CPU frequency scaling** `cpufreq-dt`, `schedutil` by default, six OPPs:
  600 / 624 / 741 / 871 / 1001 / 1050 MHz, all at 1.150 V. Verified linear
  (`t x f` constant within 3.3% across a 1.67x span).
* **Thermal** SoC die sensor (~32 C idle, ~48 C plateau under 4-core load) with
  working CPU throttling: trips 75/85 C passive + 115 C critical, step-wise
  governor driving the cpufreq cooling device through the OPP ladder.
* **Hardware RTC** `/dev/rtc0` with working alarm, keeps time across power-off,
  and the kernel sets the system clock from it at boot.
* **s2idle suspend/resume works** (`loginctl suspend`, RTC or power-key wake): display
  comes back on the power key, Wi-Fi is power-cycled around sleep by an elogind hook,
  the USB debug link re-pulls itself. See §8E for the three mechanisms and caveats.

---

## 8. What is left

Detailed register-level notes for each item are in `TODO.md`. Ordered by what I
would do next.

### A. Microphone — FIXED 2026-11-21 (evening)

**Capture works**: `arecord -D hw:0,0 -f S16_LE -r 48000 -c 2` gives real audio on the
left channel (RMS scales with the `Mic Gain` control: 271 / 1864 / 4881 for gain 0/3/5;
mono capture and capture-during-playback also work). The right channel is a constant
offset (~-111, RMS 2): the second ADC input is not wired on this board, the built-in mic
is left-only. ACC mode, mic mux 0, gain 3 (12 dB) are the working defaults.

**What fixed it**: `douglas-alsa.c` now binds to the device-tree node
`afe: mt_soc_dl1_pcm@11220000` (compatible `mediatek,mt8163-soc-pcm-dl1`) instead of
creating a bare platform device. That gives the driver a real `pm_domain` (the AUDIO
scpsys domain is powered through the scpsys driver's full sequence and shows `on` in
`pm_genpd_summary`), pinctrl states (`audpmicclk-speaker-mode0/1` replace the raw pinmux
pokes) and named clocks from the node. The old single-write power-on of
`SPM_AUDIO_PWR_CON = 0xd` is kept only as a fallback when no pm_domain is attached.
Which of these is the decisive one is not proven: a manual scpsys-style off/on sequence
from userspace had *not* fixed it earlier that day, so the ordering/state at genpd
attach (domain brought up before any AFE register is touched) is the best explanation.

Gotchas met on the way:
* The DAI link's platform component is matched **by device name**; with DT binding the
  device is `11220000.mt_soc_dl1_pcm`, so `douglas_playback_platforms[0].name` is set to
  `dev_name(dev)` at probe (without this the card defers forever, no error printed).
* An in-tree `mt8163-afe` driver also probes this node at boot and fails
  ("cannot find TOPCKGEN regmap"); it leaves the node free. Should be removed or fixed.
* The empty `aud_pins_default` node makes the driver core log "there is not valid maps
  for state default" four times; harmless. Give it content or drop `pinctrl-0`.
* `0x9330` vs `0x8330` in `AFE_PMIC_NEWIF_CFG0` is **not** a bug: 0x9330 is the PMIC
  reset value, the vendor's 48 kHz code is 8 → 0x8330, same as ours.
* `/root/mods/douglas-alsa.ko` is the working build; rollbacks
  `douglas-alsa.ko.pre-dtbind-backup` and `douglas-alsa.ko.pre-pinfix-backup` sit next
  to it. Source and binary also saved as `douglas-port-backup/douglas-alsa.{c,ko}.mic-working`.

The long investigation that preceded this (vendor kernel run via TWRP, full register
diffs, SoC-level experiments) is preserved in `douglas-audio-test/mic-diag-2026-11-21/`
and in the git history of this file; the TWRP method described there remains the way to
get a vendor reference for any future audio problem. Key facts kept from it:
* `douglas-boot17/18` are amonet stub headers, there is no bootable Android image on the
  device; TWRP (`recovery_x`, p6) runs Amazon's 3.18 kernel with the full vendor audio
  driver and rooted adb, entered with `printf 'boot-recovery\0\0\0' |
  dd of=/dev/mmcblk0p12 bs=16 count=1 conv=fsync; reboot -f`, left with `adb reboot`.
* **Never poke live**: `INFRACFG_PERI_BUS_DCM_CTRL` (0x10001074) bits 22/29,
  `INFRA_TOPCKGEN_DCMCTL`, or any bit of `AP_PLL_CON5` (0x1000c014, bits 2/3/4/7 each
  hang the board). A boot-time write of `AP_PLL_CON5=0` does not survive either.
* `libreecho-kernel/drivers/soc/mediatek/douglas-quirks.c` (variant 0 = no-op) holds a
  reusable one-shot guard (`WDT_NONRST_REG2` bit 31) for risky boot-time register writes.

### B. CPU OPPs above 1050 MHz — blocked on one hardware question

The vendor table (Amazon's own `mt_cpufreq.c`) is
1300 MHz @ 1.300 V, 1216 @ 1.250, 1133 @ 1.200, and everything at or below
1050 MHz @ 1.150 V. The vendor **never** goes below 1.150 V, so there is no
undervolting to be had — all six implemented OPPs already run at the voltage the
board boots at.

Going above 1050 MHz means actually raising VPROC, and the unresolved question is
whether the MT6323 PMIC's own always-on `buck_vproc` (also sitting at 1.15 V)
feeds the same rail as the external SY8827N. If it does, it pins the floor at
1.15 V — harmless, and equal to the vendor's own floor — but the interaction when
raising the external buck to 1.3 V is unverified. Resolve that first. Thermal
throttling and a critical trip are now in place behind it.

### C. The 10 s boot stall — FIXED 2026-11-21 (evening), proper IOMMU still open

`mediatek-drm` now binds at **t=4.48 s** (was 14.56 s). The ovl/rdma `iommus` references
pointed at an `m4u` node for which this kernel has no driver, so they sat in deferred
probe until the 10 s timeout. `douglas-boot50.img` marks `iommu: m4u@10205000` as
`status = "disabled"`; the IOMMU core then treats the reference as "no IOMMU" and the
display probes immediately, which is the same end state the timeout produced before.
Panfrost is unaffected (probes at 0.72 s, `renderD128` present), Phosh starts normally,
audio unaffected. The `start-phosh` DRM wait loop is now redundant but harmless.

Still open: enabling a real IOMMU (`CONFIG_MTK_IOMMU`, which breaks panfrost through
initcall reordering; see `douglas-boot44.img` notes in the old text below). To try it
again, revert the `status = "disabled"` line.

### D. Thermal calibration — FIXED 2026-11-21 (evening)

The calibration words were never in MMIO efuse; they are LK's `chosen/atag,devinfo` tag,
and a copy of that FDT node is in `douglas-port-backup/dt-sysfs/devicetree/base/chosen/`.
Tag layout: word0 = size, word1 = tag id, then `data[]`; the vendor's `mtk_ts_cpu.c`
uses `data[7]`, `data[8]`, `data[9]` = **`0x1205006d 0x8c60c0fb 0x00000000`** on this
unit. Decoded (same V1 layout as `mtk_thermal.c`): valid=1, DEGC_cali=54, ADC_GE=561,
O_SLOPE=4 but ID=0 so slope is ignored, VTS1=258, VTS2=80, VTS3=251, **VTS4/VTSABB
unfused (0)**. The vendor only uses TS1/TS2 (CPU bank), TS2 (GPU) and TS3 (SoC).

Changes (`douglas-boot52.img`):
* `drivers/thermal/mtk_thermal.c`: when there is no `calibration-data` nvmem cell, read
  `mediatek,calibration-data = <buf0 buf1 buf2>` from the thermal node; MT8163 banks now
  mirror the vendor (TS1+TS2, TS2, TS3) instead of including the unfused TS4/TSABB.
* `mt8163-douglas.dts`: the three words on the `thermal@1100b000` node. **They are
  per-device**; another unit needs its own `atag,devinfo` words (read them from a vendor
  boot via `/proc/device-tree/chosen/atag,devinfo`, or from the LK FDT).

Result: idle reading moved from ~27.7 C to ~35 C with Phosh running at 741 MHz; the
vendor kernel (TWRP, near-zero load, one core off) reports 32.6 C for its CPU zone, so
the remaining gap is load, not calibration. Trips (75/85/115 C) unchanged and now
meaningful. Proper fix for upstream would be an nvmem provider fed from the LK tag.

### E. Suspend — WORKING since 2026-11-21 night (three independent fixes)

Each resume problem had its own cause; all three are handled:

1. **Hard hang 40–80 s after resume** (silent, no RCU message, watchdog reset): the
   MT6625L consys/Wi-Fi stack does not survive s2idle. It is not fixed in the driver;
   `/etc/elogind/system-sleep/10-douglas-sleep` powers the chip off (`echo 0 >
   /dev/wmtWifi`) before sleep and on again after. Wi-Fi comes back and NetworkManager
   re-manages `wlan0`. Suspending with Wi-Fi powered (e.g. `echo mem > /sys/power/state`
   bypassing elogind) still hangs; use `loginctl suspend`.
2. **Black screen after resume**: the port runs the DSI controller on configuration
   inherited from LK (see mtk_dsi.c: no PHY/PLL programming, panel commands stubbed).
   DPMS off/on survives because the DSI device holds the display domain at runtime, but
   system suspend powered the `mm` domain off and wiped that state. Fix:
   `GENPD_FLAG_ALWAYS_ON` on `MT8163_POWER_DOMAIN_DISP` in `mtk-scpsys-mt8163.c`
   (clocks still gate, backlight still turns off). After an RTC wake the screen stays off
   (Phosh behaviour); the power key turns it on with live vblanks.
3. **USB gadget dead after resume** (musb-mtk recovers the PHY but never re-pulls D+):
   the sleep hook toggles `/sys/class/udc/musb-hdrc.2.auto/soft_connect` after resume,
   and `/usr/local/sbin/usb-link-guard` (started by `start-phosh`) does the same whenever a
   host is attached (`udc state == configured`) but the relay stops answering. It never
   reboots (an earlier version did, and rebooted the tablet when the relay side wedged). A
   kernel-side fix would call `mtk_musb_restore_gadget_pullup()` from a system-resume hook.

Verified: `loginctl suspend` + RTC wake + simulated power key (uinput, `rootfs/root/powerkey.py`)
→ display active with ~60 vblank/s, Wi-Fi re-powered with a fresh IPv4 lease (boot59), BT
powered and `bluetoothctl scan on` finds devices, link back, audio OK, no oops.
The former `vblank wait timed out` WARNINGs on every blank are gone: `mtk_drm_crtc.c` now
holds a DSI power reference from hw_init to hw_fini so the bridge's post_disable cannot
gate the DSI clocks before the CRTC's final vblank wait (`mediatek-drm.ko` in
`/root/mods/lib/modules/.../`, backup `*.pre-dsiref-backup`).

#### Safety nets added the same night (keep them)
* **Watchdog bark IRQ**: `toprgu` has `interrupts = <GIC_SPI 128 IRQ_TYPE_LEVEL_LOW>`
  (vendor DT says edge-falling; GICv2 rejects that and the driver then fails to probe,
  which leaves the bootloader's watchdog unpetted → reset every ~65 s — this happened,
  `douglas-boot53.img`). With `WATCHDOG_PRETIMEOUT_GOV_PANIC` a hang now panics at 15 s
  and the stack lands in pstore (`/dev/watchdog0`: timeout 31, pretimeout 15, governor panic).
* **Self-healing initramfs**: `douglas-initfs/init` counts boots in `/root/.boot-attempts`;
  `start-phosh` touches `/root/.boot-ok` 90 s after start. Three consecutive boots without
  the marker reflash `/root/douglas-boot-good.img` (= boot59 since 2026-11-22) onto `mmcblk0p5` and reboot.
  Update the good image when a new one has proven itself. This made unattended kernel
  iteration safe; the reset-loop above needed a human and TWRP before it existed.
* `CONFIG_INPUT_UINPUT=y` so the power key can be simulated remotely.

### Wi-Fi association — FIXED 2026-11-22 (night)

Wi-Fi had silently stopped associating: a saved auto-connect network was in range at
signal 94 but `wlan0` stayed "disconnected" and `nmcli con up` failed with "The device
could not be readied for configuration". NM debug log: `set-hw-addr: failed to
set-cloned MAC address to 00:08:22:38:88:FB`. The MT6625L driver has **no set-MAC
support** (`ip link set wlan0 address ...` → `ioctl 0x8924: Not supported`), so the MAC
"pinning" in `start-phosh` never worked and the real address is `00:08:22:a4:b1:fe`;
any cloned-MAC setting makes NM fail. Fix: `/etc/NetworkManager/conf.d/20-mac.conf`
sets `wifi.cloned-mac-address=preserve`, the per-profile
`802-11-wireless.cloned-mac-address` overrides were cleared (`nmcli con modify <name>
802-11-wireless.cloned-mac-address ""`), and the pinning line was removed from
`start-phosh`. Association + DHCPv6 work immediately. IPv4 needed a second fix, below.

### Wi-Fi IPv4 — FIXED 2026-11-22 (night), `douglas-boot59.img`

Symptom: after every (re)connect NM showed `wlan0: connected` but no IPv4 address, no
default route, `apk add` and `wget` failed with "Permission denied" / "Network
unreachable" (the Alpine CDN has no AAAA record), while a hand-run
`udhcpc -i wlan0 -n -q -s /bin/true` got a lease instantly. NM debug log
(`nmcli general logging level DEBUG domains DHCP4,IP4`, read from busybox `syslogd`):

```
dhcp4 (wlan0): event: received ACK of 192.168.8.148 from 192.168.8.1
dhcp4 (wlan0): accept lease
dhcp4 (wlan0): accept failed: failed to accept lease: Protocol not available
```

Cause: the kernel config had **`CONFIG_NETDEVICES` disabled** (the Wi-Fi driver lives
under `drivers/misc`, the USB gadget under `drivers/usb`, so nothing had pulled it in).
Without it `SO_BINDTOIFINDEX` *and* `SO_BINDTODEVICE` return `ENOPROTOOPT`
(`net/core/sock.c: sock_bindtoindex_locked`), and n-dhcp4's `socket_bind_if()` fails when
it opens the per-lease UDP socket on accept. DISCOVER/OFFER/ACK still worked because they
use a raw packet socket. Fix: `CONFIG_NETDEVICES=y`, `CONFIG_NET_CORE=y`, `CONFIG_TUN=y`
(for VPNs; nothing else enabled). Verified on boot59: IPv4 applied right after
association, `ping 1.1.1.1` and `apk add` work, and IPv4 comes back after a
suspend/resume cycle. The earlier "resolved" note that blamed test interference was wrong;
the `ipv4.dad-timeout=0` in `20-mac.conf` and the per-profile `dhcp-timeout 45` were added
while chasing this and are harmless but not the fix.

### Bluetooth — FIXED 2026-11-22 (night), `douglas-boot57.img`

Before: `hci0` existed (virtual, address 00:00:00:00:00:00, DOWN), `bluetoothctl` said
"No default controller available", `btmgmt` hung. Cause: `stp_chrdev_bt.c` called
`hci_register_dev()` at init, before the WMT launcher had loaded firmware; the core's
setup power-on failed and the porter's workaround (cancel the power-on work, clear
`HCI_AUTO_OFF`) left `HCI_SETUP` set forever, so the management interface never
announced the adapter. Fix: a delayed work polls `mtk_wcn_stp_is_ready()` and registers
the device only then ("STP ready after 8 polls" at t≈6.7 s); the normal power-on runs,
`bluetoothd` sees `Controller 00:00:46:81:63:01`, `btmgmt power on` succeeds, name
"MTK MT8163 #1". `init-tier1.sh` now waits for `/sys/class/bluetooth/hci0` before the
`btmgmt` calls (with timeouts). Suspend: the sleep hook `rfkill block`s BT before
powering the consys chip off and unblocks it after resume; bluetoothd re-powers it.

### F. Smaller / parked

* **MicroSD slot** (`TODO.md` §1) — fully specified, never attempted; parked
  because there is no card to test with.
* **TMP103 PCB sensors** — now `status = "disabled"` in the DT with the finding
  recorded: **nothing ACKs at 0x70/0x71/0x72** on i2c-2 while 0x18 and 0x6b do,
  so they are unpowered or not fitted. The old
  `tmp103 2-0070: error writing config register` is simply that NAK. Find the
  rail that powers them (look at the MT6323 LDO list) before re-enabling.
  `CONFIG_SENSORS_TMP103` is already `=y` and the driver matches correctly.
* **Upstream cleanup** (`TODO.md` §7) — Power/Volume-Down are already folded into
  in-tree `mtk-pmic-keys`. Still out-of-tree: Volume Up, the ALSA machine driver,
  and the battery gauge. Note an in-tree `sound/soc/mediatek/mt8163/mt8163-douglas.c`
  **already exists, written but never compiled** (`CONFIG_SND_SOC_MT8163_DOUGLAS`
  is off) — treat it as unverified.
* **No serial console.** The vendor bootargs reference `ttyS0,921600n1`, so test
  points probably exist. Without one you are blind on early-boot and
  suspend-path hangs and depend on pstore.

---

## 9. Reference material

* **Amazon's own GPL kernel for this device** — the single most valuable
  reference. `hexdump0815/linux-amazon-mediatek-mt8163-kernel-source`, branch
  **`douglas-import`**. Fetch files raw:
  `https://raw.githubusercontent.com/hexdump0815/linux-amazon-mediatek-mt8163-kernel-source/douglas-import/<path>`.
  Useful paths:
  * `drivers/misc/mediatek/base/power/mt8163/mt_cpufreq.c` — the authoritative
    CPU freq/volt table and DVFS sequence.
  * `drivers/misc/mediatek/thermal/mt8163/` — thermal, incl. `mtk_ts_cpu.c`
    (trip points, calibration bit layout).
  * `sound/soc/mediatek/mt_soc_audio_8163_amzn/` — the audio tree used for the
    capture work (`mt_soc_pcm_capture.c`, `mt_soc_codec_63xx.c`,
    `mt_soc_afe_control.c`, `mt_soc_afe_connection.c`, `AudDrv_*.{c,h}`).
  Enumerate it with the GitHub trees API rather than guessing paths.
* **Vendor DTB**: `/home/bjesus/douglas-port-backup/vendor-dtb-tail.bin`.
  `dtc -I dtb -O dts` it. This is where hardware wiring questions get answered
  (it gave the PMIC's EINT 24 line, the SY8827N's I2C bus and VSEL strap, the
  i2c1 pin numbers, the AFE SRAM size). Phandles are numeric — resolve them by
  searching for `phandle = <0x..>`.
* **The original Android install is still intact and mountable** —
  `mmcblk0p14` is a complete LineageOS root filesystem (1.6 GB) and `p17` holds
  an `ANDROID!` boot image. `mount -o ro /dev/mmcblk0p14 /mnt/p14`. Useful for
  "what did the vendor actually do" questions; the MediaTek audio HAL config was
  read from there. Note the per-device mic mode is chosen at runtime by the HAL
  from a parameter store, so it is *not* in the static XML.
* **Datasheets**: the CPU buck is a Silergy **SY8827N** (vendor calls it
  "sym827"): 0.6–1.3875 V in 12.5 mV steps, `VSEL0 0x00`, `VSEL1 0x01`,
  `CONTROL 0x02`, `ID1 0x03`, `ID2 0x04`, `PGOOD 0x05`; VSEL pin low selects
  VSEL0. On this board it reads `ID1 = 0x81`, `ID2 = 0x0c`.

---

## 10. Artifact inventory

### Boot images (`/home/bjesus/douglas-port-backup/`, also staged on the relay)

| Image | Contents |
|---|---|
| `douglas-boot31.img` | pre-session known-good — **rollback target** |
| `douglas-boot32.img` | **do not flash** — the diagnostic-initramfs failure |
| `douglas-boot34.img` | + `i2c1` bus, `aliases`, `mediatek,mt8163` root compatible |
| `douglas-boot35.img` | + `silergy,sym827` regulator (`ext_vproc`) |
| `douglas-boot37.img` | + vendor OPP ladder, `fcs,suspend-voltage-selector` |
| `douglas-boot38.img` | + PMIC EINT 24, `mtk-pmic-keys`, RTC fixed |
| `douglas-boot39.img` | + `mtk_wdt` suspend fix, `omit-battery-class` |
| `douglas-boot43.img` | + thermal (sensor, zone, throttling) |
| `douglas-boot44.img` | + `MTK_IOMMU` — **stall fixed but panfrost broken** (§8C) |
| `douglas-boot47.img` | + 1050 MHz OPP, TMP103 disabled. Known-good fallback |
| `douglas-boot48.img` | boot47 + vendor DCM defaults quirk (harmless, no effect) |
| `douglas-boot49.img` | **do not flash** — `AP_PLL_CON5=0` at boot, first boot dies (guard skips it on 2nd) |
| `douglas-boot50.img` | boot47 + `m4u` node disabled → 10 s boot stall gone |
| `douglas-boot51.img` | boot50 + DTS calibration words only (driver unpatched; no effect) |
| `douglas-boot52.img` | boot50 + thermal calibration (driver + DTS). Known-good, also stored on the tablet as `/root/douglas-boot-good.img` |
| `douglas-boot53.img` | **do not flash** — watchdog IRQ with edge type, driver fails to probe, reset loop |
| `douglas-boot54.img` | boot52 + display domain always-on + watchdog bark IRQ (level) |
| `douglas-boot55.img` | boot54 + self-healing initramfs |
| `douglas-boot56.img` | boot55 + uinput + watchdog sysfs |
| `douglas-boot57.img` | boot56 + BlueZ HCI bridge registers after STP ready → Bluetooth works |
| `douglas-boot58.img` | unused duplicate of boot57 |
| `douglas-boot59.img` | boot57 + `CONFIG_NETDEVICES`/`NET_CORE`/`TUN` → NM gets IPv4. **CURRENT**, also `/root/douglas-boot-good.img` |
| `douglas-boot60.img` | boot59 + PMIC long-press hardware reset in the DT. **CURRENT** since 2026-11-25, also `/root/douglas-boot-good.img` |
| CI `douglas-boot.img` (GitHub Actions, run 44998827495) | same sources as boot59 built with Ubuntu GCC 13 → boots, display/Wi-Fi/BT/audio verified 2026-11-22 morning. Running now; boot59 stays the self-heal image |

### Modules on the tablet (`/root/mods/`), each with a rollback copy

| Module | Rollback |
|---|---|
| `douglas-alsa.ko` (DT-bound; playback + **working** capture + mic controls) | `douglas-alsa.ko.pre-dtbind-backup`, `.pre-pinfix-backup`, `.playback-only-backup` |
| `douglas-keys.ko` (Volume Up only) | `douglas-keys.ko.pmic-polling-backup` |
| `douglas-battery.ko` (no power-supply hijack) | `douglas-battery.ko.hijack-backup` |

### Userspace scripts on the tablet

* `/sbin/douglas-init` — real init: mounts, sets `usb0` to `10.42.0.2`, starts
  dropbear, kicks off Wi-Fi, then `start-phosh`. It deliberately flaps `usb0` at
  8/12/15/20/30/45 s because the ECM carrier notification is often missed.
* `/usr/local/sbin/start-phosh` — modules, udev, **the DRM wait loop**, seatd,
  charger limits, `schedutil` governor, NetworkManager, then the Phosh session.
* `/usr/local/sbin/init-tier1.sh` — sensors (resolves the i2c bus by matching
  `of_node` against `i2c@11009000`, **not** a hardcoded number), iio-sensor-proxy,
  BlueZ.

---

## 11. Things that bit us, in one list

1. `CONFIG_INITRAMFS_SOURCE` pointing at the diagnostic initramfs (§5).
2. Bundling five untested changes into one flash — made a simple problem
   take hours to diagnose.
3. The USB link wedging after every reboot (§2).
4. `dmesg` wrapping before you can read it; `/tmp` being tmpfs (§6).
5. Pinctrl registers not being at the pio node's `reg` (§6).
6. Reading a driver's registers at the wrong offsets and believing the result.
7. Sampling registers *after* a fast-failing tool has already cleaned up (§6).
8. **I2C bus renumbering**: adding `i2c@11008000` shifted the sensor bus, so an
   `aliases` node now pins `i2c0/1/2` to `0x11007000/8000/9000` and the sensors
   moved from `i2c-1` to **`i2c-2`**. Anything hardcoding a bus number breaks.
9. `power_supply_unregister()` on another driver's supply (what
   `douglas-battery.c` used to do) — a use-after-free that oopsed on every
   resume. Fixed with `omit-battery-class` on the charger node.
10. Trusting the register values previously written in `TODO.md`. Several were
    wrong (VUL offsets, the UL interconnect register, the CPU OPP voltages, the
    i2c1 pin numbers, the regulator voltage range). Verify against the vendor
    source or the hardware before building on them.
