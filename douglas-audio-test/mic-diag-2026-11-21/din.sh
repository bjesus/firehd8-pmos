cat > /root/din.py <<'PY'
import mmap, os, struct, sys, time
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x10005000)
def r(o): return struct.unpack_from("<I", m, o)[0]
N=20000; c=[0,0,0]
t=time.time()
for i in range(N):
    v=r(0x500)   # din port 0, pins 0..15
    for k,p in enumerate((7,8,9)):
        c[k]+= (v>>p)&1
print(sys.argv[1], "din ones/%d: pin7=%d pin8=%d pin9=%d  (%.2fs)" % (N, c[0], c[1], c[2], time.time()-t))
PY
python3 /root/pinmode.py 1 >/dev/null
python3 /root/din.py idle-mode1
( sleep 1; python3 /root/din.py capture ) > /root/din.log 2>&1 &
arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 4 /root/cap.wav >/dev/null 2>&1
cat /root/din.log
( sleep 1; python3 /root/din.py playback ) > /root/din2.log 2>&1 &
dd if=/dev/zero bs=4 count=192000 2>/dev/null | aplay -q -D hw:0,0 -f S16_LE -r 48000 -c 2 >/dev/null 2>&1
cat /root/din2.log
python3 /root/pinmode.py 0 >/dev/null
