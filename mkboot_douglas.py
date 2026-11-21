#!/usr/bin/env python3
"""Pack a mainline arm64 Linux Image into an LK-compatible Amazon Fire HD 8 (douglas) boot.img."""
import gzip
import hashlib
import os
import struct
import sys
import zlib


def extract_vendor_dtb_tail(vendor_boot_img: str, out_tail: str) -> bytes:
    if os.path.exists(out_tail):
        return open(out_tail, "rb").read()
    with open(vendor_boot_img, "rb") as f:
        hdr = f.read(2048)
        ksize, kaddr, rsize, raddr, ssize, saddr, tags, psize = struct.unpack("<8I", hdr[8:40])
        f.seek(psize)
        kblob = f.read(ksize)
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    d.decompress(kblob)
    tail = d.unused_data
    assert tail[:4] == b"\xd0\x0d\xfe\xed", f"Expected FDT magic, got {tail[:4].hex()}"
    with open(out_tail, "wb") as f:
        f.write(tail)
    return tail


def make_empty_cpio_gz() -> bytes:
    # Minimal valid SVR4 newc cpio archive containing only TRAILER!!!
    name = b"TRAILER!!!\x00"
    hdr = (
        b"070701"
        + b"00000000" * 9
        + f"{len(name):08x}".encode()
        + b"00000000"
    )
    rec = hdr + name
    # Pad to 4-byte boundary, then pad archive to 512 bytes
    rec += b"\x00" * ((4 - (len(rec) % 4)) % 4)
    rec += b"\x00" * ((512 - (len(rec) % 512)) % 512)
    return gzip.compress(rec, compresslevel=9, mtime=0)


def pad_page(data: bytes, page_size: int = 2048) -> bytes:
    rem = len(data) % page_size
    return data if rem == 0 else data + b"\x00" * (page_size - rem)


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <Image> <out_boot.img> [cmdline]")
        sys.exit(1)

    image_path = sys.argv[1]
    out_path = sys.argv[2]
    cmdline = (sys.argv[3] if len(sys.argv) > 3 else "bootopt=64S3,32N2,64N2").encode()

    raw_image = open(image_path, "rb").read()
    gz_image = gzip.compress(raw_image, compresslevel=9, mtime=0)
    dtb_tail = extract_vendor_dtb_tail(
        "/tmp/opencode/boot.img",
        "/home/bjesus/douglas-port-backup/vendor-dtb-tail.bin",
    )
    kernel_blob = gz_image + dtb_tail
    ramdisk_blob = make_empty_cpio_gz()

    page_size = 2048
    kaddr = 0x40080000
    raddr = 0x43480000
    saddr = 0x40F00000
    tags = 0x48000000

    sha = hashlib.sha1()
    sha.update(kernel_blob)
    sha.update(struct.pack("<I", len(kernel_blob)))
    sha.update(ramdisk_blob)
    sha.update(struct.pack("<I", len(ramdisk_blob)))
    sha.update(b"")
    sha.update(struct.pack("<I", 0))
    digest = sha.digest() + b"\x00" * 12

    hdr = bytearray(page_size)
    hdr[0:8] = b"ANDROID!"
    struct.pack_into(
        "<10I",
        hdr,
        8,
        len(kernel_blob),
        kaddr,
        len(ramdisk_blob),
        raddr,
        0,
        saddr,
        tags,
        page_size,
        0,
        0,
    )
    hdr[64 : 64 + len(cmdline)] = cmdline
    hdr[576 : 576 + 32] = digest

    out = bytes(hdr) + pad_page(kernel_blob, page_size) + pad_page(ramdisk_blob, page_size)
    with open(out_path, "wb") as f:
        f.write(out)

    print(
        f"Wrote {out_path}: total={len(out)} bytes "
        f"(raw Image={len(raw_image)}, gz={len(gz_image)}, dtb_tail={len(dtb_tail)}, ramdisk={len(ramdisk_blob)})"
    )
    print(f"  kaddr={kaddr:#x} raddr={raddr:#x} tags={tags:#x} cmdline={cmdline!r}")


if __name__ == "__main__":
    main()
