#!/bin/bash

# This test creates a process in non-host pidns and then dumps it and restores
# it into host pidns. We use pid >100000 in non-host pidns to make sure it does
# not intersect with some host pid on restore but it is potentially racy so
# please run this test only in manualy.

trap "cleanup" QUIT TERM INT HUP EXIT

function cleanup()
{
	PIDS=$(pgrep -x -f "sh _run_pidns.sh")
	for PID in $PIDS; do
		kill -9 $PID
	done

	sleep 0.5
	umount -lf pidns_proc || :
	rm -f pidns_proc || :
}

CRIU=../../../criu/criu

unshare -p -f sh -c 'mkdir pidns_proc;
mount -t proc proc pidns_proc;
echo 100000 > pidns_proc/sys/kernel/ns_last_pid;
umount -l pidns_proc;
rmdir pidns_proc;
setsid sh _run_pidns.sh &>/dev/null </dev/null' &>/dev/null </dev/null &

while :; do
	PID=$(pgrep -x -f "sh _run_pidns.sh" -n)
	if [ -n "$PID" ]; then
		break
	fi
	sleep 0.1
done

PIDNS=$(readlink /proc/$PID/ns/pid | sed 's/://')

exec 100<&-
exec 100< /proc/$$/ns/pid

BEFORE=$(grep NSpid /proc/$PID/status)
echo "before c/r: $BEFORE"

rm -rf images_pidns || :
mkdir -p images_pidns

$CRIU dump -v4 -o dump.log -t $PID -D images_pidns --external $PIDNS:exti
RESULT=$?
cat images_pidns/dump.log | grep -B 5 Error || echo ok
[ "$RESULT" != "0" ] && {
	echo "CRIU dump failed"
	echo FAIL
	exit 1
}

$CRIU restore -v4 -o restore.log -D images_pidns --restore-detached --inherit-fd fd[100]:exti
RESULT=$?
cat images_pidns/dump.log | grep -B 5 Error || echo ok
[ "$RESULT" != "0" ] && {
	echo "CRIU restore failed"
	echo FAIL
	exit 1
}

PID=$(pgrep -x -f "sh _run_pidns.sh" -n)
AFTER=$(grep NSpid /proc/$PID/status)
echo "after c/r: $AFTER"
echo PASS
exit 0
