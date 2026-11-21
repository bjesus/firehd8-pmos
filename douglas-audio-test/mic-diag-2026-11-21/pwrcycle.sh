cat > /root/pwr.py <<'PY'
import mmap, os, struct, time, functools
print = functools.partial(print, flush=True)
f = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
spm = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x10006000)
afe = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ|mmap.PROT_WRITE, offset=0x11220000)
def r(m,o): return struct.unpack_from("<I", m, o)[0]
def w(m,o,v): struct.pack_into("<I", m, o, v)
CON=0x29c; STA=0x60c; STA2=0x610; BIT=1<<24
RST_B,ISO,ON,ON2,CLKDIS = 1,2,4,8,16
SRAM_PDN=0xf00; SRAM_ACK=0xf000
def wait(cond, what, t=0.2):
    t0=time.time()
    while time.time()-t0 < t:
        if cond(): return True
        time.sleep(0.0005)
    print("  TIMEOUT waiting", what, "CON=%08x STA=%08x STA2=%08x" % (r(spm,CON), r(spm,STA), r(spm,STA2))); return False
print("before: CON=%08x STA=%08x STA2=%08x TOP_CON0=%08x" % (r(spm,CON), r(spm,STA), r(spm,STA2), r(afe,0)))
v=r(spm,CON)
# --- power off (scpsys_power_off order) ---
v |= SRAM_PDN; w(spm,CON,v); wait(lambda: (r(spm,CON)&SRAM_ACK)==SRAM_ACK, "sram pdn ack")
v |= ISO; w(spm,CON,v)
v &= ~RST_B; w(spm,CON,v)
v |= CLKDIS; w(spm,CON,v)
v &= ~ON; w(spm,CON,v)
v &= ~ON2; w(spm,CON,v)
wait(lambda: (r(spm,STA)&BIT)==0 and (r(spm,STA2)&BIT)==0, "power off")
print("off:    CON=%08x STA=%08x STA2=%08x" % (r(spm,CON), r(spm,STA), r(spm,STA2)))
time.sleep(0.01)
# --- power on (scpsys_power_on order) ---
v=r(spm,CON)
v |= ON; w(spm,CON,v)
v |= ON2; w(spm,CON,v)
wait(lambda: (r(spm,STA)&BIT) and (r(spm,STA2)&BIT), "power on ack")
v &= ~CLKDIS; w(spm,CON,v)
v &= ~ISO; w(spm,CON,v)
v |= RST_B; w(spm,CON,v)
v &= ~SRAM_PDN; w(spm,CON,v); wait(lambda: (r(spm,CON)&SRAM_ACK)==0, "sram on ack")
print("on:     CON=%08x STA=%08x STA2=%08x TOP_CON0(after reset)=%08x DAC_CON1=%08x MAXLEN=%08x MSB=%08x" % (r(spm,CON), r(spm,STA), r(spm,STA2), r(afe,0), r(afe,0x14), r(afe,0x3d4), r(afe,0xcc)))
# re-init like vendor AudDrv_Clk_AllOn / Auddrv_Bus_Init
w(afe,0,0x00004000)
print("TOP_CON0 now %08x" % r(afe,0))
PY
python3 /root/pwr.py
echo "== capture after proper power cycle"
timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 3 /root/cap_pwr.wav 2>&1 | tail -1
python3 /root/ana.py /root/cap_pwr.wav
echo "== playback sanity"; dd if=/dev/zero bs=4 count=48000 2>/dev/null | aplay -q -D hw:0,0 -f S16_LE -r 48000 -c 2 && echo aplay-ok
python3 /root/afew.py 0
