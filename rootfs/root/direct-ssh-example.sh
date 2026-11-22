#!/bin/sh
# direct tablet access over Wi-Fi IPv6 (bypasses the relay)
ssh -i ~/.ssh/id_tablet -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 -o BatchMode=yes root@fdfc:d767:207::87b "cat > /tmp/s.sh; timeout ${2:-90} sh /tmp/s.sh </dev/null" < "$1" 2>&1 | grep -v 'WARNING\|post-quantum\|openssh\|Permanently added'
