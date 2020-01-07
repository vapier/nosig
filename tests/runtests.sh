#!/bin/bash -e
# Written by Mike Frysinger <vapier@gmail.com>
# Released into the public domain.

# Run unittests!

fullpath() {
	# Because portability is a pita.
	realpath "$0" 2>/dev/null && return 0
	readlink -f "$0" 2>/dev/null && return 0
	python -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$0"
}

SCRIPT="$(fullpath "$@")"
TESTDIR="$(dirname "${SCRIPT}")"
TOP_SRCDIR="$(dirname "${TESTDIR}")"

if [ -z "${NOSIG}" ]; then
	NOSIG="${TOP_SRCDIR}/nosig"
fi
nosig() { "${NOSIG}" "$@"; }
get_status() {
	set +x
	out=$(nosig "$@" --show-status | tr ' ' '\n')
	set -x
}

echo "using nosig: ${NOSIG}"

echo "Setting up test env"

TMPDIR=""
cleanup() {
	# See if we're exiting due to an error.
	local ret=$1
	# Don't allow failures in this hook to prevent final cleanup.
	set +e
	if [ ${ret} -ne 0 ]; then
		ls -Ral "${TMPDIR}"
	fi
	rm -rf "${TMPDIR}"
}
trap 'cleanup $?' EXIT
TMPDIR="$(mktemp -d)"

cd "${TMPDIR}"

set -x

# Disable coredumps.
ulimit -c 0

# See if realtime signals are supported.
if nosig --version | grep -q 'Realtime signals supported'; then
	SIG_RT="yes"
else
	SIG_RT="no"
	# Sanity check: Linux always supports realtime signals.
	[ "$(uname -s)" != "Linux" ]
fi

: "### Check CLI info flags output & exit status"
check_flag() {
	local flag="$1" out

	(
	# The shell trace output can write to stderr which we check here.
	set +x

	# Check exit status & output to stdout.
	out=$(nosig "${flag}")
	[ -n "${out}" ]

	out=$(nosig "${flag}" 2>&1 >/dev/null)
	[ -z "${out}" ]
	)
}
check_flag --version
check_flag -V
check_flag --list
check_flag -l
check_flag --help
check_flag -h
check_flag --show-status

: "### Check unknown flag exit status"
check_exit() {
	local ret=0 exp="$1"
	shift
	nosig "$@" || ret=$?
	[ ${ret} -eq ${exp} ]
}

check_exit 125 --badflag

: "### Check unknown flag write to stderr only"
out=$(nosig --badflag 2>/dev/null) || :
[ -z "${out}" ]

(
set +x
out=$(nosig --badflag 2>&1) || :
[ -n "${out}" ]
)

: "### Check -- handling"
check_exit 127 -- --help

: "### Check missing program"
check_exit 127 alksdjflkasdjfklasdjflkasdjf

: "### Check non-exec programs"
check_exit 126 ./

touch noexec
check_exit 126 ./noexec

: "### Check exit status pass through"
nosig true

check_exit 1 sh -c 'exit 1'
check_exit 50 sh -c 'exit 50'
check_exit 128 sh -c 'exit 128'

: "### Check valid sigspec parsing"
nosig --ignore SIGTERM true
nosig --ignore TERM true
nosig --ignore 1 true

if [ "${SIG_RT}" = "yes" ]; then
	nosig --ignore SIGRTMIN true
	nosig --ignore SIGRTMIN+0 true
	nosig --ignore SIGRTMIN+10 true
	nosig --ignore SIGRTMAX true
	nosig --ignore SIGRTMAX-0 true
	nosig --ignore SIGRTMAX-10 true
fi

: "### Check invalid sigspec values"
check_exit 125 --ignore badsigname true
check_exit 125 --ignore SIGBADNAMEFORSURE true
check_exit 125 --ignore -10 true
check_exit 125 --ignore -9999999999999999999999999999999999999 true
check_exit 125 --ignore 10000 true
check_exit 125 --ignore 9999999999999999999999999999999999999 true
check_exit 125 --ignore 0x1 true

