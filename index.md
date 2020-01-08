## About

A small utility akin to `nohup`: quickly set up the signal block mask & signal
dispositions before executing another program.

## Portability

nosig should be usable on any recent POSIX compliant system.
[C11] & [POSIX/IEEE Std 1003.1-2008][POSIX-2008] are the current requirements.

There is no desire to port to any non-POSIX system (e.g. Windows).

Your C library will also have to support `getopt_long` via `getopt.h`.

## Downloads

The latest downloads can be found here:
<https://github.com/vapier/nosig/releases>

## Contact

Please use the [issue tracker][tracker] to contact us for bugs, questions, etc...

Author:
<a href="mailto:vapier@gmail.com">Mike Frysinger</a>


[C11]: https://en.wikipedia.org/wiki/C11_(C_standard_revision)
[POSIX-2008]: https://pubs.opengroup.org/onlinepubs/9699919799.2008edition/
[tracker]: https://github.com/vapier/nosig/issues
