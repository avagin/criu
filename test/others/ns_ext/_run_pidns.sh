#!/bin/bash

set -e

status_fd=$1
mkdir -p pidns_proc
mount -t proc proc pidns_proc;
echo 100000 > pidns_proc/sys/kernel/ns_last_pid;
umount -l pidns_proc;
rmdir pidns_proc;

exec {pipe}<> <(:)
exec {pipe_r}</proc/self/fd/$pipe
exec {pipe_w}>/proc/self/fd/$pipe
exec {pipe}>&-

setsid bash __run_pidns.sh $status_fd &
exec {pipe_w}>&-
exec {status_fd}>&-

cat <&$pipe_r
echo "Exiting..."
