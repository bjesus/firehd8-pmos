cat > /root/reen.py <<'PY'
import mmap, os, struct, time, ctypes, functools, sys
print = functools.partial(print, flush=True)
PAGE=4096
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
afe = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11220000)
def r(o): return struct.unpack_from("<I", afe, o)[0]
def w(o,v): struct.pack_into("<I", afe, o, v)
target = sys.argv[1]
if target == "dram":
    libc = ctypes.CDLL(None, use_errno=True)
    buf = mmap.mmap(-1, PAGE, flags=mmap.MAP_SHARED|mmap.MAP_ANONYMOUS, prot=mmap.PROT_READ|mmap.PROT_WRITE)
    buf[:] = b'\x00'*PAGE
    addr = ctypes.addressof(ctypes.c_char.from_buffer(buf))
    libc.mlock(ctypes.c_void_p(addr), ctypes.c_size_t(PAGE))
    with open("/proc/self/pagemap","rb") as pm:
        pm.seek((addr//PAGE)*8); ent = struct.unpack("<Q", pm.read(8))[0]
    phys = (ent & ((1<<55)-1))*PAGE
else:
    phys = 0x11221000
    sr = mmap.mmap(f, PAGE, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=phys)
    for o in range(0, PAGE, 4): struct.pack_into("<I", sr, o, 0x7e7e7e7e)
time.sleep(0.3)
print(target, "before: DAC_CON0=%08x BASE=%08x END=%08x CUR=%08x SGEN=%08x" % (r(0x10), r(0x80), r(0x88), r(0x8c), r(0x1f0)))
w(0x10, r(0x10) & ~0x8)          # VUL_ON off
w(0x80, phys); w(0x88, phys+PAGE-1)
w(0x10, r(0x10) | 0x8)           # VUL_ON on
c=[]
for i in range(6):
    time.sleep(0.1); c.append("%08x"%r(0x8c))
print(target, "CUR trace after re-enable:", c)
junk = bytearray(16*1024*1024)
for i in range(0, len(junk), 64): junk[i] = 1
dm = mmap.mmap(f, PAGE, mmap.MAP_SHARED, mmap.PROT_READ, offset=phys)
d = dm[:PAGE]
words=[struct.unpack_from("<I",d,o)[0] for o in range(0,PAGE,4)]
print(target, "page: zero=%d marker=%d other=%d sample=%s" % (sum(1 for x in words if x==0), sum(1 for x in words if x==0x7e7e7e7e), sum(1 for x in words if x not in (0,0x7e7e7e7e)), ["%08x"%x for x in words[:6]]))
PY
python3 /root/afew.py 1f0 5c6c26c2 >/dev/null
for t in sram dram; do
  ( sleep 0.8; python3 /root/reen.py $t ) > /root/reen_$t.log 2>&1 &
  timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 3 /root/cap.wav >/dev/null 2>&1
  sleep 1; cat /root/reen_$t.log
done
python3 /root/afew.py 1f0 f0000000 >/dev/null
