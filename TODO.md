# Amazon Fire HD 8 (2017, `douglas`) — Mainline Linux 6.1 Port `TODO.md`

## Current Port Status (Already Working)
- **Kernel & Core SoC**: Mainline Linux 6.1.178 (`arm64`, 4x Cortex-A53 SMP, 1.41 GB RAM, internal eMMC `mmc0`).
- **Display & 3D GPU**: `mtk-drm` (`OVL0` $\rightarrow$ `RDMA0` $\rightarrow$ `COLOR0` $\rightarrow$ `CCORR0` $\rightarrow$ `AAL0` $\rightarrow$ `GAMMA0` $\rightarrow$ `DITHER0` $\rightarrow$ `DSI0` at `800x1280 @ 60.000 Hz`) + **Mali-T720 (`Panfrost`)** OpenGL ES 2.0 acceleration, plus PWM backlight control (`0–15`).
- **Touch & Physical Buttons**: Goodix GT9271 5-point touchscreen + Power, Volume Up, and Volume Down buttons ([douglas-keys.c](file:///home/bjesus/firehd8-pmos/douglas-keys/douglas-keys.c)).
- **Wi-Fi & Bluetooth 4.2**: MediaTek MT6625L (`CONSYS` + `wmt_loader` + `wlan_mt6625l` + `stp_chrdev_bt`) with pinned MAC (`00:08:22:38:88:fb`), DHCP, NTP clock sync, and BlueZ (`hci0`, BR/EDR + BLE).
- **Audio Playback & Headphone Jack Switching**: MT8163 AFE `DL1` + MT6323 PMIC DAC + `GPIO28` speaker amp + `GPIO44` headphone amp + `GPIO26` jack detect + `Master` hardware volume ([douglas-alsa.c](file:///home/bjesus/firehd8-pmos/douglas-alsa/douglas-alsa.c)).
- **Battery & Charger**: TI `bq24297` (`bq24190-charger`) at `1.5 A` + MT6323 PMIC AUXADC `vbat_channel_number = 6` gauge with EFUSE trim, 8x oversampling, and EMA filter ([douglas-battery.c](file:///home/bjesus/firehd8-pmos/douglas-alsa/douglas-battery.c)).
- **Sensors**: Bosch `BMA253` accelerometer (`1-0018`, with Y-inverted mount matrix) + Solteam `JSA1212/1214` ambient light sensor (`1-0044`, `jsa1212.ko`) integrated with `iio-sensor-proxy`.

---

## Remaining Tasks & Implementation Guide

---

### 1. MicroSD Card Slot (`mmc@11240000` / `msdc1`)
**Goal**: Enable the physical MicroSD card reader on the side of the tablet so cards appear as `/dev/mmcblk1`.

#### Hardware Details (from [vendor-dtb-tail.bin](file:///home/bjesus/douglas-port-backup/vendor-dtb-tail.bin))
- **Controller**: `MSDC1` at physical address `0x11240000` (size `0x1000`)
- **Interrupt**: `GIC_SPI 80 IRQ_TYPE_LEVEL_LOW` (`<0 0x50 8>`)
- **Clocks**:
  - `"source"` = `<&topckgen CLK_TOP_MSDC30_1_SEL>` (`0x4b` = 75)
  - `"hclk"` = `<&infracfg CLK_INFRA_MSDC_1>` (`0x1d` = 29)
- **Power Rails (from MT6323 PMIC)**:
  - `vmmc-supply` (Card power, 3.3V): `<&mt6323_vmch_reg>`
  - `vqmmc-supply` (I/O signaling voltage, 3.3V / 1.8V): `<&mt6323_vmc_reg>`
- **Card Detect GPIO**: `GPIO49` (`cd-gpios = <&pio 49 GPIO_ACTIVE_LOW>`, `EINT17`)
- **Pins (`MSDC1`)**:
  - `GPIO73`: `MSDC1_CLK` (`MT8163_PIN_73_MSDC1_CLK__FUNC_MSDC1_CLK`)
  - `GPIO74`: `MSDC1_CMD` (`MT8163_PIN_74_MSDC1_CMD__FUNC_MSDC1_CMD`)
  - `GPIO69..72`: `MSDC1_DAT0..3` (`MT8163_PIN_69_MSDC1_DAT0__FUNC_MSDC1_DAT0` .. `DAT3`)

#### Implementation Process
1. Add the `mmc1_pins_default`, `mmc1_pins_uhs`, and `mmc1_pins_eint` pinctrl groups inside `&pio` in [mt8163-douglas.dts](file:///home/bjesus/firehd8-pmos/libreecho-kernel/arch/arm64/boot/dts/mediatek/mt8163-douglas.dts).
2. Add the `mmc1: mmc@11240000` node under `/soc` in [mt8163-douglas.dts](file:///home/bjesus/firehd8-pmos/libreecho-kernel/arch/arm64/boot/dts/mediatek/mt8163-douglas.dts):
   ```dts
   mmc1: mmc@11240000 {
       compatible = "mediatek,mt8163-mmc";
       reg = <0 0x11240000 0 0x1000>;
       interrupts = <GIC_SPI 80 IRQ_TYPE_LEVEL_LOW>;
       clocks = <&topckgen CLK_TOP_MSDC30_1_SEL>,
                <&infracfg CLK_INFRA_MSDC_1>;
       clock-names = "source", "hclk";
       pinctrl-names = "default", "state_uhs";
       pinctrl-0 = <&mmc1_pins_default>;
       pinctrl-1 = <&mmc1_pins_uhs>;
       bus-width = <4>;
       max-frequency = <200000000>;
       cap-sd-highspeed;
       sd-uhs-sdr50;
       sd-uhs-sdr104;
       cd-gpios = <&pio 49 GPIO_ACTIVE_LOW>;
       vmmc-supply = <&mt6323_vmch_reg>;
       vqmmc-supply = <&mt6323_vmc_reg>;
       status = "okay";
   };
   ```
3. Note: Do **not** specify the vendor DTB's `"sd_extra"` clock name unless `drivers/mmc/host/mtk-sd.c` expects it; mainline `mtk-sd.c` uses `"source"` and `"hclk"`.
4. Rebuild `mt8163-douglas.dtb` + `Image`, flash to `/dev/mmcblk0p5`, and insert a MicroSD card to verify `/dev/mmcblk1` in `dmesg`.

---

### 2. Hardware Real-Time Clock (`/dev/rtc0` via `mt6323-rtc`)
**Status**: Working as of 2026-11-21 — **awaiting confirmation before marking done.**

Fixed without patching `rtc-mt6397.c` at all. The real problem was never the RTC
driver: the `pmic: mt6323` node had no `interrupts`, so `mt6397-core.c` (patched
earlier to make the IRQ optional) left `pmic->irq_domain = NULL`. With no domain,
the `mt6323_rtc_resources` entry `DEFINE_RES_IRQ(MT6323_IRQ_STATUS_RTC)` was taken
as a raw Linux IRQ number — landing on IRQ 20, which belongs to `mt-eint`, hence
`Failed to request resources for mt6397-rtc (irq 20) on irqchip mt-eint`.

The fix is to declare the PMIC's real interrupt line (see task 6) so
`mt6397_irq_init()` builds a proper domain. Result:
```
mt6397-rtc mt6323-rtc: registered as rtc0
mt6397-rtc mt6323-rtc: setting system clock to 2026-11-21T10:35:59 UTC
188:  0  1  0  0   mt6397-irq  20 Edge   mt6397-rtc
```
`/dev/rtc0` + `/dev/rtc` exist, `hwclock -r` works, the battery-backed clock had
the correct time across a power cycle, and the kernel now sets the system clock
from it at boot. **The alarm works too** — verified independently by arming
`/sys/class/rtc/rtc0/wakealarm` at +10 s and watching the RTC IRQ count go 1 -> 2
after 9 s. It is a registered wakeup source and is what the suspend testing in
task 6 uses to wake the board.

### 3. CPU Frequency Scaling (`cpufreq-dt`) & External CPU Buck Regulator (`sym827@60`)
**Status**: Implemented and measured on 2026-11-21 — **awaiting confirmation before marking done.**
Frequency scaling works across the full vendor ladder up to 1001 MHz. There is
**nothing to gain from voltage scaling below 1050 MHz** — see the vendor table below.

#### What is working (kernel `#8`, `douglas-boot36.img`)
- `i2c1: i2c@11008000` probes; pins `GPIO57`/`GPIO58` claimed as `SDA1`/`SCL1`.
- `silergy,sym827` regulator at `1-0060` probes: `FAN53555 Option[1] Rev[12] Detected!`,
  exposed as `ext_vproc`, reporting `1150000 uV` — exactly matching a raw register read.
- `cpufreq-dt` registers one shared policy for all 4 cores:
  `scaling_available_frequencies = 598000 1001000`, `related_cpus = 0 1 2 3`.
- Transitions verified: 4/4 manual switches via the `userspace` governor, plus 17
  automatic transitions under `schedutil` (598 MHz idle -> 1001 MHz under load -> back).
- Measured with a fixed integer workload: `598 MHz` = 5.61 s, `1001 MHz` = 3.34 s,
  ratio **1.678** vs the theoretical 1001/598 = **1.674**. The cores really do change speed.
- Default governor is `performance`, so the board boots at the known-good top OPP and
  performs no transition unless asked. `userspace` and `schedutil` are also built in.

#### Corrections to the earlier notes in this file (all verified in-system)
| Item | Earlier note | Verified value |
|---|---|---|
| I2C1 SDA/SCL pins | `GPIO77`/`GPIO78` | **`GPIO57`/`GPIO58`** (`pinmux = <0x3901 0x3a01>`); 77/78 are I2C**2** |
| Regulator range | `950000`–`1350000` uV | chip is **`600000`–`1387500` uV**, 12.5 mV steps (64 codes) |
| Chip identity | "compatible with `fcs,fan53555`/`silergy,syr827`" | **not** compatible: `DIE_ID = 1`, but mainline `fan53555.c` only knew `SYR82X = 8` / `SYR83X = 9`. Binding it as `syr827` fails probe ("Chip ID 1 not supported!"). A `SILERGY_SYM827` variant was added (`vsel_min = 600000`, correct slew ladder). Using `syr827`'s `vsel_min = 712500` would misreport every voltage by **+112.5 mV**. |
| CPU clock at boot | "1.30 GHz" | **`armpll = 1001 MHz`** (`ARMPLL_CON1 = 0x0009a000`, `TOP_CKMUXSEL = 1`). 1001 MHz is MediaTek's standard `PLL_DIV1_FREQ`. |
| Driver to use | `CONFIG_ARM_MEDIATEK_CPUFREQ` | **`CONFIG_CPUFREQ_DT`**. `mediatek-cpufreq.c` has no `mt8163` match *and* needs `"cpu"`/`"intermediate"` clocks, but `clk-mt8163.c` registers no CPU mux clock. `"mediatek,mt8163"` was already in the `cpufreq-dt-platdev.c` **allowlist**; it only needed adding to the root node's `compatible`. |
| OPP voltages | `598 MHz @ 0.95 V` … `1.30 GHz @ 1.15 V` | **Unverified and probably wrong.** The vendor DTB carries no OPP table. Public MT67xx-class tables put ~1.17 GHz at 1.15 V and 1.3 GHz at ~1.21–1.30 V, so "1.3 GHz @ 1.15 V" would be an **undervolt**. |

#### Two regulators are both called "vproc"
The vendor DTB wires the CPU through `mtcpufreq { reg-ext-vproc-supply = <&sym827>; }`, so the
external SY8827N is the CPU core supply. But the MT6323 PMIC *also* has an always-on
`buck_vproc` (range `700000`–`1350000`, currently also at `1.15 V`), which caused a regulator
debugfs name collision — ours is therefore named **`ext_vproc`**. Whether both feed the same
rail is still unknown; if they do, lowering only the SY8827N cannot drop the rail below
1.15 V. That is safe but saves no power, and must be resolved before Stage C2.

#### The authoritative vendor OPP table (resolved)
From Amazon's own GPL kernel for this device,
`drivers/misc/mediatek/base/power/mt8163/mt_cpufreq.c`, active `#if 1 /* mt8163 */`
branch ([hexdump0815/linux-amazon-mediatek-mt8163-kernel-source, branch
`douglas-import`](https://github.com/hexdump0815/linux-amazon-mediatek-mt8163-kernel-source)):

| Freq (kHz) | VPROC (uV) |  | Freq (kHz) | VPROC (uV) |
|---|---|---|---|---|
| 1300000 | **1300000** |  | 871000 | 1150000 |
| 1216000 | 1250000 |  | 741000 | 1150000 |
| 1133000 | 1200000 |  | 624000 | 1150000 |
| 1050000 | **1150000** |  | 600000 | **1150000** |

Two conclusions that shaped the implementation:
1. **The vendor never goes below 1.150 V.** Every point from 600 MHz to 1050 MHz
   uses exactly the voltage LK already programmed and that we already run at. So
   there is no undervolting to be had, and the DT constraints (`950000`-`1150000`)
   already span the whole safe-without-thermal-management range.
2. **1.3 GHz needs 1.300 V**, not the 1.15 V this file previously claimed — that
   would have been a 150 mV undervolt. 1133 MHz and up all need more than 1.15 V.

The same file also confirms `PLL_SETTLE_TIME = 20` us, matching the `udelay(20)` in
`mt8163_armpll_safe_set_rate()` (plus the `udelay(20)` inside
`mtk_pll_set_rate_regs()`, so 40 us total, comfortably over MT8173's 30 us).

#### Implemented OPP table and measurements
`600 / 624 / 741 / 871 / 1001 / 1050` MHz, all at `1150000` uV — i.e. **every
vendor operating point that runs at 1.150 V**. 1050 MHz was added once hundreds of
transitions had been proven; it is the vendor's top point at this voltage, so it
costs no voltage change. Everything above it (1133/1216/1300 MHz) needs
1.20-1.30 V and is gated on the rail question below.

Full sweep, both directions, watchdog-protected — `t x f` is constant within 3.3%
across a 1.67x span, which is the proof that the cores really change speed:

| Freq | bench | t x f |
|---|---|---|
| 600000 | 4.186 s | 2.5115 |
| 624000 | 4.082 s | 2.5473 |
| 741000 | 3.433 s | 2.5437 |
| 871000 | 2.921 s | 2.5446 |
| 1001000 | 2.593 s | 2.5957 |

ARMPLL lands exactly on each requested rate (600 MHz comes out at 599,999,512 Hz
because the PCW truncates; everything else is exact). `schedutil` is the governor,
set from `start-phosh` so it is revertible without reflashing; hundreds of
transitions with no faults.

#### The SY8827N VSEL bank — latent bug found and fixed
`GPIO34` (`KPROW1`) is the chip's VSEL strap. Read non-invasively through
`/dev/mem`, it is **mode 0 (GPIO), direction output, level LOW**, so the chip takes
its output from **VSEL0** (datasheet: pin LOW = VSEL0, HIGH = VSEL1).

Mainline `fan53555.c` maps `sleep_vsel_id = 0` (the default when
`fcs,suspend-voltage-selector` is absent) to `vol_reg = VSEL1` — **the wrong bank**.
It only looked correct because both banks happen to hold `0xac`. `fcs,suspend-voltage-selector = <1>`
is now set, which selects `vol_reg = VSEL0`.

Note on reading pinctrl registers: the offsets in `pinctrl-mt8163.c` are relative to
the **`mediatek,pctl-regmap` syscon at `0x10005000`**, *not* the pio node's own
`reg = <0x1000b000>` (that is only the EINT block). Reading the wrong base silently
returns plausible-looking garbage — always validate against a pin with known state
(pins 57/58 must read mode 1 as `SDA1`/`SCL1`).

#### Remaining (needs task 4 first)
Adding 1133/1216/1300 MHz means raising VPROC to 1.20-1.30 V on a passively cooled
tablet that currently has **no thermal zones and no throttling**. Do task 4 first.
Also still unresolved: the MT6323 PMIC's own always-on `buck_vproc` is also at
1.15 V. If it feeds the same rail it pins the floor at 1.15 V — harmless, and equal
to the vendor's own floor, but it means the rail cannot be dropped further. It would
matter if anything ever wanted to go above 1.15 V.

### 4. Thermal Zones & CPU Throttling (`mtk_thermal`)
**Status**: Working and validated 2026-11-21 — **awaiting confirmation before marking done.**
SoC die temperature is readable and CPU throttling engages. The three TMP103 PCB
sensors are still not wired up (see below).

#### What it took
`mediatek,mt8163-thermal` is **already fully supported** in `drivers/thermal/mtk_thermal.c`
(`mt8163_thermal_data`: 5 sensors, 3 banks, auxadc channel 11, `MTK_THERMAL_V1`),
and `MTK_THERMAL`/`CPU_THERMAL`/`THERMAL_GOV_STEP_WISE`/`SENSORS_TMP103` were
already `=y`. Two real blockers had to be found:

**1. The temperature sensor buffer is left off by the bootloader.** MT8163 boots
with `TS_CON1 = 0x10` (`RG_TS2AUXADC = 2'b01`) and every `TEMP_MSRx` reads 0.
Amazon's driver clears those bits and waits:
```c
temp &= ~(0x00000030);   /* TS_CON1[5:4]=2'b00: Buffer on, TSMCU to AUXADC */
udelay(200);             /* wait 100uS than turn on thermal controller */
```
Mainline has `mtk_thermal_turn_on_buffer()` which does the equivalent, but it is
gated on `MTK_THERMAL_V2` and MT8163 is V1. Added an `enable_ts2auxadc` flag to
`mtk_thermal_data` plus a V1 helper matching the vendor sequence exactly.

**2. The AUXADC was powered down.** `mtk_thermal` drives the AUXADC over AHB but
never powers it up — that is `mt6577_auxadc.c`'s job, and
`CONFIG_MEDIATEK_MT6577_AUXADC` was **not set**, so `AUXADC_MISC` bit 14
(`PDN_EN`) stayed 0 and no conversion ever ran. **`mtk_thermal` has an implicit
dependency on the AUXADC driver being built.** `mediatek,mt8163-auxadc` is
already in that driver's match table. Enabling `CONFIG_MEDIATEK_MT6577_AUXADC=y`
fixed it; `AUXADC_MISC` became `0x00004008` (`PDN_EN=1`), `DAT11` started
returning live values, and `iio:device0 11001000.auxadc` appeared as a bonus.

Diagnosing this needed the driver's *real* register offsets — `TEMP_MSR0..3` are
at `0x090/0x094/0x098/0x0B8`, not the `0x030` block. Everything else the driver
writes was already correct and verifiable in-system (`PNPMUXADDR = 0x1000c604`
confirming apmixedsys really is at `0x1000c000`, `ADCVALIDADDR = 0x11001040` =
`AUXADC_DATA(11)`, `ADCMUX = ADCEN = 0x800`).

#### Measurements
| Condition | Temperature |
|---|---|
| Idle | ~32-33 C |
| 4x busy loop, sustained | rises to a ~48 C plateau |

`MSR0` reads a saturated `0xfff` while `MSR1..3` read ~3450; sensor 0 appears not
to be wired on this board. It does not skew the result because
`mtk_thermal_bank_temperature()` takes the max and a saturated raw maps low.

Throttling verified end-to-end with `CONFIG_THERMAL_EMULATION` (`emul_temp`
injection, so the DT trips stay untouched — trip temperatures from DT are **not**
writable because `mtk_thermal` implements no `set_trip_temp`):
```
emul= 80000  cdev=0/4  freq=1001000   <- below the 85 C trip, no throttle
emul= 90000  cdev=1/4  freq= 871000   <- trip crossed
emul=100000  cdev=2/4  freq= 741000
emul=110000  cdev=3/4  freq= 624000
emul= 70000  cdev=2/4  freq= 741000   <- steps back up
emul=     0  cdev=0                   <- fully released
```

#### Trip points and why
`75000` passive (informational, no cooling-map), `85000` passive (carries the
cooling-map), `115000` critical. Grounded in Amazon's own MT8163 driver
(`trip_temp[] = { 117000, 100000, 85000, 75000, 65000, ... }` with
`trip_temp[0]` documented as "the thermal HW protection point", `TARGET_TJ =
65000`) and corroborated by mainline `mt8173.dtsi` (68000/85000 passive,
115000 critical). Critical sits below the vendor's 117000 hardware point so
software acts first. Measured reality (~48 C under full load) is far below all of
them, so these are headroom protection rather than everyday behaviour.

#### Calibration is NOT wired (absolute offset remains) — FIXED 2026-11-21 evening, see HANDOFF.md §8D
`mtk_thermal.c` logs nothing and silently uses its nominal fallback
(`adc_ge = 512`, `vts = 260` for all sensors, `degc_cali = 40`, `o_slope = 0`).
Amazon's driver reads the real values as devinfo indices 7/8/9 and documents the
addresses as `0x10206180/184/188`, whose bit layout matches mainline's
`extract_efuse_v1()` exactly (buf[0]=0x180, buf[1]=0x184, buf[2]=0x188). But on
MT8163 `0x10206000` is **`disp_config2`** in the vendor DTB, not efuse. Scanning
that page finds exactly two non-zero words — `+0x100` (bit0 set,
`degc_cali = 54`, VTS 259/256) and `+0x104` (`adc_ge = 561`, VTS3 251), all
plausible — but **no valid third word anywhere in the page**, and MT8163 needs
`buf[2]` for VTS4 and VTSABB. A partial triplet would zero 2 of the 5 sensors and
`mtk_thermal_bank_temperature()` takes the max, so that is worse than uniform
defaults. **To do**: find the real efuse base (via the vendor devinfo mechanism),
then add `nvmem-cells`/`nvmem-cell-names = "calibration-data"` to the thermal node.
The readings are plausible without it, which is why trips keep wide margins.

#### Still to do here
- **TMP103 PCB sensors: resolved as not reachable.** Added as DT nodes on
  `&i2c2` and tested: the driver matches correctly (`ti,tmp103`,
  `CONFIG_SENSORS_TMP103=y`, modalias matches) but probe fails with
  `tmp103 2-0070: error writing config register`. A raw 1-byte read shows
  **nothing ACKs at 0x70/0x71/0x72** while 0x18 (bma253) and 0x6b (charger) on
  the same bus do — so that error is simply a NAK and the sensors are unpowered
  or not fitted. The nodes are left in the DT with `status = "disabled"` and the
  finding recorded inline. Find the rail that powers them (the MT6323 LDO list is
  the place to look) before re-enabling. Not needed for CPU protection - the SoC
  die sensor already drives throttling.
- Wire the real efuse calibration (above).
- **This unblocks task 3's remaining OPPs**: 1133/1216/1300 MHz at 1.20/1.25/1.30 V
  now have working throttling and a critical backstop behind them.

---

### 5. Audio Capture / Built-in Microphone (`VUL` / UL1)
**Status**: WORKING since 2026-11-21 evening — fixed by binding `douglas-alsa` to the
`afe` DT node (pm_domain + pinctrl). See `HANDOFF.md` §8A. Older notes below kept for history.
**Previous status**: Partly working 2026-11-21. The capture *pipeline* works end to end —
`pcm0c` exists, `arecord` runs cleanly at 48 kHz stereo S16_LE with correct
timing and no XRUN — but **every sample is an exact zero**, so the PMIC -> AFE
uplink is not delivering data. Playback is verified unregressed.

All of this is in `douglas-alsa.c` and needs **no kernel reflash** — it is an
out-of-tree module, so iterate with `insmod`. Backups:
`/root/mods/douglas-alsa.ko.playback-only-backup` on the device and
`douglas-port-backup/douglas-alsa.c.playback-only`.

#### Corrections to the register values previously listed here
Taken from Amazon's own `AudDrv_Afe.h` / `mt_soc_afe_connection.c` for MT8163
and verified live on the device:

| Item | Previously documented | Verified |
|---|---|---|
| `AFE_VUL_BASE` | `0x0060` | **`0x0080`** |
| `AFE_VUL_END` | `0x0068` | **`0x0088`** |
| `AFE_VUL_CUR` | `0x0064` | **`0x008C`** |
| UL interconnect | "`AFE_CONN1`/`AFE_CONN2`" | **`AFE_CONN3` (`0x002C`), bit 0 = I03->O09, bit 3 = I04->O10** |

The interconnect bits come from the vendor's `[input][output]` tables:
`mConnectionReg[I03][O09] = 0x2C` with `mConnectionbits[I03][O09] = 0`, and
`mConnectionReg[I04][O10] = 0x2C` with `mConnectionbits[I04][O10] = 3`.

Correct as previously documented: `AFE_ADDA_UL_SRC_CON0 = 0x0114`,
`AFE_DAC_CON0` bit 3 = `VUL_ON` (`Soc_Aud_AFEClockType_VUL_ON = 3`),
`ABB_AFE_CON0` bit 1 = PMIC UL enable, MICBIAS in `AUDTOP_CON8`.

#### The bug that made capture fail outright
`AFE_DAC_CON0` **bit 0 is the global AFE enable** (the vendor's `EnableAfe()`),
independent of the per-memif `VUL_ON` in bit 3. Playback sets `BIT(1)|BIT(0)`;
the first capture attempt set only `BIT(3)`, so the AFE never ran, `AFE_VUL_CUR`
stayed parked at `AFE_VUL_BASE`, no period ever elapsed, and alsa-lib reported
its wait timeout as `pcm_read: read error: I/O error`. With `BIT(0)` added,
recording produces correctly-sized files with correct timing.

#### Verified register sequence now implemented
Confirmed applied by sampling `/dev/mem` and the PMIC regmap debugfs
(`/sys/kernel/debug/regmap/1000d000.pwrap/registers`) *during* capture:
```
AFE:  AFE_VUL_BASE=0x11227000 AFE_VUL_END=0x11228fff
      AFE_DAC_CON1[19:16]=10 (48 kHz, same code DL1 uses in [3:0]), bit27=mono
      AFE_MEMIF_PBUF_SIZE[23:22]=0  (VUL fetch format = 16 bit)
      AFE_CONN_24BIT bits 9,10 = 0  (O09/O10 format = 16 bit;
                                     SetoutputConnectionFormat writes bit==output)
      AFE_CONN3 = 0x00000009        (I03->O09, I04->O10)
      AFE_ADDA_UL_SRC_CON0 = 0x001e0001  ((3<<19)|(3<<17) voice mode 48 kHz + enable)
      AFE_ADDA_NEWIF_CFG0 = 0x03f87201, AFE_ADDA_NEWIF_CFG1[11:10] = 3
      AFE_ADDA_UL_DL_CON0 bit0 = 1, AFE_DAC_CON0 = 0x...09 (AFE_ON | VUL_ON)
PMIC: AFE_PMIC_NEWIF_CFG2 = 0x3c2f  (0x302f | (GetULNewIFFrequency(48k)=3 << 10))
      ABB_AFE_CON1 bit4 = 1         (GetULFrequency(48k) = 1)
      ABB_AFE_CON11 bit8 toggled    (TopCtlChangeTrigger)
      ABB_AFE_CON0 bit1 = 1         (UL on)
      AUDTOP_CON0 = 0x7800/0x7f80, CON2 = 0x00f0/0x00ff, CON3 bit8 = 0,
      CON0[3:0] = 0 (main mic mux), CON8 bit3 = 1 (MICBIAS),
      CON0 bits 7,8 = 1 (LCH ADC+PGA), CON2 bits 0,1 = 1 (RCH ADC+PGA)
```
`MT6323_AUDTOP_CON0 = 0x0700` is in the **mainline** MT6323 header and matches
the vendor's `PMIC_REG_BASE + 0x700`, so no guessing was needed there.

SRAM is now split so capture and playback can run together: playback keeps
`0x11221000 + 0x6000`, capture gets `0x11227000 + 0x2000`. The vendor DTB maps
`0x9000` of AFE SRAM (`reg = <0 0x11220000 0 0x1000>, <0 0x11221000 0 0x9000>`),
so this fits.

#### 2026-11-21 evening: the silence is downstream of the PMIC — see `HANDOFF.md` §8A
MISO pad toggles during capture (PMIC transmitting); AFE sine generator injected at
I03/I04 or O09/O10 records zeros; DL1->AWB digital loopback records zeros; memifs do
write (marker pattern overwritten) but with zero data. The list below is therefore
obsolete as a theory of the fault; items 1-3 were implemented as ALSA controls anyway.
Diagnostic scripts: `douglas-audio-test/mic-diag-2026-11-21/`.
Driver fixes staged (effective next boot): capture now muxes pins 7/8/9, pinmux mask 0x7.

#### Why it is still silent, and what to try next
The samples are **exact** zeros (peak 0, 0.0% non-zero over 480000 samples), not
quiet audio — a real ADC always dithers. So the data path is disconnected
upstream of the analog gain, i.e. the PMIC's uplink serial interface is not
streaming into the AFE's ADDA UL block. The memif itself is fine (it advances and
wraps). Ordered list of things to try:
1. **`SetMicPGAGain()` is not ported.** Gain lives in `AUDTOP_CON0[6:4]` (LCH)
   and `AUDTOP_CON1[10:8]` (RCH), index 0..5 for `{-6, 0, 6, 12, 18, 24}` dB.
   At rest `AUDTOP_CON0 = 0x6010` gives index 1 (0 dB), so this alone should not
   produce *exact* zeros, but set it explicitly.
2. **The mic mux may be wrong.** The code hardcodes `AUDTOP_CON0[3:0] = 0`
   ("ADC1 main_mic"). The vendor supports mux 1 ("ADC2", headset mic) and 2
   ("ADC3", ref mic) with different `AUDTOP_CON1`/`CON3` writes. If the built-in
   mic is not on ADC1, mux 0 selects an unconnected input. **Expose the mux and
   PGA gain as ALSA controls and sweep them from userspace** rather than
   rebuilding per combination - that is the fastest way to converge.
3. **The mic may not be analog at all.** The vendor codec offers ACC / DCC /
   DMIC / DCCECMDIFF / DCCECMSINGLE modes selected at runtime by the Android
   HAL, and the kernel default (`AUDIO_ANALOGUL_MODE_ACC`) is just a default -
   the board's real choice lives in the HAL, not the kernel tree, and `dough.h`
   in the vendor audio directory is empty. If sweeping the mux finds nothing,
   port `TurnOnADcPowerDmic()` and `TurnOnADcPowerDCC()` and try those.
4. Check the SoC<->PMIC "NEWIF" settings agree on both sides. Playback sets the
   PMIC `AFE_PMIC_NEWIF_CFG0 = 0x8330`; the uplink may need bits there too, not
   just `CFG2`.
5. `AFE_ADDA_UL_SRC_CON1` and `AFE_ADDA_TOP_CON0` bit 0 (0 = internal ADC) are
   currently left at their defaults; confirm that is right.

#### Useful technique
The PMIC is not reachable through `/dev/mem`, but its regmap is exposed at
`/sys/kernel/debug/regmap/1000d000.pwrap/registers` after
`mount -t debugfs none /sys/kernel/debug`. Note `arecord` fails or finishes fast,
so a diagnostic must start sampling **before** `arecord` - otherwise `close()`
has already torn the registers down and you see only the disabled state.

---

### 6. True Deep Sleep (`s2idle` + DSI Panel Re-Init on Resume)
**Status**: Partly working as of 2026-11-21. **s2idle suspend and resume both work**;
two bugs remain on the resume path. **Awaiting confirmation before marking done.**

#### The PMIC interrupt line (the prerequisite nobody had wired)
The vendor DTB wires it on a child node, which mainline does not look at:
```
pmicmt6323 { interrupt-parent = <&pio>; interrupts = <0x18 0x04>; }
```
That is **EINT 24**, level-high, and EINT 24 is **pin 2 `PWRAP_INT`** — the dedicated
PMIC-wrapper interrupt pin, unclaimed by anything else. Declared on the `mt6323`
node itself (plus `interrupt-controller` / `#interrupt-cells = <2>`), the whole
chain now exists:
```
 39:  mt-eint  24 Level   mt6397-pmic     <- PMIC line into the SoC
188:  mt6397-irq  20 Edge  mt6397-rtc     <- RTC (task 2)
189:  mt6397-irq   5 Edge  mtk-pmic-keys  <- PWRKEY
190:  mt6397-irq  17 Edge  mtk-pmic-keys  <- HOMEKEY / Volume Down
```
`mt6397-core.c` was additionally hardened: a failing `irq_init` now warns and
continues instead of aborting the probe, because aborting would take down **every**
MT6323 regulator (vproc, vsys, vgp*, vmch/vmc) and everything depending on them.

#### Power key is now interrupt-driven — via upstream, not a rewrite
No need to hand-convert `douglas-keys.c`: in-tree `mtk-pmic-keys.c` already reads
`MT6323_CHRSTATUS` mask `0x2` for PWRKEY and `0x4` for HOMEKEY — *exactly* the bits
`douglas-keys.c` was polling for Power and Volume Down — interrupt-driven, with
per-key `wakeup-source`. So:
- `keys { compatible = "mediatek,mt6323-keys"; }` under `pmic`, first child = PWRKEY
  (`KEY_POWER`, `wakeup-source`), second = HOMEKEY (`KEY_VOLUMEDOWN`). Child order
  is what selects the key, not the node name.
- `CONFIG_KEYBOARD_MTK_PMIC=y`.
- `douglas-keys.c` trimmed to **Volume Up only** (that one is on the SoC keypad,
  `KPD MEM1` bit 0, not on the PMIC, and still needs polling). Reporting Power and
  Volume Down from both drivers would double every press. The old module is kept at
  `/root/mods/douglas-keys.ko.pmic-polling-backup` for instant rollback, and the
  source at `douglas-keys/douglas-keys.c.orig-pmic-polling`.
- `elogind` picks the power button up by itself:
  `Watching system buttons on /dev/input/event0 (mtk-pmic-keys)`.

#### s2idle works
With an RTC wakealarm at +15 s:
```
=== echo freeze > /sys/power/state at 10:47:05 ===
=== RETURNED rc=0 at 10:47:20 -- slept 15s ===
rtc irq after: 188: 0 1 0 0  mt6397-rtc      <- RTC alarm was the wake source
DSI after: connected  backlight=12  ext_vproc: 1150000  phoc: running  wlan0: 1
```
The power key cannot be pressed remotely, so **use the RTC wakealarm for all remote
suspend testing**. It is an independent hardware wake and it is proven to fire.

#### Bug 1: watchdog reset the board during suspend — **FIXED**
`mtk_wdt_suspend()` only stopped the watchdog `if (watchdog_active(...))`, i.e. only
when something had called `watchdog_start()`. On this board the bootloader leaves the
watchdog running and the kernel adopts it as `WDOG_HW_RUNNING`
(`Watchdog enabled (timeout=31 sec, nowayout=0)`), for which `watchdog_active()` is
**false** — so it kept counting through suspend and reset at ~31 s.

Fix in `drivers/watchdog/mtk_wdt.c`: suspend/resume now also act on
`watchdog_hw_running()`. Verified decisively by sleeping **longer than the timeout**:
```
armed RTC +50s (watchdog timeout is 31s)
=== RETURNED rc=0 after 50s ===     suspend_stats: success=1 fail=0
RESULT: slept 50s > 31s watchdog timeout -- BUG 1 FIXED
```
Note the trade-off: the watchdog is no longer a safety net *during* suspend, so a
hang in the suspend path now needs a physical power-hold. It is restarted on resume,
so it still covers runtime hangs (which is what auto-recovers bug 4 below).

#### Bug 2: Oops on resume in bq24190 — **FIXED** (it was our own bug)
```
Internal error: Oops: level 3 translation fault, WnR=1  (pte=0, CPU 2, kworker/u8:1)
pc : queued_spin_lock_slowpath
  _raw_spin_lock_irqsave / pm_stay_awake / power_supply_changed
  bq24190_pm_resume+0x90/0xa4 / __device_resume / async_resume
```
Not a mainline bug and nothing to do with secondary CPUs. `douglas-battery.c` did:
```c
remove_existing_psy("bq24190-battery");   /* -> power_supply_unregister() */
remove_existing_psy("battery");
```
i.e. it called `power_supply_unregister()` on a power supply **owned by another
driver** in order to claim the name "battery". That frees the object while
`bq24190_charger.c` still holds it in `bdi->battery`, so
`bq24190_pm_resume()`'s `if (bdi->battery) power_supply_changed(bdi->battery);`
was a use-after-free on every resume. The same object being poked after teardown
also produced the two boot-time warnings
`wakeup.c:560 wakeup_source_report_event` ("unregistered wakeup source") and
`workqueue.c:1502 __queue_work`.

Fix, using the mechanism upstream provides for exactly this:
- `omit-battery-class;` on the `bq24296@6b` charger node, so `bq24190_charger.c`
  never registers its deprecated battery psy and `bdi->battery` stays NULL.
- `remove_existing_psy()` deleted from `douglas-battery.c`. **Never
  `power_supply_unregister()` another driver's supply.**

Verified: `/sys/class/power_supply/` now holds only `battery` and
`bq24190-charger` (no `bq24190-battery`), the gauge still reads correctly, the two
boot warnings are **gone** (2 -> 0), and across two suspend/resume cycles plus 150 s
of monitoring there were **no Oopses**.
Rollback artifacts: `/root/mods/douglas-battery.ko.hijack-backup` and
`douglas-port-backup/douglas-battery.c.with-remove-psy-hack`.

#### Bug 3: DSI vblank timeout on resume — still open, now localised
```
WARNING: drm_vblank.c:1269 drm_wait_one_vblank
  drm_mode_config_helper_resume+0x48/0x8c [drm_kms_helper]
  mtk_drm_sys_complete+0x28/0x4c [mediatek_drm]
  dpm_complete / dpm_resume_end / suspend_devices_and_enter
```
Two such warnings per resume. **It is not suspend-specific** — the same warning
fires during normal running (seen at t=331 with `Comm: phoc`, no suspend involved,
probably on a backlight/modeset path), so this is a pre-existing display-pipeline
problem that resume merely exercises reliably. Non-fatal: the connector stays
`connected`, the backlight value survives and `phoc` keeps running. Needs
`mtk_dsi.c` + `panel-amazon-douglas-nt35521.c` to re-run the NT35521 init
sequence, per the original plan.

#### Bug 4: system dies ~3-5 min after resume (RCU stall) — the real blocker
Suspend and resume themselves are clean, but the box becomes unresponsive a few
minutes later and the watchdog resets it. Seen twice with different victims:
```
rcu: INFO: rcu_preempt detected stalls on CPUs/tasks: 2-...0
  task:tx_thread   HifRegDump / kalDevPortWrite / nicTxCmd / wlanSendCommand
      preceded by [wlan]HifRegDump: HIF Reg 0x0 = 0x108163
```
```
rcu: Stack dump where RCU GP kthread last ran:  Task dump for CPU 1:
  task:gsd-power  state:R running
```
Both look like a driver busy-waiting on hardware that did not survive s2idle. The
MT6625L consys/Wi-Fi vendor driver has no suspend/resume support at all and its HIF
own-clear handshake fails after resume; `gsd-power` polling the battery suggests the
MT6323 AUXADC/pwrap path may be similarly affected. **This is what has to be solved
for suspend to be usable**, most likely by stopping/reinitialising the consys stack
around suspend.

Careful: bringing Wi-Fi down as a workaround does not currently work either —
`pkill wpa_supplicant` oopsed in generic AF_PACKET teardown
(`virt_to_folio` <- `kfree` <- `free_pg_vec` <- `packet_set_ring` <-
`packet_release` <- `sock_close`), with no suspend involved. That is a separate
pre-existing bug worth its own investigation.

### 7. Upstream Cleanup (Consolidate Out-of-Tree `.ko` Modules into `libreecho-kernel` + Device Tree)
**Goal**: Fold our working runtime modules into clean git commits in [libreecho-kernel](file:///home/bjesus/firehd8-pmos/libreecho-kernel/) and [mt8163-douglas.dts](file:///home/bjesus/firehd8-pmos/libreecho-kernel/arch/arm64/boot/dts/mediatek/mt8163-douglas.dts).

#### Modules to Fold In
1. **[douglas-keys.c](file:///home/bjesus/firehd8-pmos/douglas-keys/douglas-keys.c)** — **mostly done** (2026-11-21): Power and Volume Down now come from in-tree `mtk-pmic-keys` via the `keys` child of `pmic: mt6323`, interrupt-driven (see task 6). Only Volume Up is left in the out-of-tree module, because it is on the SoC keypad rather than the PMIC. Remaining: fold that one key into a `keypad@10010000` node or move the file under `drivers/input/keyboard/`.
2. **[douglas-alsa.c](file:///home/bjesus/firehd8-pmos/douglas-alsa/douglas-alsa.c)** $\rightarrow$ Move into `sound/soc/mediatek/mt8163/mt8163-douglas.c`.
3. **[douglas-battery.c](file:///home/bjesus/firehd8-pmos/douglas-alsa/douglas-battery.c)** $\rightarrow$ Move into `drivers/power/supply/mt6323_auxadc_battery.c`.
4. **I2C1 (`i2c@11009000`) sensors** $\rightarrow$ Declare `bma253@18` (with `mount-matrix`) and `jsa1212@44` directly under `&i2c2` (`0x11009000`) in `mt8163-douglas.dts` so `echo ... > new_device` in `init-tier1.sh` is no longer needed.

---

## Session Notes / Traps (2026-11-21)

### Build trap that cost a boot cycle — check this before every flash
`kbuild/.config` had `CONFIG_INITRAMFS_SOURCE="/home/bjesus/douglas-port-backup/le-initfs"`,
which is the **boot-#1 diagnostic** initramfs: it dumps logs and then runs `sleep 1000000`,
never mounting `/dev/mmcblk0p16`. `douglas-boot31.img` was built against
`/tmp/opencode/le-initfs` (tmpfs, long gone), whose `init` does
`switch_root /mnt/root /sbin/douglas-init`. Flashing a kernel built on that stale config
boots, registers the USB gadget, and then sits there forever with no rootfs and no network.
- The real initramfs has been recovered out of `boot31`'s binary into a persistent
  **`/home/bjesus/firehd8-pmos/douglas-initfs/`** and the config points there.
- Before flashing, diff the new config against the running kernel's:
  extract `boot31`'s with the `IKCFG_ST` marker inside its gzipped `Image` and compare.
  That config also had `MTK_IOMMU`/`MTK_SMI`/`MEMORY`/`SND_SOC_MT8163_DOUGLAS` turned on
  as untested WIP — five untested changes in one flash is what made this hard to diagnose.
- `boot31`'s built-in DTB can be recovered the same way (scan the decompressed `Image`
  for the `d00dfeed` FDT magic), which is how the known-good `bootargs` were confirmed.

### USB-ethernet link wedges after every tablet reboot
The host-side `cdc_ether` comes up with `carrier=1` but frames never leave the interface
(`tx` counter frozen, `tcpdump` sees nothing). `ip link set ... up` does not fix it; forcing
USB re-enumeration does. **`/root/usbkick.sh` on `stockholm-aladdin`** does this (toggles
`authorized` on the `0525:a4a1` gadget, re-adds `10.42.0.1/24`, waits for ping). Run it
before every post-reboot SSH attempt.

### Hardware watchdog is a verified auto-recovery net
`/dev/watchdog0` (`mtk-wdt 10007000.watchdog`, `nowayout=0`, max useful timeout ~31 s) was
tested by arming it and not petting it: the board reset itself and came back. Arm it around
any risky operation (live PLL reprogram, DVFS transition) so a CPU hang reboots instead of
needing a physical Power+VolDown. Disarm with a magic `V` write before closing.

### Pre-existing phosh startup race (fixed in userspace)
`mediatek-disp-ovl`/`rdma` reference `mediatek,larb`, but `CONFIG_MTK_SMI` is off, so `larb0`
never probes and both sit in deferred probe until the **10 s `deferred_probe_timeout`**
expires — `mediatek-drm` only binds at `t ~= 15.3 s`. `phoc` starts earlier, finds no DRM
device, dies with `Found 0 GPUs, cannot create backend`, and never retries. `boot31` only won
this race by luck. `start-phosh` now waits for a DRM **connector** (`/sys/class/drm/card*-*`;
`card0`/`renderD128` are panfrost and appear at `t ~= 1 s`, so they are not a valid signal).
**Proper fix attempted 2026-11-21 and it regresses the GPU — do not just enable it.**
`CONFIG_MTK_SMI` is `tristate ... if COMPILE_TEST`, so it is not user-selectable and
is only `select`ed by `CONFIG_MTK_IOMMU`: it is all-or-nothing. With `MTK_IOMMU=y`
the stall does disappear (mediatek-drm binds at **t=4.34 s** instead of 15.3 s) and
the IOMMU works (`mtk-iommu bound 14016000.larb`, ovl/rdma join iommu group 0) —
**but panfrost dies**: it probes at t=0.59 s instead of 1.04 s and reports
`gpu soft reset timed out` / `Fatal error during GPU init` /
`probe failed with error -110`, leaving no `renderD128`, no GPU acceleration and a
dead phoc. Linking `drivers/memory` + `drivers/iommu` reorders initcalls so panfrost
runs before the MFG power domain / `mali-supply` is ready. Fix that ordering and both
work. That image is kept as `douglas-boot44.img` to reproduce; the DT already has the
full chain (`m4u@10205000`, `smi_common`, `larb0`, `iommus = <&iommu ...>`).

### The Wi-Fi driver floods `dmesg`
`ECHO_WLAN_HIF` debug prints wrap the kernel log buffer within minutes, destroying boot logs.
Capture `dmesg` immediately after boot, or silence those prints. `pstore`/`ramoops` is empty
(not mounted / not populated), and the diagnostic initramfs's `dd ... seek=98501` with
`bs=4096` targets `0x180D5000`, not `0x98501000` — i.e. that log dump never worked.

### Boot image history
- `douglas-boot31.img` — last known-good pre-session image (**rollback target**).
- `douglas-boot34.img` — Stage A: `i2c1` bus + `aliases` + `mediatek,mt8163` root compatible.
- `douglas-boot35.img` — Stage B: `+ silergy,sym827` regulator (`ext_vproc`).
- `douglas-boot36.img` — Stage C1: `+ OPP table` (2 points, voltage-flat) and governors.
- `douglas-boot37.img` — Stage C2: `+` vendor 5-point OPP ladder, `fcs,suspend-voltage-selector = <1>`.
- `douglas-boot38.img` — `+` PMIC EINT 24 wiring, `mtk-pmic-keys`, graceful PMIC-IRQ
  fallback in `mt6397-core.c`. Fixes the RTC (task 2) and makes the power key
  interrupt-driven. Pairs with the trimmed `douglas-keys.ko` (Volume Up only); the
  old one is at `/root/mods/douglas-keys.ko.pmic-polling-backup`.
- `douglas-boot39.img` — `+` `mtk_wdt` suspend fix (bug 1) and `omit-battery-class`
  (bug 2). Pairs with the patched `douglas-battery.ko`; the old one is at
  `/root/mods/douglas-battery.ko.hijack-backup`.
- `douglas-boot43.img` — `+` thermal: `auxadc`/`thermal` nodes, thermal zone with
  trips + cooling-map, `#cooling-cells` on all CPUs, `enable_ts2auxadc` in
  `mtk_thermal.c`, `CONFIG_MEDIATEK_MT6577_AUXADC=y`,
  `CONFIG_THERMAL_EMULATION=y`.
- `douglas-boot44.img` — `+` `CONFIG_MTK_IOMMU`/`MTK_SMI`: removes the 10 s boot
  stall but **breaks panfrost**. Kept only to reproduce that (see task 6 notes).
- `douglas-boot47.img` — `+` 1050 MHz OPP, TMP103 nodes disabled. **Current.**

Out-of-tree module versions currently on the device (`/root/mods/`), each with a
rollback copy beside it:
- `douglas-alsa.ko` — playback + capture. Rollback:
  `douglas-alsa.ko.playback-only-backup`.
- `douglas-keys.ko` — Volume Up only. Rollback:
  `douglas-keys.ko.pmic-polling-backup`.
- `douglas-battery.ko` — without the power-supply hijack. Rollback:
  `douglas-battery.ko.hijack-backup`.
Both `boot31` and the staged images are also on `stockholm-aladdin:/root/douglas-flash/`.
`boot32` was the failed diagnostic-initramfs image; do not flash it.

### I2C bus numbering changed
Adding `i2c@11008000` shifted the sensor bus, so an `aliases` node now pins
`i2c0/i2c1/i2c2` to `0x11007000`/`0x11008000`/`0x11009000`. The sensors moved from `i2c-1`
to **`i2c-2`**, and `init-tier1.sh` now resolves the bus by matching `of_node` against
`i2c@11009000` instead of hardcoding a number.

### The original Android install is still intact
`mmcblk0p14` is a complete LineageOS root filesystem (1.6 GB) and `p17` holds an
`ANDROID!` boot image. `mount -o ro /dev/mmcblk0p14 /mnt/p14`. This is a useful
reference for "what did the vendor actually do on this board" questions - the
MediaTek audio HAL config was read from there. Caveat: the per-device mic mode is
chosen at runtime by the HAL from a parameter store, so it is *not* in the static
XML, which is why the microphone question could not be settled this way.

### pstore/ramoops works — dmesg will not survive
`ramoops@98400000` is in the DTS and `CONFIG_PSTORE_{RAM,CONSOLE,PMSG}=y`, but
nothing mounts it. `mount -t pstore pstore /mnt/pstore` gives
`console-ramoops-0` plus `dmesg-ramoops-N` crash records, which is how the
`bq24190_pm_resume` Oops above was recovered. This is the only way to debug a
suspend or boot failure on this board: the Wi-Fi driver wraps the kernel log buffer
within minutes, and `/tmp` is a tmpfs that a reset wipes. **Write test logs to the
ext4 rootfs (`/root/...`), not `/tmp`**, and run anything that suspends under
`setsid nohup` so dropping the USB network does not kill it.

Note the diagnostic initramfs's `dd ... bs=4096 seek=98501` targets `0x180D5000`,
not `0x98501000` (that would need `seek=623873`), and `0x180D5000` is below the
DRAM base — so that log dump never wrote anywhere. The `0x98500200` markers also
read back clobbered after a TWRP visit, so TWRP does use that DRAM. pstore is the
reliable mechanism; those hand-rolled markers are not.
