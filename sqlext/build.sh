#!/bin/bash

sqlextension="ldext.so"
libarchive="ldext_fPIC.a"

if [ "$1" = "" ]; then
	cmd="buildext"
else
	cmd="$1"
fi

case "$cmd" in

amalg) cat dircursor.c exports_cursor.c
	cat init_header.c deploy.c loader.c exports.c exptbl.c
	cat readfile.c fstbl.c init_footer.c
	;;

buildext) $0 amalg | \
 gcc -Wall -fPIC -shared -O2 -o "$sqlextension" -x c - \
 -I/usr/include/lua5.2 \
 -llua5.2 -lcrypto
	;;

buildfpica)
	if [ -e "fpica.o" ]; then
		echo "fpica.o already exists!"
		exit 1
	fi
	$0 amalg | gcc -Wall -fPIC -O2 -DSQLITE_CORE -x c -c -o fpica.o - \
	 -I/usr/include/lua5.2
	ar rcs "$libarchive" fpica.o
	rm -f fpica.o
	;;

clean) rm -f "$sqlextension" "$libarchive"
	;;

esac
