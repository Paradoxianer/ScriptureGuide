# Changelog

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

- No Hebrew Strong's dictionary is available from the module list used
  here, so Old Testament Strong's numbers cannot be looked up regardless
  of this release. Install `StrongsHebrew` if your repository offers it.
- `ScriptureGuide/tests` only builds when the application has been built
  first. Its Makefile reaches into `../` for sources, which puts `..` on
  make's VPATH — and VPATH applies to targets too, so make answers a
  request for its own `objects.../foo.o` with the application's copy one
  level up, calls it up to date, and compiles nothing. The tests
  themselves pass.
