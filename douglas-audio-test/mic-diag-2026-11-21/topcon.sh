cat > /root/topcon.py <<'PY'
import mmap, os, struct, time, sys
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=0x11220000)
def r(o): return struct.unpack_from("<I", m, o)[0]
for i in range(int(sys.argv[1])):
    print("%.2f TOP_CON0=%08x DAC_CON0=%08x UL_SRC_CON0=%08x UL_DL_CON0=%08x VUL_CUR=%08x" % (time.time()%100, r(0), r(0x10), r(0x114), r(0x124), r(0x8c)), flush=True)
    time.sleep(0.25)
PY
python3 /root/topcon.py 1
python3 /root/topcon.py 12 > /root/topcon.log 2>&1 &
sleep 0.5
arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/cap.wav 2>&1
sleep 2
cat /root/topcon.log
python3 - <<'PY'
import struct
d=open('/root/cap.wav','rb').read()[44:]
s=struct.unpack('<%dh'%(len(d)//2), d)
print("samples",len(s),"peak",max(abs(x) for x in s),"nonzero",sum(1 for x in s if x))
PY
