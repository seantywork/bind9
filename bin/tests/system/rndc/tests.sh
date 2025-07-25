#!/bin/sh

# Copyright (C) Internet Systems Consortium, Inc. ("ISC")
#
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0.  If a copy of the MPL was not distributed with this
# file, you can obtain one at https://mozilla.org/MPL/2.0/.
#
# See the COPYRIGHT file distributed with this work for additional
# information regarding copyright ownership.

set -e

. ../conf.sh

DIGOPTS="+tcp +noadd +nosea +nostat +noquest +nocomm +nocmd"
DIGOPTS=""
DIGCMD="$DIG $DIGOPTS -p ${PORT}"
RNDCCMD="$RNDC -p ${CONTROLPORT} -c ../_common/rndc.conf -s"

status=0
n=0

n=$((n + 1))
echo_i "preparing ($n)"
ret=0
$NSUPDATE -p ${PORT} -k ns2/session.key >/dev/null 2>&1 <<END || ret=1
server 10.53.0.2
zone nil.
update add text1.nil. 600 IN TXT "addition 1"
send
zone other.
update add text1.other. 600 IN TXT "addition 1"
send
END
[ -s ns2/nil.db.jnl ] || {
  echo_i "'test -s ns2/nil.db.jnl' failed when it shouldn't have"
  ret=1
}
[ -s ns2/other.db.jnl ] || {
  echo_i "'test -s ns2/other.db.jnl' failed when it shouldn't have"
  ret=1
}
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

echo_i "rndc freeze"
$RNDCCMD 10.53.0.2 freeze | sed 's/^/ns2 /' | cat_i

n=$((n + 1))
echo_i "checking zone was dumped ($n)"
ret=0
for i in 1 2 3 4 5 6 7 8 9 10; do
  grep "addition 1" ns2/nil.db >/dev/null && break
  sleep 1
done
grep "addition 1" ns2/nil.db >/dev/null 2>&1 || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking journal file is still present ($n)"
ret=0
[ -s ns2/nil.db.jnl ] || {
  echo_i "'test -s ns2/nil.db.jnl' failed when it shouldn't have"
  ret=1
}
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking zone not writable ($n)"
ret=0
$NSUPDATE -p ${PORT} -k ns2/session.key >/dev/null 2>&1 <<END && ret=1
server 10.53.0.2
zone nil.
update add text2.nil. 600 IN TXT "addition 2"
send
END

$DIGCMD @10.53.0.2 text2.nil. TXT >dig.out.1.test$n || ret=1
grep 'addition 2' dig.out.1.test$n >/dev/null && ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

echo_i "rndc thaw"
$RNDCCMD 10.53.0.2 thaw | sed 's/^/ns2 /' | cat_i

wait_for_log 3 "zone_postload: zone nil/IN: done" ns2/named.run

n=$((n + 1))
echo_i "checking zone now writable ($n)"
ret=0
$NSUPDATE -p ${PORT} -k ns2/session.key >nsupdate.out.1.test$n 2>&1 <<END || ret=1
server 10.53.0.2
zone nil.
update add text3.nil. 600 IN TXT "addition 3"
send
END
$DIGCMD @10.53.0.2 text3.nil. TXT >dig.out.1.test$n || ret=1
grep 'addition 3' dig.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

echo_i "rndc sync"
ret=0
$RNDCCMD 10.53.0.2 sync nil | sed 's/^/ns2 /' | cat_i

n=$((n + 1))
echo_i "checking zone was dumped ($n)"
ret=0
for i in 1 2 3 4 5 6 7 8 9 10; do
  grep "addition 3" ns2/nil.db >/dev/null && break
  sleep 1
done
grep "addition 3" ns2/nil.db >/dev/null 2>&1 || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking journal file is still present ($n)"
ret=0
[ -s ns2/nil.db.jnl ] || {
  echo_i "'test -s ns2/nil.db.jnl' failed when it shouldn't have"
  ret=1
}
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking zone is still writable ($n)"
ret=0
$NSUPDATE -p ${PORT} -k ns2/session.key >nsupdate.out.1.test$n 2>&1 <<END || ret=1
server 10.53.0.2
zone nil.
update add text4.nil. 600 IN TXT "addition 4"
send
END

$DIGCMD @10.53.0.2 text4.nil. TXT >dig.out.1.test$n || ret=1
grep 'addition 4' dig.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

