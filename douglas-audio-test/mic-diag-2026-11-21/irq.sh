echo "IRQ regs before:"; python3 /root/afew.py 3a0; python3 /root/afew.py 3a4; python3 /root/afew.py 3b0; python3 /root/afew.py 3b4
# IRQ2: cnt=1024 frames, rate code 0xa in IRQ_MCU_CON[11:8], enable bit1
python3 /root/afew.py 3b0 00000400 >/dev/null
python3 - <<'PY'
import mmap, os, struct
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
m = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11220000)
v = struct.unpack_from("<I", m, 0x3a0)[0]
v = (v & ~(0xf<<8)) | (0xa<<8) | 0x2
struct.pack_into("<I", m, 0x3a0, v)
print("IRQ_MCU_CON=%08x" % struct.unpack_from("<I", m, 0x3a0)[0])
PY
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /root/cap_irq.wav 2>/dev/null
python3 /root/ana.py /root/cap_irq.wav
echo "IRQ status after:"; python3 /root/afew.py 3a4
python3 /root/afew.py 3a8 00000002 >/dev/null   # clear irq2
python3 /root/afew.py 3a0 00000000 >/dev/null
python3 /root/afew.py 3a4
