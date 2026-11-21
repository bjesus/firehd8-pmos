cat > /root/loop.py <<'PY'
import mmap, os, struct, time, functools
print = functools.partial(print, flush=True)
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
afe = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11220000)
sram = mmap.mmap(f, 0x9000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11221000)
def r(o): return struct.unpack_from("<I", afe, o)[0]
def w(o,v): struct.pack_into("<I", afe, o, v)
def upd(o,mask,val): w(o, (r(o) & ~mask) | (val & mask))
def stats(off, n):
    words=[struct.unpack_from("<I",sram,o)[0] for o in range(off,off+n,4)]
    return "zero=%d marker=%d other=%d sample=%s" % (sum(1 for x in words if x==0), sum(1 for x in words if x==0x7e7e7e7e), sum(1 for x in words if x not in (0,0x7e7e7e7e)), ["%08x"%x for x in words[:6]])
print("start: DAC_CON0=%08x CONN1=%08x CONN2=%08x CONN3=%08x I2S_CON1=%08x" % (r(0x10), r(0x24), r(0x28), r(0x2c), r(0x34)))
# DL1 source: 0x11221000..0x11221fff with pattern (L=0x1111+i, R=0x2222+i)
for i in range(0, 0x1000, 4): struct.pack_into("<I", sram, i, ((0x2222+i//4)<<16) | (0x1111+i//4))
w(0x40, 0x11221000); w(0x48, 0x11221fff)
upd(0x14, 0xf, 0xa); upd(0x14, 1<<21, 0)
# AWB sink: 0x11223000..0x11223fff marker-filled
for o in range(0x2000, 0x3000, 4): struct.pack_into("<I", sram, o, 0x7e7e7e7e)
w(0x70, 0x11223000); w(0x78, 0x11223fff)
upd(0x14, 0xf<<12, 0xa<<12); upd(0x14, 1<<24, 0)
upd(0x3d8, 3<<20, 0); upd(0x6c, (1<<5)|(1<<6), 0)
# interconnect I05->O05 (CONN2 bit19), I06->O06 (CONN2 bit24)
upd(0x28, (1<<19)|(1<<24), (1<<19)|(1<<24))
# go: AFE_ON | DL1_ON | AWB_ON
upd(0x10, (1<<6)|(1<<1)|1, (1<<6)|(1<<1)|1)
t=[]
for i in range(4): time.sleep(0.05); t.append("DL1=%08x AWB=%08x" % (r(0x44), r(0x7c)))
time.sleep(0.2)
print("DL1->AWB loopback:", t)
print("AWB sink:", stats(0x2000, 0x1000))
upd(0x10, (1<<6)|(1<<1)|1, 0)
upd(0x28, (1<<19)|(1<<24), 0)
# second: also try with SGEN at I05/I06 (0x348C28C2) into AWB
for o in range(0x2000, 0x3000, 4): struct.pack_into("<I", sram, o, 0x7e7e7e7e)
upd(0x28, (1<<19)|(1<<24), (1<<19)|(1<<24))
w(0x1f0, 0x348C28C2)
upd(0x10, (1<<6)|1, (1<<6)|1)
time.sleep(0.3)
print("SGEN I05/I06 -> AWB:", stats(0x2000, 0x1000))
upd(0x10, (1<<6)|1, 0); w(0x1f0, 0xf0000000); upd(0x28, (1<<19)|(1<<24), 0)
# restore DL1 base/end as the driver left them
w(0x40, 0x11221000); w(0x48, 0x11226fff)
print("end: DAC_CON0=%08x" % r(0x10))
PY
python3 /root/loop.py
