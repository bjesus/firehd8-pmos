python3 /root/pinmode.py 1 >/dev/null
echo "silence playback, SGEN off:"
( sleep 1; python3 /root/din.py play-zero ) > /root/d1.log 2>&1 &
dd if=/dev/zero bs=4 count=144000 2>/dev/null | aplay -q -D hw:0,0 -f S16_LE -r 48000 -c 2 >/dev/null 2>&1; cat /root/d1.log
echo "silence playback, SGEN at O03/O04 (set 0.3s after start):"
( sleep 0.3; python3 /root/afew.py 1f0 2c8c28c2; sleep 0.7; python3 /root/din.py play-sgen-O03 ) > /root/d2.log 2>&1 &
dd if=/dev/zero bs=4 count=144000 2>/dev/null | aplay -q -D hw:0,0 -f S16_LE -r 48000 -c 2 >/dev/null 2>&1; cat /root/d2.log
python3 /root/afew.py 1f0 f0000000 >/dev/null
echo "random playback for reference:"
( sleep 1; python3 /root/din.py play-random ) > /root/d3.log 2>&1 &
dd if=/dev/urandom bs=4 count=144000 2>/dev/null | aplay -q -D hw:0,0 -f S16_LE -r 48000 -c 2 >/dev/null 2>&1; cat /root/d3.log
python3 /root/pinmode.py 0 >/dev/null