echo_i "rndc sync -clean"
ret=0
$RNDCCMD 10.53.0.2 sync -clean nil | sed 's/^/ns2 /' | cat_i

n=$((n + 1))
echo_i "checking zone was dumped ($n)"
ret=0
for i in 1 2 3 4 5 6 7 8 9 10; do
  grep "addition 4" ns2/nil.db >/dev/null && break
  sleep 1
done
grep "addition 4" ns2/nil.db >/dev/null 2>&1 || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking journal file is deleted ($n)"
ret=0
[ -s ns2/nil.db.jnl ] && {
  echo_i "'test -s ns2/nil.db.jnl' failed when it shouldn't have"
  ret=1
}
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking zone is still writable ($n)"
ret=0
$NSUPDATE -p ${PORT} -k ns2/session.key >/dev/null 2>&1 <<END || ret=1
server 10.53.0.2
zone nil.
update add text5.nil. 600 IN TXT "addition 5"
send
END

$DIGCMD @10.53.0.2 text4.nil. TXT >dig.out.1.test$n || ret=1
grep 'addition 4' dig.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking other journal files not removed ($n)"
ret=0
[ -s ns2/other.db.jnl ] || {
  echo_i "'test -s ns2/other.db.jnl' failed when it shouldn't have"
  ret=1
}
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

echo_i "cleaning all zones ($n)"
$RNDCCMD 10.53.0.2 sync -clean | sed 's/^/ns2 /' | cat_i

n=$((n + 1))
echo_i "checking all journals removed ($n)"
ret=0
[ -s ns2/nil.db.jnl ] && {
  echo_i "'test -s ns2/nil.db.jnl' succeeded when it shouldn't have"
  ret=1
}
[ -s ns2/other.db.jnl ] && {
  echo_i "'test -s ns2/other.db.jnl' succeeded when it shouldn't have"
  ret=1
}
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking that freezing static zones is not allowed ($n)"
ret=0
$RNDCCMD 10.53.0.2 freeze static >rndc.out.1.test$n 2>&1 && ret=1
grep 'not dynamic' rndc.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking that journal is removed when serial is changed before thaw ($n)"
ret=0
sleep 1
$NSUPDATE -p ${PORT} -k ns2/session.key >nsupdate.out.1.test$n 2>&1 <<END || ret=1
server 10.53.0.2
zone other.
update add text6.other. 600 IN TXT "addition 6"
send
END
[ -s ns2/other.db.jnl ] || {
  echo_i "'test -s ns2/other.db.jnl' failed when it shouldn't have"
  ret=1
}
$RNDCCMD 10.53.0.2 freeze other 2>&1 | sed 's/^/ns2 /' | cat_i
for i in 1 2 3 4 5 6 7 8 9 10; do
  grep "addition 6" ns2/other.db >/dev/null && break
  sleep 1
done
serial=$(awk '$3 ~ /serial/ {print $1}' ns2/other.db)
newserial=$((serial + 1))
sed s/$serial/$newserial/ ns2/other.db >ns2/other.db.new
echo 'frozen TXT "frozen addition"' >>ns2/other.db.new
mv -f ns2/other.db.new ns2/other.db
$RNDCCMD 10.53.0.2 thaw 2>&1 | sed 's/^/ns2 /' | cat_i
sleep 1
[ -f ns2/other.db.jnl ] && {
  echo_i "'test -f ns2/other.db.jnl' succeeded when it shouldn't have"
  ret=1
}
$NSUPDATE -p ${PORT} -k ns2/session.key >nsupdate.out.2.test$n 2>&1 <<END || ret=1
server 10.53.0.2
zone other.
update add text7.other. 600 IN TXT "addition 7"
send
END
$DIGCMD @10.53.0.2 text6.other. TXT >dig.out.1.test$n || ret=1
grep 'addition 6' dig.out.1.test$n >/dev/null || ret=1
$DIGCMD @10.53.0.2 text7.other. TXT >dig.out.2.test$n || ret=1
grep 'addition 7' dig.out.2.test$n >/dev/null || ret=1
$DIGCMD @10.53.0.2 frozen.other. TXT >dig.out.3.test$n || ret=1
grep 'frozen addition' dig.out.3.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking that journal is kept when ixfr-from-differences is in use ($n)"
ret=0
$NSUPDATE -p ${PORT} -k ns2/session.key >nsupdate.out.1.test$n 2>&1 <<END || ret=1
server 10.53.0.2
zone nil.
update add text6.nil. 600 IN TXT "addition 6"
send
END
[ -s ns2/nil.db.jnl ] || {
  echo_i "'test -s ns2/nil.db.jnl' failed when it shouldn't have"
  ret=1
}
$RNDCCMD 10.53.0.2 freeze nil 2>&1 | sed 's/^/ns2 /' | cat_i
for i in 1 2 3 4 5 6 7 8 9 10; do
  grep "addition 6" ns2/nil.db >/dev/null && break
  sleep 1
