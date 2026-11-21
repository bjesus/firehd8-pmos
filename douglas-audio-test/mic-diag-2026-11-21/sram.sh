cat > /root/sram.py <<'PY'
import mmap, os, struct
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
m = mmap.mmap(f, 0xa000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11221000)
def r(o): return struct.unpack_from("<I", m, o)[0]
def w(o,v): struct.pack_into("<I", m, o, v)
for off in (0x0000, 0x5ffc, 0x6000, 0x6004, 0x7000, 0x7ffc, 0x8000, 0x8ffc, 0x9000, 0x9ffc):
    old=r(off); w(off, 0x5a5aa5a5); a=r(off); w(off, 0x12345678); b=r(off); w(off, old)
    print("sram+%05x (phys %08x): old=%08x wr5a->%08x wr12->%08x %s" % (off, 0x11221000+off, old, a, b, "OK" if (a==0x5a5aa5a5 and b==0x12345678) else "FAIL"))
PY
python3 /root/sram.py
echo "== is memif writing nonzero into SRAM? fill UL region with 0x7777 then capture 1s with the driver; dump first words after"
python3 - <<'PY'
import mmap, os, struct
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
m = mmap.mmap(f, 0x2000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11227000)
print("before:", ["%08x"%struct.unpack_from("<I",m,o)[0] for o in (0,4,8,0x1000,0x1ffc)])
PY
( sleep 0.7; python3 - <<'PY'
import mmap, os, struct
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
m = mmap.mmap(f, 0x2000, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x11227000)
d = m[:0x2000]
print("during capture: nonzero bytes in UL SRAM = %d / %d" % (sum(1 for b in d if b), len(d)))
print("sample words:", ["%08x"%struct.unpack_from("<I",d,o)[0] for o in (0,4,0x800,0x1000,0x1800,0x1ffc)])
PY
) > /root/sramcap.log 2>&1 &
arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/cap.wav >/dev/null 2>&1
cat /root/sramcap.log
