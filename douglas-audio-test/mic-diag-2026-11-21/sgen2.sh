uptime
for v in 24862862 048C2762; do
  python3 /root/afew.py 1f0 $v
  timeout 10 arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 1 /root/cap_sg.wav 2>&1 | tail -1
  python3 /root/afew.py 1f0 f0000000 >/dev/null
  echo -n "SGEN=$v -> "; python3 /root/ana.py /root/cap_sg.wav
done
