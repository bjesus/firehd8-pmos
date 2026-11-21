#!/bin/sh
# usage: run.sh script.sh  -> runs it on the tablet via relay, after usbkick
ssh -o ConnectTimeout=20 root@stockholm-aladdin '/root/usbkick.sh >/dev/null 2>&1; cat > /tmp/s.sh; cat /tmp/s.sh | ssh -o ConnectTimeout=20 root@10.42.0.2 "cat > /tmp/s.sh; sh /tmp/s.sh </dev/null"' < "$1" 2>&1 | grep -v 'WARNING\|post-quantum\|openssh'
