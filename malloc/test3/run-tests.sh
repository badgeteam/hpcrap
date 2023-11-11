#!/bin/bash

test_count=100
bash build.sh

for i in $(seq 1 $test_count); do
	./malloc32 > malloc32.log
	retval=$?
	if [ ${retval} -gt 0 ]; then
		echo
		echo "malloc32 failed with ${retval}"
		exit 1
	fi
	echo -n "."
done 

echo
echo "All runs successful"
