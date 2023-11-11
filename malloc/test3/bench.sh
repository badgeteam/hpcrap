#!/bin/bash

bash build-bench.sh

echo "Badge"
time ./bench-badge
echo "System"
time ./bench-system
