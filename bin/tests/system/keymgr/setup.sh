#!/bin/sh
#
# Copyright (C) 2016  Internet Systems Consortium, Inc. ("ISC")
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
# REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
# INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
# LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
# OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.

SYSTEMTESTTOP=..
. $SYSTEMTESTTOP/conf.sh

KEYGEN="$KEYGEN -qr $RANDFILE"

$SHELL clean.sh

# Test 1: KSK goes inactive before successor is active
dir=01-ksk-inactive
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
ksk1=`$KEYGEN -K $dir -3fk example.com`
$SETTIME -K $dir -I +9mo -D +1y $ksk1 > /dev/null 2>&1
ksk2=`$KEYGEN -K $dir -S $ksk1`
$SETTIME -K $dir -I +7mo $ksk1 > /dev/null 2>&1
zsk1=`$KEYGEN -K $dir -3 example.com`

# Test 2: ZSK goes inactive before successor is active
dir=02-zsk-inactive
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
zsk1=`$KEYGEN -K $dir -3 example.com`
$SETTIME -K $dir -I +9mo -D +1y $zsk1 > /dev/null 2>&1
zsk2=`$KEYGEN -K $dir -S $zsk1`
$SETTIME -K $dir -I +7mo $zsk1 > /dev/null 2>&1
ksk1=`$KEYGEN -K $dir -3fk example.com`

# Test 3: KSK is unpublished before its successor is published
dir=03-ksk-unpublished
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
ksk1=`$KEYGEN -K $dir -3fk example.com`
$SETTIME -K $dir -I +9mo -D +1y $ksk1 > /dev/null 2>&1
ksk2=`$KEYGEN -K $dir -S $ksk1`
$SETTIME -K $dir -D +6mo $ksk1 > /dev/null 2>&1
zsk1=`$KEYGEN -K $dir -3 example.com`

# Test 4: ZSK is unpublished before its successor is published
dir=04-zsk-unpublished
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
zsk1=`$KEYGEN -K $dir -3 example.com`
$SETTIME -K $dir -I +9mo -D +1y $zsk1 > /dev/null 2>&1
zsk2=`$KEYGEN -K $dir -S $zsk1`
$SETTIME -K $dir -D +6mo $zsk1 > /dev/null 2>&1
ksk1=`$KEYGEN -K $dir -3fk example.com`

# Test 5: KSK deleted and successor published before KSK is deactivated
# and successor activated.
dir=05-ksk-unpub-active
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
ksk1=`$KEYGEN -K $dir -3fk example.com`
$SETTIME -K $dir -I +9mo -D +8mo $ksk1 > /dev/null 2>&1
ksk2=`$KEYGEN -K $dir -S $ksk1`
zsk1=`$KEYGEN -K $dir -3 example.com`

# Test 6: ZSK deleted and successor published before ZSK is deactivated
# and successor activated.
dir=06-zsk-unpub-active
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
zsk1=`$KEYGEN -K $dir -3 example.com`
$SETTIME -K $dir -I +9mo -D +8mo $zsk1 > /dev/null 2>&1
zsk2=`$KEYGEN -K $dir -S $zsk1`
ksk1=`$KEYGEN -K $dir -3fk example.com`

# Test 7: KSK rolled with insufficient delay after prepublication.
dir=07-ksk-ttl
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
ksk1=`$KEYGEN -K $dir -3fk example.com`
$SETTIME -K $dir -I +9mo -D +1y $ksk1 > /dev/null 2>&1
ksk2=`$KEYGEN -K $dir -S $ksk1`
$SETTIME -K $dir -P +269d $ksk2 > /dev/null 2>&1
zsk1=`$KEYGEN -K $dir -3 example.com`

# Test 8: ZSK rolled with insufficient delay after prepublication.
dir=08-zsk-ttl
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
zsk1=`$KEYGEN -K $dir -3 example.com`
$SETTIME -K $dir -I +9mo -D +1y $zsk1 > /dev/null 2>&1
zsk2=`$KEYGEN -K $dir -S $zsk1`
# allow only 1 day between publication and activation
$SETTIME -K $dir -P +269d $zsk2 > /dev/null 2>&1
ksk1=`$KEYGEN -K $dir -3fk example.com`

# Test 9: No special preparation needed
rm -f $dir/K*.key
rm -f $dir/K*.private

