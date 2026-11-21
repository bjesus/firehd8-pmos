cat > /root/rng.py <<'PY'
import mmap, os, struct, sys
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x11220000)
def r(o): return struct.unpack_from("<I", m, o)[0]
a=int(sys.argv[2],16); b=int(sys.argv[3],16)
print(sys.argv[1], " ".join("%03x=%08x"%(o,r(o)) for o in range(a,b,4) if r(o)), flush=True)
PY
echo "== 0x0a0-0x100 idle"; python3 /root/rng.py IDLE 0a0 100
echo "== MSB before:"; python3 /root/afew.py 0cc
( sleep 1; python3 /root/rng.py CAP 0a0 100 ) > /root/rng.log 2>&1 &
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/cap.wav >/dev/null 2>&1
sleep 0.5; cat /root/rng.log
echo "== test: clear AFE_MEMIF_MSB entirely, then capture"
python3 /root/afew.py 0cc 00000000
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/cap_msb.wav >/dev/null 2>&1
python3 /root/ana.py /root/cap_msb.wav
echo "== MSB after:"; python3 /root/afew.py 0cc
