#!/usr/bin/env bash

release_mode=$1

warnings_to_disable="-std=c++11 -Wno-switch -Wno-pointer-sign -Wno-tautological-constant-out-of-range-compare -Wno-tautological-compare -Wno-macro-redefined"
libraries="-pthread -ldl -lm -lstdc++"
other_args=""
compiler="clang"

if [ -z "$release_mode" ]; then release_mode="0"; fi

if [ "$release_mode" -eq "0" ]; then
	other_args="${other_args} -g"
fi
if [ "$release_mode" -eq "1" ]; then
	other_args="${other_args} -O3 -march=native"
fi

if [[ "$(uname)" == "Darwin" ]]; then

	# Set compiler to clang on MacOS
	# MacOS provides a symlink to clang called gcc, but it's nice to be explicit here.
	compiler="clang"

	other_args="${other_args} -liconv"
elif [[ "$(uname)" == "FreeBSD" ]]; then
	compiler="clang"
fi

if [[ "$2" == "install" ]]; then
	sudo ln -sf $PWD/odin /usr/bin/odin
fi

${compiler} src/main.cpp ${warnings_to_disable} ${libraries} ${other_args} -o odin && ./odin run examples/demo/demo.odin
