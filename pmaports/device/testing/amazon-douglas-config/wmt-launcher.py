#!/usr/bin/env python3
import fcntl, glob, os, select, struct

def _IOC(d, t, nr, sz):
    return (d << 30) | (sz << 16) | (t << 8) | nr
_IOW  = lambda t, nr, sz: _IOC(1, t, nr, sz)
_IOR  = lambda t, nr, sz: _IOC(2, t, nr, sz)

MAGIC = 0xa0
WMT_IOCTL_SET_STP_MODE     = _IOW(MAGIC, 5, 4)
WMT_IOCTL_SET_PATCH_NUM    = _IOW(MAGIC, 14, 4)
WMT_IOCTL_SET_PATCH_INFO   = _IOW(MAGIC, 15, 8)
WMT_IOCTL_WMT_QUERY_CHIPID = _IOR(MAGIC, 22, 4)

patches = []
for path in sorted(glob.glob("/lib/firmware/ROMv2_lm_patch_*_hdr.bin")):
    hdr = open(path, "rb").read(30)
    pinfo = hdr[24:28]
    total = (pinfo[0] & 0xF0) >> 4
    seq = pinfo[0] & 0x0F
    addr = b"\x00" + pinfo[1:4]
    patches.append((seq, addr, path))
    print(f"Found patch seq={seq}/{total} addr={addr.hex()} path={path}", flush=True)
patches.sort(key=lambda x: x[0])

def send_patches(fd):
    fcntl.ioctl(fd, WMT_IOCTL_SET_PATCH_NUM, len(patches))
    for seq, addr, path in patches:
        info = bytearray(struct.pack("<I4s256s", seq, addr, path.encode()))
        fcntl.ioctl(fd, WMT_IOCTL_SET_PATCH_INFO, info)

fd = os.open("/dev/stpwmt", os.O_RDWR)
chipid = fcntl.ioctl(fd, WMT_IOCTL_WMT_QUERY_CHIPID, 0)
print(f"QUERY_CHIPID = {chipid:#x}", flush=True)
fcntl.ioctl(fd, WMT_IOCTL_SET_STP_MODE, 3)

print("WMT launcher ready (2 patches, addr[0]=0) on /dev/stpwmt...", flush=True)
while True:
    r, _, _ = select.select([fd], [], [], 5.0)
    if not r:
        continue
    cmd = os.read(fd, 256).split(b"\x00")[0].decode(errors="replace")
    print(f"WMT cmd: {cmd!r}", flush=True)
    if "srh_patch" in cmd:
        send_patches(fd)
    os.write(fd, b"ok")
