#!/bin/bash -e
# Written by Mike Frysinger <vapier@gmail.com>
# Released into the public domain.

# Build logic for running under Travis CI.

#
# Utility functions.
#

travis_fold() {
	if [[ -n ${TRAVIS_OS_NAME} ]] ; then
		printf 'travis_fold:%s:%s\r\n' "$@" | sed 's: :_:g'
	fi
}

if [[ -n ${TRAVIS_OS_NAME} ]] ; then
	whitebg=$(tput setab 7)
	blackfg=$(tput setaf 0)
	normal=$(tput sgr0)
else
	whitebg=
	blackbg=
	normal=
fi
v() {
	local fold=""
	case $1 in
	--fold=*) fold=${1:7}; shift;;
	esac
	if [[ -n ${fold} ]] ; then
		travis_fold start "${fold}"
		echo "\$ $*"
		"$@"
		travis_fold end "${fold}"
	else
		echo "${whitebg}${blackfg}\$ $*${normal}"
		"$@"
	fi
}

ncpus=$(getconf _NPROCESSORS_ONLN)
m() {
	v make -j${ncpus} "$@"
}

#
# Actual test code!
#

build_make() {
	m
	m check
	m install DESTDIR="${PWD}/root"
}

build_linux() { build_make; }

build_osx() { build_make; }

main() {
	"build_${TRAVIS_OS_NAME}"
}
main "$@"
