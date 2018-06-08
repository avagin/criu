#!/bin/bash
set -x

DROPBOX_UPLOAD=`pwd`/scripts/dropbox_upload.py

docker build  -t criu-kernel -f scripts/build/Dockerfile.kernel . || exit 1

if [ "$1" = 'prep' ]; then
	if [ -n "$KGIT" ]; then
		time git clone --depth 1 $KGIT linux
		KPATH=linux
	fi
	modprobe tun
	modprobe macvlan
	modprobe veth
	modprobe sit

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

	# zdtm/static/socket-tcp-reseted	
	sed -i "s/.*CONFIG_NF_REJECT_IPV4.*/CONFIG_NF_REJECT_IPV4=y/" .config
	sed -i "s/.*CONFIG_IP_NF_TARGET_REJECT.*/CONFIG_IP_NF_TARGET_REJECT=y/" .config
	echo "CONFIG_IP_NF_TARGET_REJECT=y" >> .config

	if [ "$KASAN" = "1" ]; then
        	sed -i "s/.*CONFIG_KASAN.*/CONFIG_KASAN=y\nCONFIG_KASAN_INLINE=y/" .config
		for i in CONFIG_LOCKDEP CONFIG_DEBUG_VM CONFIG_DEBUG_LIST CONFIG_DEBUG_ATOMIC_SLEEP CONFIG_PROVE_LOCKING; do
			sed -i "s/.*$i.*/# $i is not set/" .config
		done
	fi
	docker run -v `pwd`:/mnt/kernel -v ~/.ccache:/mnt/ccache -w /mnt/kernel criu-kernel make olddefconfig || exit 1
	cat .config
	docker run -v `pwd`:/mnt/kernel -v ~/.ccache:/mnt/ccache -w /mnt/kernel criu-kernel ccache -s
	docker run -v `pwd`:/mnt/kernel -v ~/.ccache:/mnt/ccache -w /mnt/kernel criu-kernel ccache -z
	$DROPBOX_UPLOAD .config || true
	while :; do sleep 60; ps axf --width 256; done &
	ps_pid=$1
	time docker run -v `pwd`:/mnt/kernel -v ~/.ccache:/mnt/ccache -w /mnt/kernel criu-kernel make -j 4 || exit 1
	kill $ps_pid
#	$DROPBOX_UPLOAD vmlinux || true
	make kernelrelease
	kernelrelease=$(make -s --no-print-directory kernelrelease)
	echo -- $kernelrelease
	docker run -v `pwd`:/mnt/kernel -v ~/.ccache:/mnt/ccache -w /mnt/kernel criu-kernel ccache -s
	cd $old_pwd
}

if [ -n "$COMPILE_ONLY" ]; then
	exit 0
fi

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
