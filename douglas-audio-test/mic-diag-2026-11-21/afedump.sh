cat > /root/afedump.py <<'PY'
import mmap, os, struct, sys, time
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x11220000)
def r(o): return struct.unpack_from("<I", m, o)[0]
tag=sys.argv[1]
for rng in ((0x000,0x040),(0x06c,0x070),(0x100,0x144),(0x3d8,0x3dc),(0x4c4,0x4c8)):
    print(tag, " ".join("%03x=%08x"%(o,r(o)) for o in range(rng[0],rng[1],4)))
# repeat-sample the UL monitor candidates to see if anything moves
for o in (0x118,0x11c,0x120,0x12c,0x130,0x134,0x140):
    vals=set(r(o) for i in range(200))
    print(tag, "reg %03x distinct values over 200 reads: %d  e.g. %s" % (o, len(vals), ["%08x"%v for v in list(vals)[:4]]))
PY
python3 /root/afedump.py IDLE
( sleep 1; python3 /root/afedump.py CAP ) > /root/afe.log 2>&1 &
arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 3 /root/cap.wav >/dev/null 2>&1
cat /root/afe.log
( sleep 1; python3 /root/afedump.py PLAY ) > /root/afe2.log 2>&1 &
dd if=/dev/urandom bs=4 count=144000 2>/dev/null | aplay -q -D hw:0,0 -f S16_LE -r 48000 -c 2 >/dev/null 2>&1
cat /root/afe2.log
