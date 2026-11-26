#!/usr/bin/env python3
"""Create the MT6625L Wi-Fi NVRAM file the vendor driver reads at /data/nvram/APCFG/APRDEB/WIFI.

Without it the firmware picks a new random MAC on every boot. The file mirrors the driver's
compiled defaults (wlanGetDefaultWifiMfg in gl_init.c: TX power table, EFUSE override bytes,
band-edge limits, 5 GHz enabled) and adds a MAC address that is stable per device: locally
administered, derived from the eMMC CID (the factory MAC lives in Amazon's IDME data, which
this port does not read). Layout: WIFI_CFG_PARAM_STRUCT, 512 bytes (CFG_Wifi_File.h).
"""
import hashlib, os, pathlib, struct, sys

PATH = pathlib.Path("/data/nvram/APCFG/APRDEB/WIFI")

def device_id():
    for p in ("/sys/class/mmc_host/mmc0/mmc0:0001/cid", "/etc/machine-id"):
        try:
            return pathlib.Path(p).read_text().strip()
        except OSError:
            pass
    return "douglas"

def mac():
    override = os.environ.get("DOUGLAS_WIFI_MAC")
    if override:
        return bytes(int(x, 16) for x in override.split(":"))
    h = hashlib.sha256(device_id().encode()).digest()
    return bytes([0x02, 0x08, 0x22]) + h[:3]   # MediaTek OUI with the locally-administered bit

def build(addr):
    tx = bytes([0x26, 0x26, 0x00, 0x00,
                0x22, 0x22, 0x22, 0x22, 0x21, 0x21,
                0x22, 0x22, 0x22, 0x20, 0x20, 0x20,
                0x21, 0x21, 0x21, 0x1E, 0x1E, 0x1E,
                0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                0x22, 0x22, 0x22, 0x20, 0x20, 0x20,
                0x22, 0x22, 0x22, 0x20, 0x20, 0x20])
    assert len(tx) == 40
    efuse = bytearray(144); efuse[10:13] = bytes([0x01, 0x20, 0x18])
    part1 = bytearray(256)
    struct.pack_into("<HH", part1, 0, 2, 0)      # u2Part1OwnVersion=2 (not 1: that path forces defaults), PeerVersion=0
    part1[4:10] = addr
    part1[10:12] = b"\x00\x00"                     # country code: unset, as the compiled defaults
    part1[12:52] = tx
    part1[52:196] = efuse
    part1[196] = 1    # ucTxPwrValid
    part1[197] = 1    # ucSupport5GBand
    part1[198] = 1    # fg2G4BandEdgePwrUsed
    part1[199] = 0x26; part1[200] = 0x1E; part1[201] = 0x1A   # band edge CCK / OFDM20 / OFDM40
    part1[202] = 0; part1[203] = 0                             # regulatory channel list map / index
    part2 = bytearray(256)
    struct.pack_into("<HH", part2, 0, 2, 0)      # u2Part2OwnVersion=2, PeerVersion=0
    part2[4] = 0; part2[5] = 0                   # uc2G4BwFixed20M, uc5GBwFixed20M
    part2[6] = 1                                 # ucEnable5GBand
    return bytes(part1 + part2)

if __name__ == "__main__":
    if PATH.exists() and "--force" not in sys.argv:
        sys.exit(0)
    addr = mac()
    PATH.parent.mkdir(parents=True, exist_ok=True)
    PATH.write_bytes(build(addr))
    print("douglas-wifi-nvram: wrote %s with MAC %s" % (PATH, ":".join("%02x" % b for b in addr)))
