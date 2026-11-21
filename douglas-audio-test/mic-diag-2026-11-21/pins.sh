cat > /root/pins.py <<'PY'
import mmap, os, struct, sys
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x10005000)
def r(o): return struct.unpack_from("<I", m, o)[0]
def mode(p): return (r(((p//5)<<4)+0x600) >> (3*(p%5))) & 7
def bit(base,p): return (r(base + (((p>>4)&0xf)<<4)) >> (p&0xf)) & 1
print(sys.argv[1], end=': ')
for p in (7,8,9,57,58):
    print("pin%d mode=%d dir=%d pullen=%d pullsel=%d din=%d" % (p, mode(p), bit(0,p), bit(0x100,p), bit(0x200,p), bit(0x500,p)), end=' | ')
print()
print("IES 0x900=%08x 0x910=%08x  SMT? 0x920=%08x 0x930=%08x 0x940=%08x" % (r(0x900), r(0x910), r(0x920), r(0x930), r(0x940)))
PY
python3 /root/pins.py idle
( sleep 1; python3 /root/pins.py during-capture ) > /root/pins.log 2>&1 &
arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 3 /root/cap.wav >/dev/null 2>&1
cat /root/pins.log
( sleep 1; python3 /root/pins.py during-playback ) > /root/pins2.log 2>&1 &
aplay -D hw:0,0 -d 3 /usr/share/sounds/alsa/Front_Center.wav >/dev/null 2>&1 || (dd if=/dev/zero bs=4 count=144000 2>/dev/null | aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 >/dev/null 2>&1)
cat /root/pins2.log
