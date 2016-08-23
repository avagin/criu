#!/bin/bash -x

pid=$1
vpid=$2
sig=$3
comm=$4

exec &>> /tmp/zdtm-core.log

expr match "$comm" zombie00 && {
	cat > /dev/null
	exit 0
}

report="/tmp/zdtm-core-$pid-$comm"
exec &> ${report}.txt

cat /sys/kernel/debug/tracing/trace | tail -n 1024

ps axf
ps -p $pid

cat /proc/$pid/status
ls -l /proc/$pid/fd
cat /proc/$pid/maps
exec 33< /proc/$pid/exe
cat > $report.core

echo 'bt
i r
disassemble $rip-0x10,$rip + 0x10
' | gdb -c $report.core /proc/self/fd/33 -e /home/travis/build/avagin/criu/test/lib64/ld-linux-x86-64.so.2
