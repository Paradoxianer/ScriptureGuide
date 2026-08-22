# Changelog

## 1.3.0 (test release)

### Verse lists

A standalone window for building named collections of references — a
reading plan, a topical study, whatever grouping makes sense — separate
from the reading pane: clicking an entry navigates the active chain
straight to that one verse, rather than rendering the whole collection as
a composite document. File menu (New, Open, Close, Save, Save As, Delete)
and Edit menu (move up/down, remove a row, description) cover day-to-day
list management.

Storage is one small plain-text file per reference, not one file per
list: a collection is just a folder. That buys a few things for free —
Tracker becomes a browser for a collection (sortable by Position, Code
and Tags, which are ordinary BFS attributes), copying a reference between
collections is a file copy, and double-clicking a bookmark file in
Tracker navigates the active chain straight to it. Each bookmark records
the locale it was written in, so a German "Johannes 3:16" round-trips
correctly regardless of what locale later opens it.

- **Rename in place.** Double-clicking a collection's name renames its
  own folder, unlike Save As, which duplicates it elsewhere.
- **Nested collections.** *Go to List* cascades into sub-collections at
  any depth, not just the top level.
- **Live sync.** Changes made outside the app — in Tracker, from another
  instance, by hand — appear without reopening the window, both for which
  collections exist and for the currently open one's own contents.
- Storage path simplified to `settings/scriptureguide/verselists/`; a
  stray `library` segment from an earlier design is gone (nothing had
  shipped bookmark files under the old path yet).

### Search

- The search window now offers every installed Bible and commentary, not
  only the ones currently open as columns.
- Fixed: double-clicking a search result opened a new window instead of
  navigating the active chain.
- Fixed: dragging multiple selected search results onto a verse list did
  nothing.

### Fixed

- Parallel columns of different versifications could silently drop
  verses.
- Dragging a reading-pane selection produced the wrong drag bitmap,
  including under German locale.

## 1.2.2 (test release)

Same programs as 1.2.1 -- only the documentation differs, so 1.2.1 was
never published as a release.

- The manual gained the two pictures it was describing without showing:
  the column header's drop-down, which is how a column is added or turned
  into notes, and the search window.
- The README is now an introduction to the project rather than a
  paragraph and a bullet list.

## 1.2.1

### Fixed

- **Strong's numbers showed the wrong entry.** Clicking any Greek word
  could produce the article for "alpha". `Feature=GreekDef` in a module's
  configuration means "this defines Greek words", not "this is keyed by
  Strong's number" -- the Dodson Greek-English Lexicon declares it and is
  keyed by the Greek lemma, so asking it for 2316 did not fail: SWORD
  snapped to the nearest key and returned a valid entry for something
  else. The lookup now checks which key the module actually landed on,
  and a lexicon that cannot answer Strong's lookups no longer makes
  numbers render as links.
- The app no longer creates an empty legacy `Notes.txt` on every start.
  Notes have been a SWORD module for some time; nothing had read that
  file since, and it was written to a differently-cased path that had
  nothing to do with where notes are stored. Existing files are left
  alone.

### Documentation

The manual was rewritten. Its pictures were seven full window shots at
around 150 KB each; they are now crops of the part being described, taken
against the current build -- 1.3 MB down to 156 KB. The text had fallen
behind further than the pictures: it described an Apply button in the
Book Manager and notes as one small field per verse, and said nothing
about column groups, back/forward, the dictionary window or Copy
Comparison. A German version is included, and menus are written out as
text rather than screenshotted so they translate with the page.

It also documented the wrong location for personal notes -- the path the
empty legacy file went to, not where anything is stored.

### Packaging

`package.sh` can now build from a tag on a machine with no checkout:

    sh package.sh v1.2.2

It installs the build dependencies, fetches the source, builds both apps,
writes the .hpkg and removes what it created. Useful on a Haiku version
the attached package was not built for.

The editable Icon-O-Matic icon sources are in the repository now, under
`resources/icons`. The compiled icons were already there, embedded in
each app's .rdef; the sources existed only on one machine.

## 1.2.0 (test release)

Both ScriptureGuide and the Book Manager. Versions bumped together; they
ship as one package.

### Notes column

The notes column is now edited directly, in place. Clicking puts the
caret where you clicked, text can be selected and copied across verses,
the arrow keys walk from one verse's note into the next, and a note's row
grows as you type. Return inserts a line break inside the note rather
than being refused.

This replaces a design where clicking a verse opened a separate one-verse
editor on top of a read-only display. That design was chosen on the
assumption that a directly editable column was what made switching
chapters slow. Profiling showed the cost was elsewhere (see Performance),
and the direct design is the cheaper one anyway — one view and one editor
per column regardless of how long the chapter is.