if [ "${SIG_RT}" = "yes" ]; then
	check_exit 125 --ignore SIGRTMIN-1 true
	check_exit 125 --ignore SIGRTMIN+1000 true
	check_exit 125 --ignore SIGRTMAX+1 true
	check_exit 125 --ignore SIGRTMAX-1000 true
fi

: "### Dump state for logging/triaging in case of failure"
nosig --reset -vv --show-status
nosig --show-status
nosig --reset --show-status
nosig --reset --ignore-all --block-all --show-status
nosig --reset --ignore-all-std --block-all-std --show-status
if [ "${SIG_RT}" = "yes" ]; then
	nosig --reset --ignore-all-rt --block-all-rt --show-status
fi
# NB: Not the same code path as --block-all.
nosig --reset --fill --block --show-status

: "### Check reset behavior & count signals"
# No signals should be blocked or ignored.
get_status --reset

ret=0
grep -e'^[bi]' <<<"${out}" || ret=1
[ ${ret} -eq 1 ]

# Figure out max unblocked/default signals.
SIG_UNBLOCK=$(grep -c -e'^u' <<<"${out}")
SIG_DFL=$(grep -c -e'^d' <<<"${out}")

# These really should be the same ...
[ ${SIG_UNBLOCK} -eq ${SIG_DFL} ]

# Figure out max blocked/ignored signals.
get_status --reset --block-all --ignore-all

SIG_BLOCK=$(grep -c -e'^b' <<<"${out}")
SIG_IGN=$(grep -c -e'^i' <<<"${out}")

# These should be close, but the OS will fudge it, so add some wiggle room.
OS_RESERVED_COUNT=10
[ ${SIG_BLOCK} -gt $(( SIG_UNBLOCK - OS_RESERVED_COUNT )) ]
[ ${SIG_IGN} -gt $(( SIG_DFL - OS_RESERVED_COUNT )) ]

: "### Check signal block/unblock all"
# Should be nothing blocked.
get_status --reset --block-all --unblock-all
cnt=$(grep -c -e '^b' <<<"${out}" || :)
[ ${cnt} -eq 0 ]

# Should be less than max blocked.
get_status --reset --block-all-std
cnt=$(grep -c -e '^b' <<<"${out}")
if [ "${SIG_RT}" = "yes" ]; then
	[ ${cnt} -lt ${SIG_BLOCK} ]
else
	[ ${cnt} -eq ${SIG_BLOCK} ]
fi

if [ "${SIG_RT}" = "yes" ]; then
	get_status --reset --block-all-rt
	cnt=$(grep -c -e '^b' <<<"${out}")
	[ ${cnt} -lt ${SIG_BLOCK} ]
fi

get_status --reset --block-all --unblock-all-std
cnt=$(grep -c -e '^b' <<<"${out}" || :)
if [ "${SIG_RT}" = "yes" ]; then
	[ ${cnt} -lt ${SIG_BLOCK} -a ${cnt} -gt 0 ]
else
	[ ${cnt} -eq 0 ]
fi

if [ "${SIG_RT}" = "yes" ]; then
	get_status --reset --block-all --unblock-all-rt
	cnt=$(grep -c -e '^b' <<<"${out}")
	[ ${cnt} -lt ${SIG_BLOCK} ]
fi

: "### Check signal ignore/default all"
# Should be nothing ignored.
get_status --reset --ignore-all --default-all
cnt=$(grep -c -e '^i' <<<"${out}" || :)
[ ${cnt} -eq 0 ]

# Should be less than max ignored.
get_status --reset --ignore-all-std
cnt=$(grep -c -e '^i' <<<"${out}")
if [ "${SIG_RT}" = "yes" ]; then
	[ ${cnt} -lt ${SIG_IGN} ]
else
	[ ${cnt} -eq ${SIG_IGN} ]
fi

if [ "${SIG_RT}" = "yes" ]; then
	get_status --reset --ignore-all-rt
	cnt=$(grep -c -e '^i' <<<"${out}")
	[ ${cnt} -lt ${SIG_IGN} ]
fi

get_status --reset --ignore-all --default-all-std
cnt=$(grep -c -e '^i' <<<"${out}" || :)
if [ "${SIG_RT}" = "yes" ]; then
	[ ${cnt} -lt ${SIG_IGN} -a ${cnt} -gt 0 ]
