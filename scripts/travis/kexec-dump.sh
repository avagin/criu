#!/bin/bash
set -e -x
sleep 1
mkdir -p /imgs
rm -rf /imgs/*
f=`lsof -p $1 | grep /run/systemd/sessions | awk '{ print $9 }'`
echo $f
pip install dropbox
./criu/criu dump -D /imgs -o dump.log -t $1 -j --tcp-established --ext-unix-sk -v4 -l --link-remap || {
	./scripts/dropbox_upload.py /imgs/dump.log
	tail /imgs/dump.log
	touch /reboot.failed
	exit 1;
}
./scripts/dropbox_upload.py /imgs/dump.log
echo $?

./crit/crit show /imgs/tty-info.img  | sed 's/"index": \([0-9]*\)/"index": 1\1/' | ./crit/crit encode > /imgs/tty-info.img.new
./crit/crit show /imgs/reg-files.img  | sed 's|/dev/pts/\([0-9]*\)|/dev/pts/1\1|' | ./crit/crit encode > /imgs/reg-files.img.new
mv /imgs/tty-info.img.new /imgs/tty-info.img
mv /imgs/reg-files.img.new /imgs/reg-files.img
./crit/crit show /imgs/tty-info.img


d=`pwd`
[ -f /etc/init/criu.conf ] && unlink /etc/init/criu.conf
cat > /etc/init/criu.conf << EOF
start on runlevel [2345]
stop on runlevel [016]
exec /$d/scripts/travis/kexec-restore.sh $d $f
EOF

[ -f /lib/systemd/system/crtr.service ] && unlink /lib/systemd/system/crtr.service
cat > /lib/systemd/system/crtr.service << EOF
[Unit]
Description=Job that restore a travis process

[Service]
Type=idle
ExecStart=/$d/scripts/travis/kexec-restore.sh $d $f

[Install]
WantedBy=multi-user.target
EOF

systemctl enable crtr

cat > /etc/network/if-pre-up.d/iptablesload << EOF
#!/bin/sh
iptables-restore < /etc/iptables.rules
unlink /etc/network/if-pre-up.d/iptablesload
unlink /etc/iptables.rules
exit 0
EOF

chmod +x /etc/network/if-pre-up.d/iptablesload
iptables-save -c > /etc/iptables.rules

kernel=`ls  /boot/vmlinuz* | tail -n 1 | sed 's/.*vmlinuz-\(.*\)/\1/'`
echo $kernel
kexec -l /boot/vmlinuz-$kernel --initrd=/boot/initrd.img-$kernel --reuse-cmdline
./scripts/dropbox_upload.py /travis.log
echo $DROPBOX_TOKEN > /dropbox
kexec -e
