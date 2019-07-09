#!/bin/bash
# [test] #30

n=0
[ "$1" ] && n="$1"
[ "$n" -eq 0 ] && n=1
source tests/sh/common.sh
source tests/sh/test.sh

: "${GWION_ADD_DIR=/usr/lib/Gwion/add}"

export GWION_ADD_DIR

test_plugin() {
	export NAME=$"$1"
	export PRG=$"../../gwion"
	export SUPP=$"../../help/supp"
	make
  if [ -f "$NAME.gw" ]
  then  GWOPT+=-p. test_gw "$NAME.gw" "$n"
  else  GWOPT+=-p. test_gw "/dev/null" "$n"
  fi
  make clean
 	N=$(printf "% 4i" "$n")
	echo "ok $N plugin test: '$NAME'"
  n=$((n+1))
}

# empty plug file
touch "empty.so"
./gwion -p. &> /dev/null
N=$(printf "% 4i" "$n")
echo "ok $N plugin test: 'empty'"
n=$((n+1))
rm "empty.so"


BASE_DIR="$PWD"
cd tests/import || exit
for test_file in *.c
do test_plugin "$(basename $test_file .c)"
done

GWOPT+="-d driver_test:with:some:argument " test_plugin driver
# clean
rm -f ./*.gcda ./*.gcno vgcore.* ./*.o ./*.so
cd "$BASE_DIR" || exit
