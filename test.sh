#!/bin/bash

smv="/home/sergo/ClionProjects/smv/cmake-build-debug/smv"

pass=0
fail=0
tnum=1

function assert {
    echo -n "exit_code: "
    if [ $1 -eq $2 ]; then echo "PASS ($1)" && let pass=pass+1 && return; fi
    echo "FAIL ($1)"
    let fail=fail+1
}

function run {
    desc=$1; shift; aval=$1; shift; runc=$@
    echo $tnum.$desc:; echo $runc; $runc; assert $? $aval; echo
    let tnum=tnum+1
}

tmpf1=$(mktemp -u)
tmpf2=$(mktemp -u)
tmpdir=$(dirname $tmpf1)

run "Version" 0 $smv -V
run "Help" 0 $smv --help

run "Move nonexistent file" 1 $smv $tmpf1 $tmpf2

touch $tmpf1
run "Move regular file" 0 $smv $tmpf1 $tmpf2

touch $tmpf1
run "Overwrite file with -m --no-clobber flags" 0 $smv -m --no-clobber $tmpf1 $tmpf2
run "Test source" 0 test -e $tmpf1

touch $tmpf1
run "Overwrite file with -m --force flags" 0 $smv -m --force $tmpf1 $tmpf2
run "Test source" 1 test -e $tmpf1

run "Cleanup files" 0 rm -f $tmpf1 $tmpf2

mkdir $tmpf1
touch $tmpf2
run "Move dir to file" 4 $smv $tmpf1 $tmpf2

run "Move file to dir" 0 $smv $tmpf2 $tmpf1

mkdir $tmpf2
run "Move dir to dir" 0 $smv $tmpf1 $tmpf2

run "Cleanup dir+file" 0 rm -rf $tmpf1 $tmpf2

touch $tmpf1
run "Smv with md5sum helper" 0 $smv --helper md5sum $tmpf1 $tmpdir/%1,1,4
run "Test dest" 0 test -e $tmpdir/d41d

run "Smv make path" 0 $smv -p $tmpdir/d41d $tmpdir/d42d$tmpf1
rm -rf $tmpdir/d42d

touch $tmpf1
run "Smv with md5sum helper and non-default delimiter" 0 $smv -d# --helper md5sum $tmpf1 $tmpdir/#1,1,4
run "Test dest" 0 test -e $tmpdir/d41d
rm -rf $tmpdir/d41d

tmpf3=$tmpdir/$(echo -n `basename $tmpf1` | tr 'a-z' 'A-Z')
touch $tmpf1
run "Ignore case. Move regular file $tmpf1" 0 $smv -i $tmpf3 $tmpf2
rm -f $tmpf2

if grep -i utf-8 > /dev/null <(echo -n $LANG); then
    touch "$tmpdir/абвгдеёж"
    run "UTF-8 test." 0 $smv -h echo "$tmpdir/абвгдеёж" $tmpdir/%0,3,3
    run "Test dest" 0 test -e $tmpdir/вгд
    rm -f $tmpdir/вгд
fi

echo "PASS/FAIL $pass/$fail"
exit $fail
