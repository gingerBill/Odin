#!/usr/bin/env bash

release_mode=$1

warnings_to_disable="-std=c++11 -Wno-switch"

libraries="-pthread -ldl -lm -lstdc++ -lz -lcurses -lxml2"
other_args="-DLLVM_BACKEND_SUPPORT"
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

	llvm_config_flags="--cxxflags --ldflags"
	# llvm_config_flags="${llvm_config_flags} --link-static"
	llvm_config="llvm-config ${llvm_config_flags}"

	other_args="${other_args} -liconv"
	other_args="${other_args} `${llvm_config}` -lLLVM-C"
elif [[ "$(uname)" == "FreeBSD" ]]; then
	compiler="clang"
fi

if [[ "$2" == "install" ]]; then
	sudo ln -sf $PWD/odin /usr/bin/odin
fi

${compiler} src/main.cpp ${warnings_to_disable} ${libraries} ${other_args} -o odin
	# && ./odin run examples/demo/demo.odin -llvm-api
