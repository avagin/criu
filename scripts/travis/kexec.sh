#!/bin/bash
set -x

docker build  -t criu-kernel -f scripts/build/Dockerfile.kernel . || exit 1

if [ "$1" = 'prep' ]; then
	if [ -n "$KGIT" ]; then
		time git clone --depth 1 $KGIT linux
		KPATH=linux
	fi
	modprobe tun
	modprobe macvlan
	modprobe veth

	cp scripts/linux-next-config $KPATH/.config
	cd $KPATH
	make olddefconfig
	exit 0
fi

if [ -z "$KPATH" ]; then
	export KPATH=linux
fi

uname -a
cat /proc/cpuinfo
ip a
ip r

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

kernelrelease=""
true && {
	old_pwd=`pwd`
	cd $KPATH
	yes "" | make localyesconfig
        sed -i "s/.*CONFIG_KASAN.*/CONFIG_KASAN=y/" .config
	docker run -v `pwd`:/mnt -w /mnt criu-kernel make olddefconfig || exit 1
	docker run -v `pwd`:/mnt -w /mnt criu-kernel make -j 4 || exit 1
	make kernelrelease
	kernelrelease=$(make -s --no-print-directory kernelrelease)
	echo -- $kernelrelease
	ccache -s
	cd $old_pwd
}

# Disable Docker daemon start after reboot; upstart way
echo manual > /etc/init/docker.override

setsid bash -c "setsid ./scripts/travis/kexec-dump.sh $ppid < /dev/null &> /travis.log &"
for i in `seq 10`; do
	sleep 15
	cat /travis.log
#	tail -n 30 /imgs/dump.log
#	tail -n 30 /imgs/restore.log
	uname -a
	uptime
	ps axf
	if [ -f /rebooted ]; then
		uname -a
		[ "$kernelrelease" == "`uname -r`" ] || exit 1
		exit 0;
	fi
	if [ -f /reboot.failed ]; then
		uname -a
		exit 1;
	fi
done

exit 1