done
serial=$(awk '$3 ~ /serial/ {print $1}' ns2/nil.db)
newserial=$((serial + 1))
sed s/$serial/$newserial/ ns2/nil.db >ns2/nil.db.new
echo 'frozen TXT "frozen addition"' >>ns2/nil.db.new
mv -f ns2/nil.db.new ns2/nil.db
$RNDCCMD 10.53.0.2 thaw 2>&1 | sed 's/^/ns2 /' | cat_i
sleep 1
[ -s ns2/nil.db.jnl ] || {
  echo_i "'test -s ns2/nil.db.jnl' failed when it shouldn't have"
  ret=1
}
$NSUPDATE -p ${PORT} -k ns2/session.key >nsupdate.out.2.test$n 2>&1 <<END || ret=1
server 10.53.0.2
zone nil.
update add text7.nil. 600 IN TXT "addition 7"
send
END
$DIGCMD @10.53.0.2 text6.nil. TXT >dig.out.1.test$n || ret=1
grep 'addition 6' dig.out.1.test$n >/dev/null || ret=1
$DIGCMD @10.53.0.2 text7.nil. TXT >dig.out.2.test$n || ret=1
grep 'addition 7' dig.out.2.test$n >/dev/null || ret=1
$DIGCMD @10.53.0.2 frozen.nil. TXT >dig.out.3.test$n || ret=1
grep 'frozen addition' dig.out.3.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

# temp test
echo_i "dumping stats ($n)"
$RNDCCMD 10.53.0.2 stats
n=$((n + 1))
echo_i "verifying adb records in named.stats ($n)"
grep "ADB stats" ns2/named.stats >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "test using second key ($n)"
ret=0
$RNDC -s 10.53.0.2 -p ${CONTROLPORT} -c ns2/secondkey.conf status >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "test 'rndc dumpdb' with an unwritable dump-file ($n)"
ret=0
touch ns2/named_dump.db
chmod -w ns2/named_dump.db
rndc_dumpdb ns2 2>/dev/null && ret=1
grep -F "failed: permission denied" "rndc.out.test$n" >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "test 'rndc dumpdb' on a empty cache ($n)"
ret=0
rndc_dumpdb ns3 || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "test 'rndc reload' on a zone with include files ($n)"
ret=0
grep "incl/IN: skipping load" ns2/named.run >/dev/null && ret=1
loads=$(grep "incl/IN: starting load" ns2/named.run | wc -l)
[ "$loads" -eq 1 ] || ret=1
$RNDCCMD 10.53.0.2 reload >/dev/null || ret=1
for i in 1 2 3 4 5 6 7 8 9; do
  tmp=0
  grep "incl/IN: skipping load" ns2/named.run >/dev/null || tmp=1
  [ $tmp -eq 0 ] && break
  sleep 1
done
[ $tmp -eq 1 ] && ret=1
touch ns2/static.db
$RNDCCMD 10.53.0.2 reload >/dev/null || ret=1
for i in 1 2 3 4 5 6 7 8 9; do
  tmp=0
  loads=$(grep "incl/IN: starting load" ns2/named.run | wc -l)
  [ "$loads" -eq 2 ] || tmp=1
  [ $tmp -eq 0 ] && break
  sleep 1
done
[ $tmp -eq 1 ] && ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
if $FEATURETEST --md5; then
  echo_i "testing rndc with hmac-md5 ($n)"
  ret=0
  $RNDC -s 10.53.0.4 -p ${EXTRAPORT1} -c ns4/key1.conf status >/dev/null 2>&1 || ret=1
  for i in 2 3 4 5 6; do
    $RNDC -s 10.53.0.4 -p ${EXTRAPORT1} -c ns4/key${i}.conf status >/dev/null 2>&1 && ret=1
  done
  if [ $ret != 0 ]; then echo_i "failed"; fi
  status=$((status + ret))