# Test 10: Valid key set, but rollover period has changed
dir=10-change-roll
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
ksk1=`$KEYGEN -K $dir -3fk example.com`
zsk1=`$KEYGEN -K $dir -3 example.com`
$SETTIME -K $dir -I +3mo -D +4mo $zsk1 > /dev/null 2>&1
zsk2=`$KEYGEN -K $dir -S $zsk1`

# Test 11: Many keys all simultaneously scheduled to be active in the future
dir=11-many-simul
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
k1=`$KEYGEN -K $dir -q3fk -P now+1mo -A now+1mo example.com`
z1=`$KEYGEN -K $dir -q3 -P now+1mo -A now+1mo example.com`
z2=`$KEYGEN -K $dir -q3 -P now+1mo -A now+1mo example.com`
z3=`$KEYGEN -K $dir -q3 -P now+1mo -A now+1mo example.com`
z4=`$KEYGEN -K $dir -q3 -P now+1mo -A now+1mo example.com`

# Test 12: Many keys all simultaneously scheduled to be active in the past
dir=12-many-active
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
k1=`$KEYGEN -K $dir -q3fk example.com`
z1=`$KEYGEN -K $dir -q3 example.com`
z2=`$KEYGEN -K $dir -q3 example.com`
z3=`$KEYGEN -K $dir -q3 example.com`
z4=`$KEYGEN -K $dir -q3 example.com`

# Test 13: Multiple simultaneous keys with no configured roll period
dir=13-noroll
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
k1=`$KEYGEN -K $dir -q3fk example.com`
k2=`$KEYGEN -K $dir -q3fk example.com`
k3=`$KEYGEN -K $dir -q3fk example.com`
z1=`$KEYGEN -K $dir -q3 example.com`

# Test 14: Keys exist but have the wrong algorithm
dir=14-wrongalg
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
k1=`$KEYGEN -K $dir -qfk example.com`
z1=`$KEYGEN -K $dir -q example.com`
$SETTIME -K $dir -I now+6mo -D now+8mo $z1 > /dev/null
z2=`$KEYGEN -K $dir -q -S ${z1}.key`
$SETTIME -K $dir -I now+1y -D now+14mo $z2 > /dev/null
z3=`$KEYGEN -K $dir -q -S ${z2}.key`
$SETTIME -K $dir -I now+18mo -D now+20mo $z3 > /dev/null
z4=`$KEYGEN -K $dir -q -S ${z3}.key`

# Test 15: No zones specified; just search the directory for keys
dir=15-unspec
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
k1=`$KEYGEN -K $dir -q3fk example.com`
z1=`$KEYGEN -K $dir -q3 example.com`
$SETTIME -K $dir -I now+6mo -D now+8mo $z1 > /dev/null
z2=`$KEYGEN -K $dir -q -S ${z1}.key`
$SETTIME -K $dir -I now+1y -D now+14mo $z2 > /dev/null
z3=`$KEYGEN -K $dir -q -S ${z2}.key`
$SETTIME -K $dir -I now+18mo -D now+20mo $z3 > /dev/null
z4=`$KEYGEN -K $dir -q -S ${z3}.key`

# Test 16: No zones specified; search the directory for keys;
# keys have the wrong algorithm for their policies
dir=16-wrongalg-unspec
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
k1=`$KEYGEN -K $dir -qfk example.com`
z1=`$KEYGEN -K $dir -q example.com`
$SETTIME -K $dir -I now+6mo -D now+8mo $z1 > /dev/null
z2=`$KEYGEN -K $dir -q -S ${z1}.key`
$SETTIME -K $dir -I now+1y -D now+14mo $z2 > /dev/null
z3=`$KEYGEN -K $dir -q -S ${z2}.key`
$SETTIME -K $dir -I now+18mo -D now+20mo $z3 > /dev/null
z4=`$KEYGEN -K $dir -q -S ${z3}.key`

# Test 17: Keys are simultaneously active but we run with no force
# flag (this should fail)
dir=17-noforce
echo I:set up $dir
rm -f $dir/K*.key
rm -f $dir/K*.private
k1=`$KEYGEN -K $dir -q3fk example.com`
z1=`$KEYGEN -K $dir -q3 example.com`
z2=`$KEYGEN -K $dir -q3 example.com`
z3=`$KEYGEN -K $dir -q3 example.com`
z4=`$KEYGEN -K $dir -q3 example.com`