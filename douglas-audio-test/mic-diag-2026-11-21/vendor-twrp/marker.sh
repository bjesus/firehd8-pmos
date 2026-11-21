cat > /root/marker.py <<'PY'
import mmap, os, struct, time, ctypes, functools, sys
print = functools.partial(print, flush=True)
PAGE=4096
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
afe = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11220000)
def r(o): return struct.unpack_from("<I", afe, o)[0]
def w(o,v): struct.pack_into("<I", afe, o, v)
libc = ctypes.CDLL(None, use_errno=True)
buf = mmap.mmap(-1, PAGE, flags=mmap.MAP_SHARED|mmap.MAP_ANONYMOUS, prot=mmap.PROT_READ|mmap.PROT_WRITE)
for o in range(0,PAGE,4): struct.pack_into("<I", buf, o, 0x7e7e7e7e)
addr = ctypes.addressof(ctypes.c_char.from_buffer(buf)); libc.mlock(ctypes.c_void_p(addr), ctypes.c_size_t(PAGE))
with open("/proc/self/pagemap","rb") as pm:
    pm.seek((addr//PAGE)*8); ent = struct.unpack("<Q", pm.read(8))[0]
phys=(ent & ((1<<55)-1))*PAGE
# also SRAM slice markers at 0x11223000 (not the playback or UL slices)
sr = mmap.mmap(f, PAGE, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11223000)
for o in range(0,PAGE,4): struct.pack_into("<I", sr, o, 0x7e7e7e7e)
time.sleep(0.3)
print("DAC_CON0 before=%08x (bit1 DL1=%d) VUL_CUR=%08x" % (r(0x10), (r(0x10)>>1)&1, r(0x8c)))
def reenable(base):
    w(0x10, r(0x10) & ~0x8); w(0x80, base); w(0x88, base+PAGE-1); w(0x10, r(0x10) | 0x8)
    t=[]; 
    for i in range(5): time.sleep(0.08); t.append("%08x"%r(0x8c))
    return t
t=reenable(0x11223000); time.sleep(0.2)
words=[struct.unpack_from("<I",sr,o)[0] for o in range(0,PAGE,4)]
print("SRAM 0x11223000: CUR trace", t, "marker=%d zero=%d other=%d DAC_CON0=%08x" % (sum(1 for x in words if x==0x7e7e7e7e), sum(1 for x in words if x==0), sum(1 for x in words if x not in (0,0x7e7e7e7e)), r(0x10)))
t=reenable(phys); time.sleep(0.2)
junk=bytearray(16*1024*1024)
for i in range(0,len(junk),64): junk[i]=1
dm = mmap.mmap(f, PAGE, mmap.MAP_SHARED, mmap.PROT_READ, offset=phys); d=dm[:PAGE]
words=[struct.unpack_from("<I",d,o)[0] for o in range(0,PAGE,4)]
print("DRAM %08x: CUR trace"%phys, t, "marker=%d zero=%d other=%d DAC_CON0=%08x" % (sum(1 for x in words if x==0x7e7e7e7e), sum(1 for x in words if x==0), sum(1 for x in words if x not in (0,0x7e7e7e7e)), r(0x10)))
PY
pgrep -a pulseaudio | head -2
( sleep 0.8; python3 /root/marker.py ) > /root/marker.log 2>&1 &
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 4 /root/cap.wav >/dev/null 2>&1; sleep 1; cat /root/marker.log
