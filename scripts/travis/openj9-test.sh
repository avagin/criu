#!/bin/bash

cd ../..
set -x

failures=""

docker build -t criu-openj9-ubuntu-test:latest -f scripts/build/Dockerfile.openj9-ubuntu .
docker run --rm --privileged criu-openj9-ubuntu-test:latest
if [ $? -ne 0 ]; then
	failures=`echo "$failures ubuntu"`
fi

docker build -t criu-openj9-alpine-test:latest -f scripts/build/Dockerfile.openj9-alpine .
docker run --rm --privileged --entrypoint /bin/bash criu-openj9-alpine-test:latest readelf -n /opt/java/openjdk/jre/lib/amd64/libnet.so
docker run --rm --privileged --entrypoint readelf criu-openj9-alpine-test:latest -a /opt/java/openjdk/jre/lib/amd64/libnet.so
docker run --rm --privileged -v /tmp/xxx:/tmp criu-openj9-alpine-test:latest
docker run --rm --privileged  --entrypoint /bin/bash criu-openj9-alpine-test:latest readelf -n /opt/java/openjdk/jre/lib/amd64/libnet.so
docker run --rm --privileged --entrypoint readelf criu-openj9-alpine-test:latest -a /opt/java/openjdk/jre/lib/amd64/libnet.so
if [ $? -ne 0 ]; then
	failures=`echo "$failures alpine"`
fi

if [ -n "$failures" ]; then
	cat /tmp/criu.log
	cat /tmp/xxx/criu.log
	echo "Tests failed on $failures"
	exit 1
fi
