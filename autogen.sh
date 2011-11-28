#!/bin/sh
# you can either set the environment variables AUTOCONF, AUTOHEADER, AUTOMAKE,
# ACLOCAL, AUTOPOINT and/or LIBTOOLIZE to the right versions, or leave them
# unset and get the defaults

mkdir -p build-aux || {
 echo 'failed creating auxiliary build files directory';
 exit 1;
}

autoreconf -vfi || {
 echo 'autogen.sh failed';
 exit 1;
}
