# Readme for Rdbparser

1. [Why does Rdbparser exist?](#why-does-rdbparser-exist-)
2. [What is Rdbparser?](#what-is-rdbparser-)
4. [Build or Install on Fedora / CentOS](#build-or-install-on-fedora--centos)
5. [Build or Install on Ubuntu / Debian](#build-or-install-on-ubuntu--debian)

## Why does Rdbparser exist?

I needed a C++ library for parsing and constructiong
[Redis](https://github.com/antirez/redis) Rdb files.

There are a couple of other tools for doing this, the ones I have found are:

1. [redis-rdb-tools](https://github.com/sripathikrishnan/redis-rdb-tools)

2. [redis-rdb-cli](https://github.com/leonchen83/redis-rdb-cli)

These are wonderful, but they are not something I can use in a program.

I created a document that details my understanding of the stucture of Rdb,
[Redis Rdb Dump Structures](rdb.adoc).

## What is Rdbparser?

Rdbparser is a C++ library and command line tool for parsing, displaying,
loading and constructing [Redis](https://github.com/antirez/redis) Rdb files.

Here are some usage examples:

1. Display the contents of a Rdb database dump in json format:
```console
$ redis-cli xadd stream '*' loc mel temp 23
"1582154892743-0"
$ redis-cli xadd stream '*' loc sfo temp 10
"1582154901501-0"
$ redis-cli save
OK
$ sudo rdbp -f /var/lib/redis/dump.rdb -e stream
{
"stream" : {
  "entries" : [
    { "id" : "1582154892743-0", "loc" : "mel", "temp" : 23 },
    { "id" : "1582154901501-0", "loc" : "sfo", "temp" : 10 } ],
  "last_id" : "1582154901501-0",
  "num_elems" : 2,
  "num_cgroups" : 0
}
}
```

2. Display the contents of a dump command, from redis-cli:
```console
$ redis-cli hset hash aaa 10 bbb 20 ccc 30 hello world
(integer) 4
$ redis-cli dump hash | rdbp
{
"hash" : {
  "aaa" : 10,
  "bbb" : 20,
  "ccc" : 30,
  "hello" : "world"
}
}
```

3. List the keys in a saved rdb.
```console
$ redis-cli save
OK
$ sudo rdbp -f /var/lib/redis/dump.rdb -l
stream
hash
```

4. Use restore command to insert dumped data into a running instance:
```console
#
# oops, deleted my keys accidentally
#
$ redis-cli del stream hash
(integer) 2
#
# restore keys from saved rdb
#
$ sudo rdbp -f /var/lib/redis/dump.rdb -r | redis-cli --pipe
All data transferred. Waiting for the last reply...
Last reply received from server.
errors: 0, replies: 2
#
# check that my keys are alive again
#
$ redis-cli dump stream | rdbp
{
"stream" : {
  "entries" : [
    { "id" : "1582155066471-0", "loc" : "mel", "temp" : 23 },
    { "id" : "1582155072341-0", "loc" : "sfo", "temp" : 10 } ],
  "last_id" : "1582155072341-0",
  "num_elems" : 2,
  "num_cgroups" : 0
}
}
$ redis-cli dump hash | rdbp
{
"hash" : {
  "aaa" : 10,
  "bbb" : 20,
  "ccc" : 30,
  "hello" : "world"
}
}
```

## Build or Install on Fedora / CentOS

```console
#
# Install packages needed to build and install it
#
$ sudo dnf install make gcc-c++ chrpath pcre2-devel liblzf-devel rpm-build
#
# Get the source
#
$ git clone https://www.github.com/injinj/rdbparser
$ cd rdbparser
#
# Build stuff, rdbp is usable without installing
#
$ make
$ ./FC30_x86_64/bin/rdbp -h
./FC30_x86_64/bin/rdbp [-e pat] [-v] [-i] [-f file]
   -e pat  : match key with glob pattern
   -v      : invert key match
   -i      : ignore key match case
   -f file : dump rdb file to read
   -m      : show meta data in json output
   -l      : list keys which match
   -r      : write restore commands for | redis-cli --pipe
default is to print json of matching data
if no file is given, will read data from stdin
#
# Install the rpm, which has bins, libs, include
#
$ make dist_rpm
$ sudo rpm -i rpmbuild/RPMS/x86_64/rdbparser-1.0.0-1.fc30.x86_64.rpm
#
# Show where the files are installed
#
$ rpmquery -ql rdbparser
/usr/bin/rdbp
/usr/include/rdbparser
/usr/include/rdbparser/glob_cvt.h
/usr/include/rdbparser/rdb_decode.h
/usr/include/rdbparser/rdb_json.h
/usr/include/rdbparser/rdb_restore.h
/usr/lib/.build-id
/usr/lib/.build-id/1f
/usr/lib/.build-id/1f/e8db66a884214828ad922a79cf818313ba9cff
/usr/lib/.build-id/4c
/usr/lib/.build-id/4c/f91176f9babf09dc3713f0405c29c1b430e2a3
/usr/lib64/librdbparser.a
/usr/lib64/librdbparser.so
/usr/lib64/librdbparser.so.1.0
/usr/lib64/librdbparser.so.1.0.0-1
#
# Uninstall / erase rdbparser files
#
$ sudo rpm -e rdbparser
```

## Build or Install on Ubuntu / Debian

Todo. liblzf-dev is not a package in Debian.

