#!/usr/bin/env python3
# Flash TWRP to recovery_x + set MISC to boot-recovery, via amonet BROM modules.
# Usage: run with stdin from a FIFO; feed Enter when prompted about the short.
import sys, os

AMONET = "/home/bjesus/firehd8-pmos/amonet-dir/amonet"
os.chdir(os.path.join(AMONET, "modules"))
sys.path.insert(0, os.getcwd())

from common import Device
from handshake import handshake
from load_payload import load_payload
from gpt import parse_gpt_compat


def flash_binary(dev, path, start_block, max_size=0):
    with open(path, "rb") as fin:
        data = fin.read()
    while len(data) % 0x200 != 0:
        data += b"\x00"
    if max_size and len(data) > max_size:
        raise RuntimeError("data too big to flash")
    blocks = len(data) // 0x200
    for x in range(blocks):
        print("[{} / {}]".format(x + 1, blocks), end='\r')
        dev.emmc_write(start_block + x, data[x * 0x200:(x + 1) * 0x200])
        if x % 10 == 0:
            dev.kick_watchdog()
    print("")


def main():
    dev = Device()
    dev.find_device()          # waits for BROM (/dev/ttyACM*)
    handshake(dev)
    load_payload(dev, "../brom-payload/build/payload.bin")  # prompts for Enter (short removal)
    dev.kick_watchdog()

    dev.emmc_switch(0)         # switch to user partitions
    block = dev.emmc_read(0)
    if block[510:512] != b"\x55\xAA":
        dev.reboot()
        raise RuntimeError("bad GPT magic")
    dev.kick_watchdog()

    data = (dev.emmc_read(0x400 // 0x200) + dev.emmc_read(0x600 // 0x200) +
            dev.emmc_read(0x800 // 0x200) + dev.emmc_read(0xA00 // 0x200) +
            dev.emmc_read(0xC00 // 0x200))
    gpt, gpt_header, part_list = parse_gpt_compat(dev.emmc_read(0x200 // 0x200) + data)

    rx = gpt.get("recovery_x")
    misc = gpt.get("MISC")
    if not rx or not misc:
        raise RuntimeError("partition not found: %s" % ("recovery_x" if not rx else "MISC"))

    print("Flashing TWRP to recovery_x (start block %d)" % rx[0])
    flash_binary(dev, "../bin/twrp.img", rx[0], rx[1] * 0x200)

    print("Setting MISC to boot-recovery")
    blk = list(dev.emmc_read(misc[0]))
    blk[0:16] = b"boot-recovery\x00\x00\x00"
    dev.emmc_write(misc[0], bytes(blk))

    print("Rebooting into TWRP")
    dev.reboot()


if __name__ == "__main__":
    main()
