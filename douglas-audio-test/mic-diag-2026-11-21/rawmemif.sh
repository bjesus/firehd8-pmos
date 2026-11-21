cat > /root/rawmemif.py <<'PY'
import mmap, os, struct, time, functools
print = functools.partial(print, flush=True)
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
afe = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11220000)
sram = mmap.mmap(f, 0x9000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11221000)
def r(o): return struct.unpack_from("<I", afe, o)[0]
def w(o,v): struct.pack_into("<I", afe, o, v)
def upd(o,mask,val): w(o, (r(o) & ~mask) | (val & mask))
def fill(off, n, pat):
    for o in range(off, off+n, 4): struct.pack_into("<I", sram, o, pat)
def stats(off, n):
    words=[struct.unpack_from("<I",sram,o)[0] for o in range(off,off+n,4)]
    return "zero=%d marker=%d other=%d sample=%s" % (sum(1 for x in words if x==0), sum(1 for x in words if x==0x7e7e7e7e), sum(1 for x in words if x not in (0,0x7e7e7e7e)), ["%08x"%x for x in words[:4]]+["%08x"%x for x in words[-2:]])
print("start: TOP_CON0=%08x DAC_CON0=%08x DAC_CON1=%08x" % (r(0), r(0x10), r(0x14)))
# ---- AWB memif (O05/O06): base 0x70 end 0x78 cur 0x7c, rate DAC_CON1[15:12], enable DAC_CON0 bit6
fill(0x1000, 0x1000, 0x7e7e7e7e)
w(0x70, 0x11222000); w(0x78, 0x11222fff)
upd(0x14, 0xf<<12, 0xa<<12); upd(0x14, 1<<24, 0)
upd(0x3d8, 3<<20, 0); upd(0x6c, (1<<5)|(1<<6), 0)
w(0x1f0, 0x3c6c26c2)
upd(0x10, (1<<6)|1, (1<<6)|1)
t=[]
for i in range(5): time.sleep(0.05); t.append("%08x"%r(0x7c))
time.sleep(0.2)
print("AWB: CUR trace", t, "->", stats(0x1000, 0x1000))
upd(0x10, (1<<6)|1, 0); w(0x1f0, 0xf0000000)
# ---- VUL memif raw (O09/O10) into 0x11223000
fill(0x2000, 0x1000, 0x7e7e7e7e)
w(0x80, 0x11223000); w(0x88, 0x11223fff)
upd(0x14, 0xf<<16, 0xa<<16); upd(0x14, 1<<27, 0)
upd(0x3d8, 3<<22, 0); upd(0x6c, (1<<9)|(1<<10), 0)
w(0x1f0, 0x5c6c26c2)
upd(0x10, (1<<3)|1, (1<<3)|1)
t=[]
for i in range(5): time.sleep(0.05); t.append("%08x"%r(0x8c))
time.sleep(0.2)
print("VUL: CUR trace", t, "->", stats(0x2000, 0x1000))
upd(0x10, (1<<3)|1, 0); w(0x1f0, 0xf0000000)
# ---- DL1 sanity: does the sinegen reach a memif we KNOW works? use DL1 memif to *read* - n/a. Instead test VUL with CONN3 I03/I04 and SGEN at I03/I04:
fill(0x2000, 0x1000, 0x7e7e7e7e)
upd(0x2c, 0x9, 0x9)
w(0x1f0, 0x24862862)
upd(0x10, (1<<3)|1, (1<<3)|1)
time.sleep(0.3)
print("VUL via I03/I04 SGEN:", "CUR=%08x" % r(0x8c), stats(0x2000, 0x1000))
upd(0x10, (1<<3)|1, 0); w(0x1f0, 0xf0000000); upd(0x2c, 0x9, 0)
print("SGEN readback after off: %08x  DAC_CON0=%08x" % (r(0x1f0), r(0x10)))
PY
python3 /root/rawmemif.py
