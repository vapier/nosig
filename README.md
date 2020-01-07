# nosig

A small utility akin to `nohup`: quickly set up the signal block mask & signal
dispositions before executing another program.

## Portability

nosig should be usable on any recent POSIX compliant system.
[C11] & [POSIX/IEEE Std 1003.1-2008][POSIX-2008] are the current requirements.

There is no desire to port to any non-POSIX system (e.g. Windows).

Your C library will also have to support `getopt_long` via `getopt.h`.

## Building

All you need is a [C11] compiler & [GNU make].
Run `make` and you're done!


[C11]: https://en.wikipedia.org/wiki/C11_(C_standard_revision)
[GNU make]: https://www.gnu.org/software/make/
[POSIX-2008]: https://pubs.opengroup.org/onlinepubs/9699919799.2008edition/
