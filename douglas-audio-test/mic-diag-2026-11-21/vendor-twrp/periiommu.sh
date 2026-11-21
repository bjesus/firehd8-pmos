python3 - <<'PY'
import mmap, os, struct
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
def dump(name, base, size=0x1000, allw=False):
    m = mmap.mmap(f, size, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
    out=[]
    for o in range(0,size,4):
        v=struct.unpack_from("<I", m, o)[0]
        if v or allw: out.append("%03x=%08x"%(o,v))
    print(name, "base %08x nonzero=%d:"%(base,len(out)), " ".join(out[:200]))
dump("PERISYS_IOMMU(M4U1)", 0x10214000)
dump("G3D_IOMMU(M4U2)    ", 0x13005000)
dump("PERICFG 0x200-0x240", 0x10003000, 0x400)
PY