else
  echo_i "skipping rndc with hmac-md5 ($n)"
fi

n=$((n + 1))
echo_i "testing rndc with hmac-sha1 ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT2} -c ns4/key2.conf status >/dev/null 2>&1 || ret=1
for i in 1 3 4 5 6; do
  $RNDC -s 10.53.0.4 -p ${EXTRAPORT2} -c ns4/key${i}.conf status >/dev/null 2>&1 && ret=1
done
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing rndc with hmac-sha224 ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT3} -c ns4/key3.conf status >/dev/null 2>&1 || ret=1
for i in 1 2 4 5 6; do
  $RNDC -s 10.53.0.4 -p ${EXTRAPORT3} -c ns4/key${i}.conf status >/dev/null 2>&1 && ret=1
done
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing rndc with hmac-sha256 ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT4} -c ns4/key4.conf status >/dev/null 2>&1 || ret=1
for i in 1 2 3 5 6; do
  $RNDC -s 10.53.0.4 -p ${EXTRAPORT4} -c ns4/key${i}.conf status >/dev/null 2>&1 && ret=1
done
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing rndc with hmac-sha384 ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT5} -c ns4/key5.conf status >/dev/null 2>&1 || ret=1
for i in 1 2 3 4 6; do
  $RNDC -s 10.53.0.4 -p ${EXTRAPORT5} -c ns4/key${i}.conf status >/dev/null 2>&1 && ret=1
done
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing rndc with hmac-sha512 ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf status >/dev/null 2>&1 || ret=1
for i in 1 2 3 4 5; do
  $RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key${i}.conf status >/dev/null 2>&1 2>&1 && ret=1
done
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing single control channel with multiple algorithms ($n)"
ret=0
for i in 1 2 3 4 5 6; do
  test $i = 1 && $FEATURETEST --have-fips-mode && continue
  $RNDC -s 10.53.0.4 -p ${EXTRAPORT7} -c ns4/key${i}.conf status >/dev/null 2>&1 || ret=1
done
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing automatic zones are reported ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf status >rndc.out.1.test$n || ret=1
grep "number of zones: 201 (200 automatic)" rndc.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing rndc with null command ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing rndc with unknown control channel command ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf obviouslynotacommand >/dev/null 2>&1 && ret=1
# rndc: 'obviouslynotacommand' failed: unknown command
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing rndc with querylog command ($n)"
ret=0
# first enable it with querylog on option
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf querylog on >/dev/null 2>&1 || ret=1
grep "query logging is now on" ns4/named.run >/dev/null || ret=1
# query for builtin and check if query was logged (without +subnet)
$DIG @10.53.0.4 -p ${PORT} -c ch -t txt foo12345.bind +qr >dig.out.1.test$n 2>&1 || ret=1
grep "query: foo12345.bind CH TXT.*(.*)$" ns4/named.run >/dev/null || ret=1
# query for another builtin zone and check if query was logged (with +subnet=127.0.0.1)
$DIG +subnet=127.0.0.1 @10.53.0.4 -p ${PORT} -c ch -t txt foo12346.bind +qr >dig.out.2.test$n 2>&1 || ret=1
grep "query: foo12346.bind CH TXT.*\[ECS 127\.0\.0\.1/32/0]" ns4/named.run >/dev/null || ret=1
# query for another builtin zone and check if query was logged (with +subnet=127.0.0.1/24)
$DIG +subnet=127.0.0.1/24 @10.53.0.4 -p ${PORT} -c ch -t txt foo12347.bind +qr >dig.out.3.test$n 2>&1 || ret=1
grep "query: foo12347.bind CH TXT.*\[ECS 127\.0\.0\.0/24/0]" ns4/named.run >/dev/null || ret=1
# query for another builtin zone and check if query was logged (with +subnet=::1)
$DIG +subnet=::1 @10.53.0.4 -p ${PORT} -c ch -t txt foo12348.bind +qr >dig.out.4.test$n 2>&1 || ret=1
grep "query: foo12348.bind CH TXT.*\[ECS ::1/128/0]" ns4/named.run >/dev/null || ret=1
# toggle query logging and check again
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf querylog >/dev/null 2>&1 || ret=1
grep "query logging is now off" ns4/named.run >/dev/null || ret=1
# query for another builtin zone and check if query was logged (without +subnet)
$DIG @10.53.0.4 -p ${PORT} -c ch -t txt foo9876.bind +qr >dig.out.5.test$n 2>&1 || ret=1
grep "query: foo9876.bind CH TXT.*(.*)$" ns4/named.run >/dev/null && ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

