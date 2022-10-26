# nosig(1): run a program with specified signals blocked

- [Synopsis](#synopsis)
- [Description](#description)
  - [Resetting state](#resetting-state)
  - [Signal specifications (sigspec)](#signal-specifications-sigspec)
  - [Standard vs realtime signals](#standard-vs-realtime-signals)
  - [Signal block mask management](#signal-block-mask-management)
- [Options](#options)
  - [Generic options](#generic-options)
  - [Signal disposition (signal(2)) options](#signal-disposition-signal2-options)
  - [Signal set management (sigsetops(3)) options](#signal-set-management-sigsetops3-options)
  - [Signal set usage (sigprocmask(2)) options](#signal-set-usage-sigprocmask2-options)
  - [Output options](#output-options)
  - [Informational options](#informational-options)
- [Notes](#notes)
  - [Unblockable/unignorable signals](#unblockableunignorable-signals)
  - [Reserved realtime signals](#reserved-realtime-signals)
  - [Alternative signal dispositions](#alternative-signal-dispositions)
  - [Locking signal settings](#locking-signal-settings)
- [Examples](#examples)
  - [Common uses](#common-uses)
  - [Advanced signal block mask uses](#advanced-signal-block-mask-uses)
- [Exit Status](#exit-status)
- [Reporting Bugs](#reporting-bugs)
- [Authors](#authors)
- [See Also](#see-also)

## Synopsis

    nosig [options...] [--] program [arguments...]
    nosig [--ignore|--default] sigspec [...] program [...]
    nosig [--add|--del] sigspec [--block|--unblock|--set] [...] program [...]

## Description

The **nosig** program is used to quickly manage signal dispositions and
the signal block mask. These are distinct settings, although most users
will not care about the difference.

If you\'re familiar with the
[**nohup**(1)](http://man7.org/linux/man-pages/man1/nohup.1.html)
program, then **nosig** is like that but way more flexible. The
equivalent to \`nohup \...\` is \`nosig \--ignore SIGHUP \...\`. That
is, [**nohup**(1)](http://man7.org/linux/man-pages/man1/nohup.1.html)
ignores *SIGHUP* by setting its signal disposition to *SIG_IGN* before
executing the specified program. It does not add the signal to the
signal block mask.

Most users will be able to accomplish what they want using only the
**\--ignore** option. That will run programs with specific signals
initially ignored.

**nosig** options operate as a state machine that take effect
immediately. That means later options will always override earlier
options, and all options may be combined as needed. This allows you to
quickly reason about behavior as well as fully set up known states
including fully resetting signal dispositions or the signal block mask.

Changing the signal block mask is a fairly advanced topic, and most
users can skip over those options the vast majority of the time. They
might be useful as a fallback when dealing with a particularly stubborn
program; see the
**[Signal block mask management](#signal-block-mask-management)** and
**[Examples](#examples)** sections below for more details.

### Resetting state

**nosig** offers a bunch of options to quickly reset all signal state.
Specifically, **\--reset** will reset all signal dispositions & block
masks. This can help when trying to debug a setup where signals are
being initialized incorrectly and you want to guarantee good state
before running another tool.

If you need a little more fine-grained selection, a variety of
**\--xxx-all** options are available. This way you don\'t have to
verbosely list out every possible signal yourself.

### Signal specifications (sigspec)

tl;dr: Use names like *SIGINT* or *SIGTERM* to specify signals.

**nosig** options operate on a simple specification (a.k.a. a
*sigspec*).

These are symbolic signal names (strongly preferred), or system-specific
numbers (strongly discouraged). The symbolic names are portable across
systems while the numbers might not be stable even on the current
system. This is due to signal handling in the system itself, not
anything under the control of **nosig**. Support for numbers is largely
a fallback if the symbolic signal is unknown on your system.

Most people will use common signal names like *SIGINT*, or omit the
leading *SIG* prefix and simply use *INT*.

For realtime signals, you\'ll want to use the names **SIGRTMIN** and
**SIGRTMAX** with offsets like **SIGRTMIN+1** or **SIGRTMAX-1**.

The set of *sigspecs* that **nosig** supports can be displayed with the
**\--list** option.

### Standard vs realtime signals

If you\'re not familiar with realtime signals, then you most likely can
completely ignore them!

The signals users are most familiar with and utilize on a daily basis
are the standard signals (or simply just \"signals\"). These have the
signal names you\'re used to seeing like **SIGINT** or **SIGTERM**.

Realtime signals on the other hand only have two named signal bases, and
all signals are derived from those as offsets. Those are **SIGRTMIN**
and **SIGRTMAX**. Programs will then define their own internal names &
uses for signals like **SIGRTMIN** and **SIGRTMIN+1** (since POSIX only
defines realtime signals as a range). That is why **nosig** uses those
naming conventions --- they hopefully align well with whatever program
you\'re working with.

Keep in mind that, while the standard signals have names that reflect
their meaning & intended use, the realtime signals have no predefined
meaning. So the only way to know how a specific realtime signal (e.g.
**SIGRTMIN+5**) is used, or whether it\'s used at all, is entirely
specific to a program.

The **\--ignore-all** and **\--block-all** options will operate on all
standard & realtime signals as a default. Additional flags are provided
to select the respective subsets.

### Signal block mask management

The signal block mask is used to block delivery of signals without
having to change their signal disposition or handlers. Many programs
never register handlers for signals, so simply changing their
dispositions (using **\--ignore**) is sufficient for most users to
effectively \"block a signal\" for a program.

If you have a program that registers a signal handler for a signal that
you\'re trying to block, then attempts to use **\--ignore** will be
overridden (see the
**[Locking signal settings](#locking-signal-settings)** section below
for more details). In that case, blocking the signal using the signal
block mask might work.

The signal set options (**\--add**, **\--del**, **\--empty**,
**\--fill**, **\--block**, **\--unblock**, **\--set**) operate on a
single signal set. The signal set always starts off empty. Using
**\--empty** as the first option would be redundant, but you might
prefer it for clarity.

You then modify the signal set (adding, deleting, clearing, filling
signals) before using the signal set (blocking, unblocking, setting
signals). You may repeat these series of actions (modify, use, reset) as
many times as makes sense for your setup; **nosig** has no limit here.

For example, you might want to block a few signals, unblock a few
signals, but leave all the rest unchanged. You would modify the set
(e.g. **\--add** multiple times), use the set (e.g. **\--block**), reset
the set (e.g. **\--empty**), modify the set (e.g. **\--add** multiple
times), then use the set again (e.g. **\--unblock**).

If you want the inverse behavior (only unblocking one or two signals),
then you probably want to take the opposite approach by adding all
signals to the set (e.g. **\--fill**), removing the few signals you care
about (e.g. **\--del** multiple times), and then setting the signal
block mask (e.g. **\--set**).

## Options

### Generic options

- **\--reset**  
  Unblock all signals and reset to their default dispositions.

- **-v**, **\--verbose**  
  Display verbose output/warnings that are normally safe to ignore.
  Specifying this more than once tends to make things more verbose.

### Signal disposition (signal(2)) options

- **-I**, **\--ignore** ***sigspec***  
  Set the signal disposition to ignore.  
  See *SIG_IGN* in
  [**signal**(2)](http://man7.org/linux/man-pages/man2/signal.2.html)
  for more details.

- **\--ignore-all**  
  Set all signal dispositions (both standard & realtime) to ignore.

- **\--ignore-std-all**  
  Set all standard signal dispositions to ignore. Does not modify
  realtime signals.

- **\--ignore-rt-all**  
  Set all realtime signal dispositions to ignore. Does not modify
  standard signals.

- **-D**, **\--default** ***sigspec***  
  Reset the signal disposition to its default.  
  See *SIG_DFL* in
  [**signal**(2)](http://man7.org/linux/man-pages/man2/signal.2.html)
  for more details.

- **\--default-all**  
  Reset all signal dispositions (both standard & realtime) to their
  default.

- **\--default-std-all**  
  Reset all standard signal dispositions to their default. Does not
  modify realtime signals.

- **\--default-rt-all**  
  Reset all realtime signal dispositions to their default. Does not
  modify standard signals.

### Signal set management (sigsetops(3)) options

- **-a**, **\--add** ***sigspec***  
  Add *sigspec* to the current signal set.  
  See
  [**sigaddset**(3)](http://man7.org/linux/man-pages/man3/sigaddset.3.html)
  for more details.

- **-d**, **\--del** ***sigspec***  
  Delete *sigspec* from the current signal set.  
  See
  [**sigdelset**(3)](http://man7.org/linux/man-pages/man3/sigdelset.3.html)
  for more details.

- **-e**, **\--empty**  
  Clear the current signal set.  
  See
  [**sigemptyset**(3)](http://man7.org/linux/man-pages/man3/sigemptyset.3.html)
  for more details.

- **-f**, **\--fill**  
  Add all signals to the current signal set.  
  See
  [**sigfillset**(3)](http://man7.org/linux/man-pages/man3/sigfillset.3.html)
  for more details.

### Signal set usage (sigprocmask(2)) options

- **-b**, **\--block**  
  Block the signals in the current signal set. Signals not in the signal
  set will not change.  
  See *SIG_BLOCK* in
  [**sigprocmask**(2)](http://man7.org/linux/man-pages/man2/sigprocmask.2.html)
  for more details.

- **\--block-all**  
  Add all signals to the signal block mask. Does not modify or use the
  current signal set.  
  A shortcut similar to **\--fill \--block**.

- **\--block-all-std**  
  Add all standard signals to the signal block mask. Does not modify or
  use the current signal set.

- **\--block-all-rt**  
  Add all realtime signals to the signal block mask. Does not modify or
  use the current signal set.

- **-u**, **\--unblock**  
  Unblock the signals in the current signal set. Signals not in the
  signal set will not change.  
  See *SIG_UNBLOCK* in
  [**sigprocmask**(2)](http://man7.org/linux/man-pages/man2/sigprocmask.2.html)
  for more details.

- **\--unblock-all**  
  Remove all signals from the signal block mask. Does not modify or use
  the current signal set.  
  A shortcut similar to **\--fill \--unblock**.

- **\--unblock-all-std**  
  Remove all standard signals from the signal block mask. Does not
  modify or use the current signal set.

- **\--unblock-all-rt**  
  Remove all realtime signals from the signal block mask. Does not
  modify or use the current signal set.

- **-s**, **\--set**  
  Block the signals in the current signal set, and unblock all signals
  not in the current signal set.  
  See *SIG_SETMASK* in
  [**sigprocmask**(2)](http://man7.org/linux/man-pages/man2/sigprocmask.2.html)
  for more details.

### Output options

- **\--stdin** *path*  
  Redirect input (stdin) from *path*. The path will be opened for
  reading, and symlinks will be followed. This is a convenience option
  akin to shell redirects like \`\<path\`.

- **\--stdout** *path*  
  Redirect stdout to *path*. The path will be opened for writing,
  truncated, created if needed using mode 0666 (respecting the user\'s
  [**umask**(2)](http://man7.org/linux/man-pages/man2/umask.2.html)),
  and symlinks followed. This is a convenience option akin to shell
  redirects like \`\>path\`.

- **\--stderr** *path*  
  Redirect stderr to *path*. The path will be opened for writing,
  truncated, created if needed using mode 0666 (respecting the user\'s
  [**umask**(2)](http://man7.org/linux/man-pages/man2/umask.2.html)),
  and symlinks followed. This is a convenience option akin to shell
  redirects like \`2\>path\`.

- **\--output** *path*  
  Redirect output (stdout & stderr) to *path*. The path will be opened
  for writing, truncated, created if needed using mode 0666 (respecting
  the user\'s
  [**umask**(2)](http://man7.org/linux/man-pages/man2/umask.2.html)),
  and symlinks followed. This is a convenience option akin to shell
  redirects like \`\>path 2\>&1\` or (the bashism) \`\>&path\`.  
    
  If you want to write stdout & stderr to the same path, make sure to
  use this rather than separate *\--stdout* and *\--stderr* options as
  those will truncate the same path and write over top of each other.

- **\--null-io**  
  Redirect input (stdin) from, and output (stdout & stderr) to,
  */dev/null*.

### Informational options

- **\--show-status**  
  Display current signal dispositions and the signal block mask. This is
  meant for debugging/testing purposes only, so its output is not
  stable.

- **-l**, **\--list**  
  List available/known symbolic signal names (*sigspecs*) and exit.

- **-V**, **\--version**  
  Show version information and exit.

- **-h**, **\--help**  
  Show usage information and exit.

## Notes

### Unblockable/unignorable signals

There are a few signals that the OS might not allow you to modify. Most
notably, *SIGKILL* and *SIGSTOP* usually may not be blocked or ignored.
There is nothing **nosig** (or any other program) can do to workaround
this OS restriction.

The OS will usually silently ignore requests to block them. **nosig**
does not attempt to diagnose this for the user.

The OS might return errors to ignore these signals, but **nosig** will
silently ignore these errors by default too.

This may also come up with the reserved realtime signals; see the
**[Reserved realtime signals](#reserved-realtime-signals)** section for
more details on those.

### Reserved realtime signals

The signals **SIGRTMIN** & **SIGRTMAX** are not actually constant.
Depending on the OS & runtime libraries, POSIX allows them to be
dynamic. This allows the runtime to reserve a few signals for internal
purposes.

Notably, GNU C library (glibc)\'s native POSIX threads library
(pthreads/NPTL) will reserve two signals for its own internal use. The
[**nptl**(7)](http://man7.org/linux/man-pages/man7/nptl.7.html) man page
goes into great detail here.

**nosig** will not attempt to bypass these reservations. It rarely (if
ever) makes sense to do so, and certainly the vast majority of users
would never want such behavior, let alone inadvertently or as a default.
If you really want to take over the reserved signals, you will need to
write our own code/tools to do so.

### Alternative signal dispositions

It is not possible to change the signal behavior beyond ignore & the
default disposition (i.e. make the signal trigger a
[**core**(5)](http://man7.org/linux/man-pages/man5/core.5.html) or have
it stop). This is simply how signals work and isn\'t really something
**nosig** can workaround. Doing so would require changes to the OS, or
executing code in the process itself which would require unreliable
hackery like **LD_PRELOAD** via
[**ld.so**(8)](http://man7.org/linux/man-pages/man8/ld.so.8.html).

### Locking signal settings

**nosig** only initializes the signal settings before handing off
control to the program. The program still has full control over its own
runtime signal settings, thus it may completely reset all signal
dispositions or the signal block mask. There is no way to workaround
this (see the
**[Alternative signal dispositions](#alternative-signal-dispositions)**
section for similar details).

## Examples

### Common uses

    # Ignore a single signal like `nohup`!
    nosig --ignore SIGHUP <cmd>
    alias nohup='nosig --ignore SIGHUP --'

    # Ignore SIGINT (Ctrl-C) signals.
    nosig --ignore SIGINT <cmd>

    # Ignore SIGTSTP (Ctrl-Z) signals (i.e. background/suspend requests).
    nosig --ignore SIGTSTP <cmd>

    # Ignore SIGQUIT (Ctrl-\) signals.
    nosig --ignore SIGQUIT <cmd>

    # Ignore all signals except for SIGINT (Ctrl-C).
    nosig --ignore-all --default SIGINT <cmd>

    # Ignore all signals.  The command can only be killed with SIGKILL (kill -9)!
    nosig --ignore-all <cmd>

### Advanced signal block mask uses

NB: Manipulating the signal block mask is not common. Try the examples
above first by ignoring signals.

    # Block all signals.
    nosig --block-all <cmd>
    nosig --fill --block <cmd>

    # Unblock all signals.
    nosig --unblock-all <cmd>
    nosig --fill --unblock <cmd>

    # Block all signals except SIGUSR1.
    nosig --block-all --add USR1 --unblock <cmd>

    # Block all signals, but leave SIGUSR1 unchanged.
    nosig --fill --del SIGUSR1 --block <cmd>

## Exit Status

If *program* was executed, then the exit status will be of it.

Otherwise:  
· 0 An informational **nosig** option (e.g. **\--version**) was
handled.  
· 125 **nosig** itself exited.  
· 126 *program* was found, but could not be executed.  
· 127 *program* could not be found.

## Reporting Bugs

Please report all bugs to the project page:  
<https://github.com/vapier/nosig/issues>

## Authors

Mike Frysinger \<vapier@gmail.com\>

## See Also

[**nohup**(1)](http://man7.org/linux/man-pages/man1/nohup.1.html),
[**sigaction**(2)](http://man7.org/linux/man-pages/man2/sigaction.2.html),
[**signal**(2)](http://man7.org/linux/man-pages/man2/signal.2.html),
[**sigprocmask**(2)](http://man7.org/linux/man-pages/man2/sigprocmask.2.html),
[**sigsetops**(3)](http://man7.org/linux/man-pages/man3/sigsetops.3.html),
[**signal**(7)](http://man7.org/linux/man-pages/man7/signal.7.html)
