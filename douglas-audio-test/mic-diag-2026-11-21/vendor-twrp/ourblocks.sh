python3 - <<'PY'
import mmap, os, struct
f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
blocks=[("DEVAPC_AO",0x1000e000,0x1000),("DEVAPC",0x10207000,0x1000),("EMI",0x10203000,0x1000),("PERICFG",0x10003000,0x1000),("M4U1",0x10214000,0x1000),("M4U0",0x10205000,0x1000),("SMI_COMMON",0x14017000,0x1000),("INFRA_AO",0x10001000,0x1000),("TOPCKGEN",0x10000000,0x1000),("APMIXED",0x1000c000,0x1000),("MCUCFG",0x10200000,0x1000),("SPM",0x10006000,0x1000)]
for name,base,size in blocks:
    try: m = mmap.mmap(f, size, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
    except Exception as e: print(name,"FAIL",e); continue
    for o in range(0,size,4):
        v=struct.unpack_from("<I", m, o)[0]
        if v: print("%s %03x %08x"%(name,o,v))
PY