RNDCCMD4="$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf"
n=$((n + 1))
echo_i "testing rndc nta time limits ($n)"
ret=0
$RNDCCMD4 nta -l 2h nta1.example >rndc.out.1.test$n 2>&1
grep "Negative trust anchor added" rndc.out.1.test$n >/dev/null || ret=1
$RNDCCMD4 nta -l 1d nta2.example >rndc.out.2.test$n 2>&1
grep "Negative trust anchor added" rndc.out.2.test$n >/dev/null || ret=1
$RNDCCMD4 nta -l 1w nta3.example >rndc.out.3.test$n 2>&1
grep "Negative trust anchor added" rndc.out.3.test$n >/dev/null || ret=1
$RNDCCMD4 nta -l 8d nta4.example >rndc.out.4.test$n 2>&1 && ret=1
grep "NTA lifetime cannot exceed one week" rndc.out.4.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing rndc nta -class option ($n)"
ret=0
nextpart ns4/named.run >/dev/null
$RNDCCMD4 nta -c in nta1.example >rndc.out.1.test$n 2>&1
nextpart ns4/named.run | grep "added NTA 'nta1.example'" >/dev/null || ret=1
$RNDCCMD4 nta -c any nta1.example >rndc.out.2.test$n 2>&1
nextpart ns4/named.run | grep "added NTA 'nta1.example'" >/dev/null || ret=1
$RNDCCMD4 nta -c ch nta1.example >rndc.out.3.test$n 2>&1
nextpart ns4/named.run | grep "added NTA 'nta1.example'" >/dev/null && ret=1
$RNDCCMD4 nta -c fake nta1.example >rndc.out.4.test$n 2>&1 && ret=1
nextpart ns4/named.run | grep "added NTA 'nta1.example'" >/dev/null && ret=1
grep 'unknown class' rndc.out.4.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

for i in 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288; do
  n=$((n + 1))
  echo_i "testing rndc buffer size limits (size=${i}) ($n)"
  ret=0
  $RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf testgen ${i} 2>&1 >rndc.out.$i.test$n || ret=1
  {
    actual_size=$($GENCHECK rndc.out.$i.test$n)
    rc=$?
  } || true
  if [ "$rc" = "0" ]; then
    expected_size=$((i + 1))
    if [ $actual_size != $expected_size ]; then ret=1; fi
  else
    ret=1
  fi

  if [ $ret != 0 ]; then echo_i "failed"; fi
  status=$((status + ret))
done

n=$((n + 1))
echo_i "testing rndc -r (show result) ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf -r testgen 0 2>&1 >rndc.out.1.test$n || ret=1
grep "ISC_R_SUCCESS 0" rndc.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "testing rndc with a token containing a space ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf -r flush '"view with a space"' 2>&1 >rndc.out.1.test$n || ret=1
grep "not found" rndc.out.1.test$n >/dev/null && ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "test 'rndc reconfig' with a broken config ($n)"
ret=0
nextpart ns4/named.run >/dev/null
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf reconfig >/dev/null || ret=1
wait_for_log 3 "running" ns4/named.run
mv ns4/named.conf ns4/named.conf.save
echo "error error error" >>ns4/named.conf
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf reconfig >rndc.out.1.test$n 2>&1 && ret=1
grep "rndc: 'reconfig' failed: unexpected token" rndc.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check rndc status reports failure ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf status >rndc.out.1.test$n 2>&1 || ret=1
grep "reload/reconfig failed" rndc.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "restore working config ($n)"
ret=0
nextpart ns4/named.run >/dev/null
mv ns4/named.conf.save ns4/named.conf
sleep 1
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf reconfig >/dev/null || ret=1
wait_for_log 3 "running" ns4/named.run
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check 'rndc status' 'reload/reconfig failure' is cleared after successful reload/reconfig ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf status >rndc.out.1.test$n 2>&1 || ret=1
grep "reload/reconfig failed" rndc.out.1.test$n >/dev/null && ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "test read-only control channel access ($n)"
ret=0
$RNDCCMD 10.53.0.5 status >rndc.out.1.test$n 2>&1 || ret=1
$RNDCCMD 10.53.0.5 nta -dump >rndc.out.2.test$n 2>&1 || ret=1
$RNDCCMD 10.53.0.5 reconfig >rndc.out.3.test$n 2>&1 && ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "test rndc status shows running on ($n)"
ret=0
$RNDCCMD 10.53.0.5 status >rndc.out.1.test$n 2>&1 || ret=1
grep "^running on " rndc.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "test 'rndc reconfig' with loading of a large zone ($n)"
ret=0
nextpart ns6/named.run >/dev/null
cp ns6/named.conf ns6/named.conf.save
echo "zone \"huge.zone\" { type primary; file \"huge.zone.db\"; };" >>ns6/named.conf
echo_i "reloading config"
$RNDCCMD 10.53.0.6 reconfig >rndc.out.1.test$n 2>&1 || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))
sleep 1

