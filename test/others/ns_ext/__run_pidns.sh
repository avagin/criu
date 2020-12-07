#!/bin/bash

set -e

status_fd=$1

exec 100</proc/self/status
pid=$(cat <&100 | grep '^Pid:' | awk '{print $2}')
exec 0</dev/null
exec 2>/dev/null
exec 1>/dev/null
exec 100>&-
echo $pid >&$status_fd
exec {status_fd}>&-

while :; do
	sleep 10000
done
