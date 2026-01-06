#!/usr/bin/env bash

set -euo pipefail
IFS=$'\n\t'

TEST_DIR="$(realpath ../../b/tests)"
IBF="$(realpath ./ibf)"

if [[ ! -z ${BF:-} ]]; then
	echo -n
elif which bf > /dev/null 2> /dev/null; then
	BF=bf;
elif which brainfuck > /dev/null 2> /dev/null; then
	BF=brainfuck;
fi

scratch=$(mktemp -d -t tmp.XXXXXXXXXX)
function finish {
  rm -rf "$scratch"
}
trap finish EXIT

if [[ -z ${1:-} ]]; then
	pushd $TEST_DIR > /dev/null
	tests=$(find -type f -name '*.b' -printf '%P\n')
	popd > /dev/null
else
	tests=($1);
fi

for testname_r in ${tests[@]}; do
	file=$TEST_DIR/$testname_r
	testname=$(echo $testname_r | sed 's&/&_&g')
	echo Running $testname...;

	inp="< $testname.in"
	in=$TEST_DIR/$testname_r.in
	if [ ! -e $in ]; then
		inp=""
		in=/dev/null
	fi

	if [ -e $TEST_DIR/$testname_r.out ]; then
		cp $TEST_DIR/$testname_r.out $scratch/$testname.ref.out
	else
                if [[ -z ${BF:-} ]]; then
                        echo No reference interpreter found
                        echo Set \$BF to set it\'s path
                        exit 1
                fi
		echo '  >' ${BF} $testname $inp
		${BF} $file > $scratch/$testname.ref.out < $in
		if [[ -n ${GENERATE_OUTS:-} ]]; then
			echo "  Warning: generating outfile"
			cp $scratch/$testname.ref.out $TEST_DIR/$testname_r.out
		fi
	fi

	echo '  >' ibf $testname $inp

        pagedir=$(mktemp -d -t tmp.XXXXXXXXXX)
        pushd $pagedir > /dev/null

	$IBF $file > $scratch/$testname.got.out < $in

        popd > /dev/null
        rm -rf $pagedir

	diff $scratch/$testname.ref.out $scratch/$testname.got.out
done

echo All OK