n=$((n + 1))
echo_i "check if zone load was scheduled ($n)"
wait_for_log_peek 20 "scheduled loading new zones" ns6/named.run || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check if query for the zone returns SERVFAIL ($n)"
$DIG @10.53.0.6 -p ${PORT} -t soa huge.zone >dig.out.1.test$n || ret=1
grep "SERVFAIL" dig.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then
  echo_i "failed (ignored)"
  ret=0
fi
status=$((status + ret))

n=$((n + 1))
echo_i "wait for the zones to be loaded ($n)"
wait_for_log_peek 60 "huge.zone/IN: loaded serial" ns6/named.run || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check if query for the zone returns NOERROR ($n)"
$DIG @10.53.0.6 -p ${PORT} -t soa huge.zone >dig.out.1.test$n || ret=1
grep "NOERROR" dig.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "verify that the full command is logged ($n)"
ret=0
$RNDCCMD 10.53.0.2 null with extra arguments >/dev/null 2>&1
grep "received control channel command 'null with extra arguments'" ns2/named.run >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

mv ns6/named.conf.save ns6/named.conf
sleep 1
$RNDCCMD 10.53.0.6 reconfig >/dev/null || ret=1
sleep 1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check 'rndc \"\"' is handled ($n)"
ret=0
$RNDCCMD 10.53.0.2 "" >rndc.out.1.test$n 2>&1 && ret=1
grep "rndc: '' failed: failure" rndc.out.1.test$n >/dev/null
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check rndc -4 -6 ($n)"
ret=0
$RNDCCMD 10.53.0.2 -4 -6 status >rndc.out.1.test$n 2>&1 && ret=1
grep "only one of -4 and -6 allowed" rndc.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check rndc -4 with an IPv6 server address ($n)"
ret=0
$RNDCCMD fd92:7065:b8e:ffff::2 -4 status >rndc.out.1.test$n 2>&1 && ret=1
grep "address family not supported" rndc.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check rndc nta reports adding to multiple views ($n)"
ret=0
$RNDCCMD 10.53.0.3 nta test.com >rndc.out.test$n 2>&1 || ret=1
lines=$(cat rndc.out.test$n | wc -l)
[ ${lines:-0} -eq 2 ] || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check 'rndc retransfer' of primary error message ($n)"
ret=0
$RNDCCMD 10.53.0.2 retransfer nil >rndc.out.test$n 2>&1 && ret=1
grep "rndc: 'retransfer' failed: failure" rndc.out.test$n >/dev/null || ret=1
grep "retransfer: inappropriate zone type: primary" rndc.out.test$n >/dev/null || ret=1
lines=$(cat rndc.out.test$n | wc -l)
[ ${lines:-0} -eq 2 ] || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check 'rndc freeze' with in-view zones works ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf freeze >rndc.out.test$n 2>&1 || ret=1
test -s rndc.out.test$n && sed 's/^/ns2 /' rndc.out.test$n | cat_i
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking non in-view zone instance is not writable ($n)"
ret=0
$NSUPDATE -p ${PORT} >/dev/null 2>&1 <<END && ret=1
server 10.53.0.4
zone example.
update add text2.example. 600 IN TXT "addition 3"
send
END
$DIGCMD @10.53.0.4 -p ${PORT} text2.example. TXT >dig.out.1.test$n || ret=1
grep 'addition 3' dig.out.1.test$n >/dev/null && ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check 'rndc thaw' with in-view zones works ($n)"
ret=0
$RNDC -s 10.53.0.4 -p ${EXTRAPORT6} -c ns4/key6.conf thaw >rndc.out.test$n 2>&1 || ret=1
test -s rndc.out.test$n && sed 's/^/ns2 /' rndc.out.test$n | cat_i
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking non in-view zone instance is now writable ($n)"
ret=0
$NSUPDATE -p ${PORT} >nsupdate.out.test$n 2>&1 <<END || ret=1
server 10.53.0.4
zone example.
update add text2.example. 600 IN TXT "addition 3"
send
END
$DIGCMD @10.53.0.4 -p ${PORT} text2.example. TXT >dig.out.1.test$n || ret=1
grep 'addition 3' dig.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "checking initial in-view zone file is loaded ($n)"
ret=0
TSIG="$DEFAULT_HMAC:int:FrSt77yPTFx6hTs4i2tKLB9LmE0="
$DIGCMD @10.53.0.7 -y "$TSIG" text1.test. TXT >dig.out.1.test$n || ret=1
grep 'include 1' dig.out.1.test$n >/dev/null || ret=1
TSIG="$DEFAULT_HMAC:ext:FrSt77yPTFx6hTs4i2tKLB9LmE0="
$DIGCMD @10.53.0.7 -y "$TSIG" text1.test. TXT >dig.out.2.test$n || ret=1
grep 'include 1' dig.out.2.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

