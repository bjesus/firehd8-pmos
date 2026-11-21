python3 - <<'PY'
import mmap, os, struct
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
def dump(name, base, size=0x1000):
    try:
        m = mmap.mmap(f, size, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
    except Exception as e:
        print(name, "mmap fail", e); return
    out=[]
    for o in range(0,size,4):
        v=struct.unpack_from("<I", m, o)[0]
        if v: out.append("%03x=%08x"%(o,v))
    print(name, "base %08x nonzero=%d:"%(base,len(out)), " ".join(out[:160]))
dump("M4U      ", 0x10205000)
dump("SMI_COMMON", 0x14017000)
dump("LARB0    ", 0x14016000)
dump("PERICFG  ", 0x10003000, 0x400)
dump("INFRA_AO ", 0x10001000, 0x400)
PY
cat /proc/device-tree/soc/m4u@10205000/status 2>/dev/null; echo; ls /proc/device-tree/soc/m4u@10205000/ | tr '\n' ' '
