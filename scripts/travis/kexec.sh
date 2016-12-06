#!/bin/bash

set -x

ppid=""
pid=$$
while :; do
	p=`cat /proc/$pid/status | grep PPid | awk '{ print $2 }'`
	if [ "$p" -eq 1 ]; then
		break;
	fi
	ppid=$pid
	pid=$p
	echo $pid
done

ls -l /boot/

setsid bash -c "setsid ./scripts/travis/kexec-dump.sh $ppid < /dev/null &> /travis.log &"
while :; do
	sleep 30
	tail -f /travis.log
	tail -f /imgs/dump.log
	tail -f /imgs/restore.log
	uname -a
	uptime
	ps axf
	if [ -f /rebooted ]; then
		break;
	fi
done
