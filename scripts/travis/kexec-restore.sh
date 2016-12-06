#!/bin/bash

exec &> /travis.log

set -x
sleep 10
cd $1
mkfifo $2
chmod 0600 $2
./criu/criu restore -D /imgs -o restore.log -j --tcp-established --ext-unix-sk -v4 -l -d
echo $?
touch /rebooted
while :; do
sleep 1000
done