### Navigation

- **Back and Forward**, in the Navigation menu and as toolbar buttons
  using Tracker's own arrow icons. Following a cross-reference navigates
  the column group you were reading in, so without a way back a link cost
  you your place. Covers every kind of navigation, not just links.
- The position the app reopens at is now the one you were actually
  reading. It used to save a key that nothing kept in sync with the
  reading pane, so quitting on Psalm 119 could reopen on Genesis 1.

### Strong's numbers

Numbers are only drawn as links when a dictionary that could resolve them
is installed. With only a Greek dictionary present, every Strong's-tagged
word in the whole Old Testament used to look clickable and lead nowhere.
When a lookup does fail, the message now distinguishes "that dictionary
isn't installed" (and names the module to install) from "no entry for
this number".

Strong's numbers need the matching dictionary installed to be of any use:
`StrongsGreek` for the New Testament, `StrongsHebrew` for the Old. Both
are in the Book Manager's list. Until one is installed its numbers simply
render as ordinary text.

### Fixes

- A column group could be left scrolled somewhere it could not scroll
  back from, when jumping to a verse in a chapter short enough to fit on
  screen.
- A column kept its neighbours' verse spacing after being disconnected
  from them, and kept it even after navigating to another book.
- The leftmost column's scrollbar did not work when it was in a group of
  its own.

### Performance

Chapter switching with three columns open took 500–1500 ms. Two causes,
both fixed:

- Alignment rebuilt every document from scratch just to adjust paragraph
  spacing — re-fetching the text and re-detecting Strong's numbers and
  cross-references three times per switch.
- Every keystroke re-shaped the glyphs of every paragraph in the
  document; in a notes column on Psalm 119 that was all 176 of them.

### Book Manager

- **Two panels**: available modules on the left, installed on the right,
  arrows between them, description and progress spanning the width
  beneath both. Moving a module starts the work — there is no Apply step.
  Removal asks once first, since it deletes downloaded data.
- **Module type** column (Bible, Commentary, Dictionary, Generic Book),
  sortable like every other column.
- **Multiple selection**: mark a batch at once instead of double-clicking
  each row.
- Fixed: the manager aborted on startup when its package list was
  missing, before its window ever appeared.
- Fixed: modules could appear twice, once missing their details.

### Translations

Every catalog in both apps is now complete or nearly so. ScriptureGuide's
translations had drifted well behind the source — six of its eight
languages were missing a shared batch that had never been translated in
any of them, and Spanish had 27 of 39 entries still in English. The Book
Manager had no localization at all; it now covers the same eight
languages.

Croatian, Romanian and Russian were extended from terminology earlier
contributors established, but have not been checked by a native speaker.

### For developers

Diagnostic logging is now off unless `SG_DEBUG` is set:

    SG_DEBUG=1 ./ScriptureGuide

### Packaging

The HaikuPorts recipe is not updated as part of this release. HaikuPorts
does not accept contributions produced with AI agents, and this release
was prepared with one. Updating the recipe is for a human maintainer to
do outside this workflow — see CLAUDE.md.

### Known issues

- `ScriptureGuide/tests` only builds when the application has been built
  first. Its Makefile reaches into `../` for sources, which puts `..` on
  make's VPATH — and VPATH applies to targets too, so make answers a
  request for its own `objects.../foo.o` with the application's copy one
  level up, calls it up to date, and compiles nothing. The tests
  themselves pass.

## 1.1.0

Tagged but never published as a release; its changes reached users with
1.2.0.

### Column groups

Columns can be joined into groups that scroll, align and navigate
together, or split apart so one group sits in a different book from
another. Every column got its own scrollbar, and the full layout --
which modules are open, where the notes column is, which columns are
joined -- is restored on the next start.

Columns can also be reordered by dragging their header, and the notes
column has a real draggable splitter rather than a fixed width.

### Cross-references, Strong's numbers and a dictionary

References in commentary text became clickable. Strong's-tagged words are
highlighted and open a new Lexicon/Dictionary window. Both, plus verse
numbers, can be switched off in the Options menu.

### Copy Comparison

Every open column's text for the current chapter, as a verse-by-verse
table, on the clipboard as plain text, tab-separated, Markdown or HTML.

### Fixes

- A commentary entry spanning several verses inflated the row height once
  per verse it covered.
- Notes and Bible columns drifted apart in height.
- A dangling pointer to the dictionary window could crash the app.
- Adding a column could reorder verses through a VerseKey locale bug, and
  the book menu listed its first book twice.
- Book Manager: package sizes were paired with the wrong names, and an
  uninstall with an unreadable config file could delete more than it
  should.

Both applications also got proper HVIF vector icons, replacing the BeOS
raster ones.
