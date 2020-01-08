#!/usr/bin/env python3
# Written by Mike Frysinger <vapier@gmail.com>
# Released into the public domain.

# pylint: disable=locally-disabled
# pylint: disable=too-few-public-methods
# pylint: disable=missing-docstring

"""Filter to improve the man->markdown conversion with pandoc.

$ pandoc -r man -w gfm -F ./pandoc-filter.py -r man -w gfm nosig.1 > man.md
"""

import re

from pandocfilters import *  # pylint: disable=wildcard-import,unused-wildcard-import


# Alias for stub attributes.
NoAttrs = attributes({})


def ghanchor(text):
    """Generate anchor link that GitHub pages use."""
    return '#' + re.sub(r'[()/]', '', text.lower().replace(' ', '-'))


def NewLink(text, url):
    """Convenience method for constructing new Link objects."""
    if not isinstance(text, list):
        if not isinstance(text, dict):
            text = Str(text)
        text = [text]
    return Link(NoAttrs, text, [url, ''])


class ActionVisitor:
    """Base class to implement visitor pattern as an action.

    Classes derive from this implement visit_<element> methods.
    e.g. visit_str() for Str() elements.
    """

    def __call__(self, key, value, format, meta):
        method_name = 'visit_' + key.lower()
        func = getattr(self, method_name, None)
        if func:
            return func(key, value)


class AutoLinkUris(ActionVisitor):
    """Automatically link URIs."""

    def __init__(self):
        self.relinked = False

    def visit_str(self, key, value):
        if value.startswith('http'):
            # When we create a new Str node, we'll get called right away for it.
            # Ignore the next Str call with our content.
            if self.relinked == value:
                self.relinked = None
                return
            self.relinked = value
            return NewLink(value, value)


class AutoLinkMans(ActionVisitor):
    """Automatically link references to other manpages."""

    @staticmethod
    def linkman7(sect, page):
        return 'http://man7.org/linux/man-pages/man%(sect)s/%(page)s.%(sect)s.html' % {
            'sect': sect,
            'page': page,
        }

    def visit_para(self, key, value):
        # NB: We use paragraphs because we need to look for consecutive nodes:
        # Strong(Str("nohup")) Str("(1)")
        for i, ele in enumerate(value):
            if ele['t'] == 'Strong':
                if i + 1 < len(value):
                    next_ele = value[i + 1]
                    if next_ele['t'] == 'Str':
                        m = re.match(r'\(([0-9])\)(.*)', next_ele['c'])
                        if m:
                            page = ele['c'][0]['c']
                            sect = m.group(1)
                            rem = m.group(2)
                            text = [Strong([Str(page)]), Str('(' + sect + ')')]
                            url = self.linkman7(sect, page)
                            new_eles = [NewLink(text, url)]
                            if rem:
                                new_eles.append(Str(rem))
                            value[:] = value[0:i] + new_eles + value[i + 2:]


class AutoLinkSections(ActionVisitor):
    """Automatically link references to other sections in the page."""

    def __init__(self, get_toc):
        self.get_toc = get_toc

    def visit_strong(self, key, value):
        text = stringify(value)
        if text in self.get_toc.sections:
            value[:] = [NewLink(text, ghanchor(text))]


class EscapeDashes(ActionVisitor):
    """Restore \- to dashes that pandoc currently strips.

    https://github.com/jgm/pandoc/issues/6041
    """

    def visit_strong(self, key, value):
        for i, ele in enumerate(value):
            if ele['t'] == 'Str' and ele['c'].startswith('-'):
                text = ele['c'].replace('-', r'\-')
                value[i] = RawInline('markdown', text)


class ConvertNameSectionToTitle(ActionVisitor):
    """Convert first NAME header to a title for the whole page.

    The .TH doesn't seem to be handled well, so we have to fake it.
    Plus the .SH NAME is a bit redundant.
    """

    def __init__(self, get_toc):
        self.header = None
        self.done = False
        self.get_toc = get_toc

    def visit_header(self, key, value):
        """Grab the first header.

        We'll save a reference to the object so we can modify it later on.
        """
        if self.done or self.header:
            return

        # Sanity check this is the first header as we expect.
        assert value[0] == 1
        assert stringify(value[2]) == 'NAME'

        self.header = Header(*value)
        return self.header

    def visit_para(self, key, value):
        """Rewrite the into paragraph.

        We'll rip the existing text into the title, and then insert the TOC.
        """
        if self.done:
            return

        # This turns "nosig - foo" into "nosig(1): foo" for the title.
        eles = stringify(value).split()
        eles[0] += '(1):'
        eles.pop(1)
        text = ' '.join(eles)
        self.done = True
        self.header['c'][2] = [Str(text)]

        # Replace the paragraph with the TOC.
        return self.get_toc.render()


class TocNode:
    """Class to hold a header for the TOC."""

    def __init__(self, parent, level, text):
        self.level = level
        self.text = text
        self.parent = parent
        self.children = []

    def append(self, node):
        """Append a node to this one."""
        self.children.append(node)

    def render(self):
        """Turn the current node & its children into the TOC content."""
        eles = []
        if self.text:
            eles.append(Plain([NewLink(self.text, ghanchor(self.text))]))
        eles += [x.render() for x in self.children]
        return BulletList([eles]) if self.text else eles


class GatherToc(ActionVisitor):
    """Gather all the headers for a TOC.

    This won't do any mutation of the headers -- we expect other code to use the
    data we gathered here to insert the TOC.
    """

    def __init__(self):
        self.root = TocNode(None, 0, None)
        self.curr = self.root
        # All the sections we've seen so code can look them up quickly without
        # having to walk the whole graph.
        self.sections = set()

    def append(self, level, text):
        """Append a new node to the TOC."""
        if level > self.curr.level:
            # Append a child.
            node = TocNode(self.curr, level, text)
            self.curr.append(node)
        else:
            # Walk up until we find a parent at a higher level.
            parent = self.curr.parent
            while level <= parent.level:
                parent = parent.parent
            node = TocNode(parent, level, text)
            parent.append(node)
        self.curr = node

    def render(self):
        return self.root.render()

    def visit_header(self, key, value):
        """Add each header to the TOC."""
        level, _, text = value
        text = stringify(text)
        # A bit of a hack: Skip the NAME header as we know we'll be rewriting
        # that into a title section and we don't want it in the TOC.
        if text == 'NAME':
            return

        value[0] = level = level + 1
        if text == text.upper():
            text = text.title()
            value[2] = [Str(text)]

        assert text not in self.sections, 'Duplicate section "%s"!?' % (text,)
        self.sections.add(text)
        self.append(level, text)


if __name__ == '__main__':
    gather_toc = GatherToc()
    toJSONFilters([
        AutoLinkUris(),
        AutoLinkMans(),
        EscapeDashes(),
        gather_toc,
        AutoLinkSections(gather_toc),
        ConvertNameSectionToTitle(gather_toc),
    ])
