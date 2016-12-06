#!/bin/bash

set -x

uname -a

ppid=""
pid=$$
while :; do
	ls -l /proc/$pid/fd
	ls -lL /proc/$pid/fd
	ls -lL /proc/$pid/exe
	p=`cat /proc/$pid/status | grep PPid | awk '{ print $2 }'`
	if [ "$p" -eq 1 ]; then
		break;
	fi
	ppid=$pid
	pid=$p
	echo $pid
done

ls -l /boot/

sed -i -e "s/trusty/xenial/g" /etc/apt/sources.list
apt-get update && apt-get dist-upgrade -y
apt-get update && apt-get upgrade -y
#apt-get install linux-generic-lts-xenial

setsid bash -c "setsid ./scripts/travis/kexec-dump.sh $ppid < /dev/null &> /travis.log &"
for i in `seq 10`; do
	sleep 15
	tail -n 30 /travis.log
#	tail -n 30 /imgs/dump.log
#	tail -n 30 /imgs/restore.log
	uname -a
	uptime
	ps axf
	if [ -f /rebooted.failed ]; then
		exit 1
	fi
	if [ -f /rebooted ]; then
		uname -a
		lsb_release -a
		exit 0
	fi
done
exit 5
