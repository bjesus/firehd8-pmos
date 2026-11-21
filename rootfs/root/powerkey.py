import os, struct, time, fcntl, ctypes
UI_SET_EVBIT=0x40045564; UI_SET_KEYBIT=0x40045565; UI_DEV_CREATE=0x5501; UI_DEV_DESTROY=0x5502
EV_SYN=0; EV_KEY=1; KEY_POWER=116
f=os.open('/dev/uinput', os.O_WRONLY|os.O_NONBLOCK)
fcntl.ioctl(f, UI_SET_EVBIT, EV_KEY); fcntl.ioctl(f, UI_SET_KEYBIT, KEY_POWER)
# struct uinput_user_dev: name[80], id(4x u16), ff_effects_max u32, absmax/absmin/absfuzz/absflat 4*64 ints
dev = struct.pack('80sHHHHI', b'fake-power-key', 0x19, 0x1, 0x1, 1, 0) + b'\0'*(4*64*4)
os.write(f, dev); fcntl.ioctl(f, UI_DEV_CREATE); time.sleep(0.5)
def ev(t,c,v): os.write(f, struct.pack('llHHi', 0,0,t,c,v))
ev(EV_KEY,KEY_POWER,1); ev(EV_SYN,0,0); time.sleep(0.1); ev(EV_KEY,KEY_POWER,0); ev(EV_SYN,0,0)
time.sleep(0.5); fcntl.ioctl(f, UI_DEV_DESTROY); print("power key sent")
