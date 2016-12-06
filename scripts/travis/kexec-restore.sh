#!/bin/bash

exec &>> /travis.log
set -x -m
DROPBOX_TOKEN=`cat /dropbox`
export DROPBOX_TOKEN
sleep 10
cd $1
mkfifo $2
chmod 0600 $2
unshare -pfm --mount-proc --propagation=private ./criu/criu restore -D /imgs -o restore.log -j --tcp-established --ext-unix-sk -v4 -l --link-remap &
sleep 5
./scripts/dropbox_upload.py /travis.log
./scripts/dropbox_upload.py /imgs/restore.log
touch /rebooted
wait -n
