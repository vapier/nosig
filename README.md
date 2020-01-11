# nosig web site

This branch uses [GitHub pages] to host a website at
<https://vapier.github.io/nosig>.

## Man page

The man.md is generated using [pandoc] and a custom Python filter.
Your distro should have the packages available for it, or you can install
prebuilts & use pip for the Python bindings.

You'll want to manually copy the man page from the master branch first.

See the [pandoc-filter.py] file for more details on actually running it.


[GitHub pages]: https://pages.github.com/
[pandoc]: https://pandoc.org/
[pandoc-filter.py]: ./pandoc-filter.py