else
	[ ${cnt} -eq 0 ]
fi

if [ "${SIG_RT}" = "yes" ]; then
	get_status --reset --ignore-all --default-all-rt
	cnt=$(grep -c -e '^i' <<<"${out}")
	[ ${cnt} -lt ${SIG_IGN} ]
fi

: "### Check signal set management"
get_status --reset --add INT --add TERM --add HUP --block
cnt=$(grep -c -e '^b' <<<"${out}")
[ ${cnt} -eq 3 ]

get_status --reset \
	--add INT --block --empty \
	--add TERM --block --empty \
	--add HUP --block
cnt=$(grep -c -e '^b' <<<"${out}")
[ ${cnt} -eq 3 ]

get_status --reset \
	--add INT --add TERM --add HUP --block \
	--del HUP --unblock
cnt=$(grep -c -e '^b' <<<"${out}")
[ ${cnt} -eq 1 ]

get_status --fill --block
cnt=$(grep -c -e '^b' <<<"${out}")
[ ${cnt} -eq ${SIG_BLOCK} ]

get_status --fill --block --unblock
cnt=$(grep -c -e '^b' <<<"${out}" || :)
[ ${cnt} -eq 0 ]

get_status --fill --block --empty --unblock
cnt=$(grep -c -e '^b' <<<"${out}")
[ ${cnt} -eq ${SIG_BLOCK} ]

get_status --fill --set
cnt=$(grep -c -e '^b' <<<"${out}")
[ ${cnt} -eq ${SIG_BLOCK} ]

get_status --fill --set --del HUP --del INT --unblock
cnt=$(grep -c -e '^b' <<<"${out}")
[ ${cnt} -eq 2 ]

get_status --empty --set
cnt=$(grep -c -e '^b' <<<"${out}" || :)
[ ${cnt} -eq 0 ]

get_status --fill --block --empty --set
cnt=$(grep -c -e '^b' <<<"${out}" || :)
[ ${cnt} -eq 0 ]

: "### Check signal blocking with real use"
# This should die, so it's a baseline sanity check.
sigret=0
nosig sh -c 'kill -INT $$; exit 1' || sigret=$?
[ ${sigret} -gt 127 ]

check_exit 2 --reset --add SIGINT --block sh -c 'kill -INT $$; exit 2'
check_exit 2 --reset --add INT --block sh -c 'kill -INT $$; exit 2'
check_exit 2 --reset --block-all sh -c 'kill -INT $$; exit 2'
check_exit 2 --reset --block-all-std sh -c 'kill -INT $$; exit 2'

if [ "${SIG_RT}" = "yes" ]; then
	check_exit ${sigret} --reset --block-all-rt sh -c 'kill -INT $$; exit 2'
fi

check_exit ${sigret} --reset --block-all-std --add INT --unblock sh -c 'kill -INT $$; exit 2'

: "### Check signal ignoring with real use"
check_exit 2 --reset --ignore SIGINT sh -c 'kill -INT $$; exit 2'
check_exit 2 --reset --ignore INT sh -c 'kill -INT $$; exit 2'
check_exit 2 --reset --ignore-all sh -c 'kill -INT $$; exit 2'
check_exit 2 --reset --ignore-all-std sh -c 'kill -INT $$; exit 2'

if [ "${SIG_RT}" = "yes" ]; then
	check_exit ${sigret} --reset --ignore-all-rt sh -c 'kill -INT $$; exit 2'
fi
check_exit ${sigret} --reset --ignore TERM sh -c 'kill -INT $$; exit 2'
check_exit ${sigret} --reset --ignore INT --default INT sh -c 'kill -INT $$; exit 2'

: "### Check numeric signal parsing"
# NB: Only use signals where POSIX mandates their number<->name binding.
# https://pubs.opengroup.org/onlinepubs/9699919799/utilities/kill.html
check_exit 2 --reset --ignore 2 sh -c 'kill -INT $$; exit 2'
check_exit ${sigret} --reset --ignore 2 --default INT sh -c 'kill -INT $$; exit 2'

: "### All passed!"
set +x