echo_i "update in-view zone ($n)"
ret=0
TSIG="$DEFAULT_HMAC:int:FrSt77yPTFx6hTs4i2tKLB9LmE0="
$NSUPDATE -p ${PORT} -y "$TSIG" >/dev/null 2>&1 <<END || ret=1
server 10.53.0.7
zone test.
update add text2.test. 600 IN TXT "addition 1"
send
END
[ -s ns7/test.db.jnl ] || {
  echo_i "'test -s ns7/test.db.jnl' failed when it shouldn't have"
  ret=1
}
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

echo_i "checking update ($n)"
ret=0
TSIG="$DEFAULT_HMAC:int:FrSt77yPTFx6hTs4i2tKLB9LmE0="
$DIGCMD @10.53.0.7 -y "$TSIG" text2.test. TXT >dig.out.1.test$n || ret=1
grep 'addition 1' dig.out.1.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

nextpart ns7/named.run >/dev/null

echo_i "rndc freeze"
$RNDCCMD 10.53.0.7 freeze | sed 's/^/ns7 /' | cat_i | cat_i

wait_for_log 3 "dump_done: zone test/IN/internal: enter" ns7/named.run

echo_i "edit zone files"
cp ns7/test.db.in ns7/test.db
cp ns7/include2.db.in ns7/include.db

echo_i "rndc thaw"
$RNDCCMD 10.53.0.7 thaw | sed 's/^/ns7 /' | cat_i

wait_for_log 3 "zone_postload: zone test/IN/internal: done" ns7/named.run

echo_i "rndc reload"
$RNDCCMD 10.53.0.7 reload | sed 's/^/ns7 /' | cat_i

wait_for_log 3 "all zones loaded" ns7/named.run

n=$((n + 1))
echo_i "checking zone file edits are loaded ($n)"
ret=0
TSIG="$DEFAULT_HMAC:int:FrSt77yPTFx6hTs4i2tKLB9LmE0="
$DIGCMD @10.53.0.7 -y "$TSIG" text1.test. TXT >dig.out.1.test$n || ret=1
grep 'include 2' dig.out.1.test$n >/dev/null || ret=1
TSIG="$DEFAULT_HMAC:ext:FrSt77yPTFx6hTs4i2tKLB9LmE0="
$DIGCMD @10.53.0.7 -y "$TSIG" text1.test. TXT >dig.out.2.test$n || ret=1
grep 'include 2' dig.out.2.test$n >/dev/null || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

n=$((n + 1))
echo_i "check rndc nta -dump reports NTAs in multiple views ($n)"
ret=0
$RNDCCMD 10.53.0.3 nta -dump >rndc.out.test$n 2>&1 || ret=1
lines=$(cat rndc.out.test$n | wc -l)
[ ${lines:-0} -eq 2 ] || ret=1
if [ $ret != 0 ]; then echo_i "failed"; fi
status=$((status + ret))

echo_i "exit status: $status"
[ $status -eq 0 ] || exit 1
