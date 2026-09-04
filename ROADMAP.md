# Roadmap

Where ScriptureGuide goes after 1.3.0. Ordered by what unlocks what, not
by wishlist size. Each entry says what it needs and what is already known
about the ground it stands on.

## The next thing: a marked verse list, shown while reading

[#92](https://github.com/Paradoxianer/ScriptureGuide/issues/92) and
[#105](https://github.com/Paradoxianer/ScriptureGuide/issues/105) are
one feature approached from two sides: #92 wants an ordinary verse list
to show its members in the reading pane, #105 wants a colour beside the
list's name for it to be shown in. They are cross-linked and should be
settled together.

Most of the work is already done. `HighlightStore::ForDocument()`
decides per bookmark: no span means verse-wide in every column, a span
means those characters in that translation. So a list needs a colour --
on the collection folder, one attribute -- and a rule for *when* it
shows. The narrower answer is "while that list is selected", which
needs no per-list state and keeps Options > Highlight Colours about
highlight categories; the selected collection is already persisted.

One real gap: "Add to Verse List" drops the selection's span, so a list
of partial-verse entries (every occurrence of a name, marked on the
name itself) cannot be created from the UI yet, though it would render
correctly if it existed.

After that,
[#104](https://github.com/Paradoxianer/ScriptureGuide/issues/104) --
every bookmark carrying a given tag, across all collections -- is the
remaining "show me everything marked X" question, with the
BQuery-vs-Tracker choice still open.

**On upstream issues:** HaikuArchives#29 ("a window listing what has
been marked") is treated as obsolete rather than pending. Verse lists
answered it in a better shape than it asked for -- named collections
that are real folders, rather than one flat listing window. Planning
follows this repository's own tracker; the HaikuArchives issues predate
column groups, verse lists and highlighting, and mostly describe a
program that no longer exists.

## Done in 1.4.0

Highlighting, end to end -- see the changelog. Worth recording that it
did **not** land the way this roadmap predicted: the plan here was
"start with whole verses, not arbitrary text", and that was the wrong
call. Free selection within one translation is what marking passages
actually means when you do it; whole-verse marks are the concession the
columns need, not the starting point. The cross-column drag became the
switch between the two, which made the verse-level case a gesture
rather than a mode.

Also shipped alongside it: a single menu for everything that acts on a
selection, and three bugs found by building with stricter warnings --
including a hang on Cmd+C in the search window that no menu item points
at.

## Done in 1.3.1

[#68](https://github.com/Paradoxianer/ScriptureGuide/issues/68) — the
whole reason verse lists took the shape they did: a real end user's
existing library files, a plain-text export from the source program
(QuickVerse/Bible Research Systems), one reference per line, no header.
File > Import Text List... reads that straight into a new collection.
The program's own *binary* `.lib` format stays unreverse-engineered, but
isn't blocking anything now that the text-export path works.

Also landed alongside it, from the real end user's own testing:
bookmark storage now reads reference/versification/locale from
attributes first (Tracker-visible columns), falling back to -- and
self-healing from -- the plain-text mirror only when those attributes
are missing.

## Done in 1.3.6

Tags became usable rather than merely stored
([#57](https://github.com/Paradoxianer/ScriptureGuide/issues/57),
closed): a row's Tags cell assigns them, a dropdown filters by them, and
both read the tag list back out of the bookmark files themselves rather
than a registry alongside them. The cross-library "everything tagged X"
half is split out as
[#104](https://github.com/Paradoxianer/ScriptureGuide/issues/104), with
the BQuery-vs-Tracker choice still open.

[#101](https://github.com/Paradoxianer/ScriptureGuide/issues/101) —
`SG:reference`/`SG:position` are editable in Tracker now, with the
Bible-order sort key recomputed on read so it cannot go stale behind an
edit made there.

Also: the Verse List window reopens with the collection and window frame
it was left at, and two bugs found while testing the above — the
description field burying its own text under empty paragraphs
accumulating on every keystroke, and "Custom Order" not actually
restoring the user's own row order.

[#56](https://github.com/Paradoxianer/ScriptureGuide/issues/56) is
closed as partly shipped, partly declined — see the issue.

## Done in 1.3.5

[#67](https://github.com/Paradoxianer/ScriptureGuide/issues/67)/[#50](https://github.com/Paradoxianer/ScriptureGuide/issues/50)
— a reference can now be added to a list from wherever it's
encountered, not only from inside the verse-list window itself:
right-click a verse (or selection) in the reading pane, or a
recognized cross-reference in Notes/Commentary, for "Add to Verse
List ▸".

[#56](https://github.com/Paradoxianer/ScriptureGuide/issues/56) landed
partly: *Go to List* can create a new sub-collection inline
("New sub-collection here…"). The reference-creation half of the
original issue was tried and then deliberately dropped — mixing
navigation and content creation in the same menu didn't hold up once
it was actually in front of a real user.

[#32](https://github.com/Paradoxianer/ScriptureGuide/issues/32)
(automatic reference recognition) is now applied to the verse list's
own description field too, not just Notes/Commentary — and the
recognizer itself got considerably more robust doing it: German
numbered books with their period ("1. Mose") and accented book names
("Matthäus") were both silently unrecognized before this, an ASCII/
punctuation gap in the matching pattern that had nothing to do with
which surface used it.

The reference column as a real table (`BColumnListView`, no tracked
issue) and [#99](https://github.com/Paradoxianer/ScriptureGuide/issues/99)
(Show in Tracker) also shipped.

[#59](https://github.com/Paradoxianer/ScriptureGuide/issues/59)
(converting the old one-file-per-list format) is off the table — it
was only ever used locally during development, never by a real user,
so there is nothing to migrate.

## After that

Roughly in the order I would take them.

- **[#38](https://github.com/Paradoxianer/ScriptureGuide/issues/38)
  Dropping Bible text into a note inserts a cross-reference link.** Small:
  the drop path and the reference-link rendering both exist, they just
  don't meet.
- **[#18](https://github.com/Paradoxianer/ScriptureGuide/issues/18) HIG
  audit.** Marked high priority and deserves it, but it is a sweep rather
  than a feature. Best done when the UI stops moving — the Book Manager
  and the notes column both changed shape in 1.2.x.
- **[#34](https://github.com/Paradoxianer/ScriptureGuide/issues/34) Greek
  and Hebrew fonts.** Currently a manual step in the manual. Bundling or
  depending on a font would remove it.
- **[#35](https://github.com/Paradoxianer/ScriptureGuide/issues/35) A BFS
  file type with Bible-reference attributes.** The most Haiku-native idea
  in the tracker: dropped selections become queryable files. Note it
  overlaps with bookmark storage — if bookmarks were such files, Tracker
  could list and query them for free. Worth thinking about *before*
  finalising step 1's storage, even if the answer is no.
- **[#16](https://github.com/Paradoxianer/ScriptureGuide/issues/16)
  Evaluate a newer SWORD.** Low priority until something needs it.
- **[#43](https://github.com/Paradoxianer/ScriptureGuide/issues/43) Audio
  modules.** Open question rather than planned work.

## Debt worth paying

Small, known, and each one already documented where it bites:

- **Dictionary choice ignores the interface language.** With a German and
  an English Strong's dictionary installed, whichever comes first in the
  module list wins.
- **The manual exists in English and German only.** Structure and pictures
  are in place; Spanish, French and Dutch are a translation away. Croatian,
  Romanian and Russian want a native speaker.
- **Croatian, Romanian and Russian interface translations** were extended
  from existing terminology but never checked by a native speaker. One
  known suspect: ScriptureGuide translates Quit as "Isključi", literally
  "switch off".

## Not planned

- **[#17](https://github.com/Paradoxianer/ScriptureGuide/issues/17)
  HaikuPorts recipe.** HaikuPorts does not accept contributions produced
  with AI agents. Updating the recipe is for a human maintainer outside
  this workflow — see CLAUDE.md.
- **Integrating the Book Manager into the main window**
  ([#25](https://github.com/Paradoxianer/ScriptureGuide/issues/25),
  closed). It is already reachable from Program → Book Manager, and module
  management is a rare, self-contained task that needs nothing from the
  reading pane.

## Done in 1.3.0

Verse lists, the whole arc: [#47](https://github.com/Paradoxianer/ScriptureGuide/issues/47)
(standalone window), [#55](https://github.com/Paradoxianer/ScriptureGuide/issues/55)
(one bookmark file per reference), [#60](https://github.com/Paradoxianer/ScriptureGuide/issues/60)/[#61](https://github.com/Paradoxianer/ScriptureGuide/issues/61)/[#64](https://github.com/Paradoxianer/ScriptureGuide/issues/64)/[#66](https://github.com/Paradoxianer/ScriptureGuide/issues/66)
(window polish), [#73](https://github.com/Paradoxianer/ScriptureGuide/issues/73)
(rename in place), [#78](https://github.com/Paradoxianer/ScriptureGuide/issues/78)
(nested Go to List), [#79](https://github.com/Paradoxianer/ScriptureGuide/issues/79)
(live filesystem sync). Also [#62](https://github.com/Paradoxianer/ScriptureGuide/issues/62)
and [#65](https://github.com/Paradoxianer/ScriptureGuide/issues/65), found
along the way.

Separately, on `master`: notes now live in SWORD's `Personal` commentary
rather than a bespoke `RawCom` ([#45](https://github.com/Paradoxianer/ScriptureGuide/issues/45),
[#46](https://github.com/Paradoxianer/ScriptureGuide/issues/46)), making
them interchangeable with BibleTime and Xiphos.

[#12](https://github.com/Paradoxianer/ScriptureGuide/issues/12) is left
open on purpose — it predates column groups and needs a deliberate look,
not an assumption that column groups already cover it.
