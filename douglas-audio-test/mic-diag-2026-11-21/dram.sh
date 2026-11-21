cat > /root/dram.py <<'PY'
import mmap, os, struct, time, ctypes, functools
print = functools.partial(print, flush=True)
libc = ctypes.CDLL(None, use_errno=True)
PAGE=4096
buf = mmap.mmap(-1, PAGE, flags=mmap.MAP_SHARED|mmap.MAP_ANONYMOUS, prot=mmap.PROT_READ|mmap.PROT_WRITE)
buf[:] = b'\x00'*PAGE
addr = ctypes.addressof(ctypes.c_char.from_buffer(buf))
if libc.mlock(ctypes.c_void_p(addr), ctypes.c_size_t(PAGE)) != 0: print("mlock failed", ctypes.get_errno())
with open("/proc/self/pagemap","rb") as pm:
    pm.seek((addr//PAGE)*8); ent = struct.unpack("<Q", pm.read(8))[0]
present = (ent>>63)&1; pfn = ent & ((1<<55)-1)
phys = pfn*PAGE
print("vaddr=%x present=%d pfn=%x phys=%08x" % (addr, present, pfn, phys))
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
afe = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11220000)
def r(o): return struct.unpack_from("<I", afe, o)[0]
def w(o,v): struct.pack_into("<I", afe, o, v)
time.sleep(0.3)
print("before: DAC_CON0=%08x BASE=%08x END=%08x CUR=%08x SGEN=%08x" % (r(0x10), r(0x80), r(0x88), r(0x8c), r(0x1f0)))
w(0x80, phys); w(0x88, phys+PAGE-1)
time.sleep(0.3)
print("after: BASE=%08x END=%08x CUR=%08x" % (r(0x80), r(0x88), r(0x8c)))
time.sleep(0.3)
print("later: CUR=%08x" % r(0x8c))
# evict caches a bit
junk = bytearray(16*1024*1024)
for i in range(0, len(junk), 64): junk[i] = 1
d = bytes(buf)
nz = sum(1 for b in d if b)
print("DRAM page: nonzero bytes=%d/%d sample words=%s" % (nz, PAGE, ["%08x"%struct.unpack_from("<I",d,o)[0] for o in (0,4,8,0x800,0xffc)]))
# also read via /dev/mem
dm = mmap.mmap(f, PAGE, mmap.MAP_SHARED, mmap.PROT_READ, offset=phys)
d2 = dm[:PAGE]
print("via /dev/mem: nonzero bytes=%d/%d sample=%s" % (sum(1 for b in d2 if b), PAGE, ["%08x"%struct.unpack_from("<I",d2,o)[0] for o in (0,4,8,0x800,0xffc)]))
PY
echo "== test A: plain mic capture -> DRAM page"
( sleep 0.8; python3 /root/dram.py ) > /root/dram.log 2>&1 &
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 4 /root/cap.wav >/dev/null 2>&1
sleep 1; cat /root/dram.log
echo "== test B: SGEN at O09/O10 -> DRAM page"
python3 /root/afew.py 1f0 5c6c26c2 >/dev/null
( sleep 0.8; python3 /root/dram.py ) > /root/dram2.log 2>&1 &
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 4 /root/cap.wav >/dev/null 2>&1
python3 /root/afew.py 1f0 f0000000 >/dev/null
sleep 1; cat /root/dram2.log
