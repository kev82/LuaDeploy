#!/bin/sh

if [ "$1" = "" ]; then
	cmd="build"
else
	cmd="$1"
fi

case "$cmd" in

clientamalg) cat msg.h client.c
	;;

amalg) cat msg.h server.c client.c db.c state.c
	echo "static char luadeploy_code[] = {"
	cat ldcode.lua | luac -o - - | xxd -i
	echo "};"
	cat module.c
	;;

build) rm -f module.o luadeploy.so
	$0 amalg | gcc -D_GNU_SOURCE -Wall -fPIC -O2 -x c -c -o module.o - \
	 -I/usr/include/lua5.2
	gcc -fPIC -shared -O2 -o luadeploy.so module.o ../sqlext/ldext_fPIC.a \
	 -lpthread -lrt -lsqlite3 -lcrypto
	rm module.o
	;;

clean) rm -f luadeploy.so
	;;

esac
