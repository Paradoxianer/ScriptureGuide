# Roadmap

Where ScriptureGuide goes after 1.4.0. Ordered by what unlocks what, not
by wishlist size. Each entry says what it needs and what is already known
about the ground it stands on.

Version targets now live as
[GitHub Milestones](https://github.com/Paradoxianer/ScriptureGuide/milestones)
-- every open issue carries one. This file keeps the reasoning behind
the grouping; the milestone itself is the source of truth for which
issue is in which one, so if the two ever disagree, trust GitHub and
fix this file.

## 1.5.0 -- word study, and the UI settling down

- [#83](https://github.com/Paradoxianer/ScriptureGuide/issues/83) Given
  a Strong's-tagged word, find every other verse using the same one --
  the "Bible Word Study" a dictionary click can't do yet. Needs a new
  search mode keyed by Strong's number rather than plain text; confirmed
  `SwordBackend` has nothing like that today.
- [#106](https://github.com/Paradoxianer/ScriptureGuide/issues/106)
  Highlight same-word occurrences within the visible chapter --
  independent of #83 (it needs `StrongsNumberAt()`, which already
  exists, not the new search), filed from the same csv-bibel.de
  reference.
- [#32](https://github.com/Paradoxianer/ScriptureGuide/issues/32)
  Reference recognition works in notes, commentary and verse-list
  descriptions already -- the one place left is dictionary entries
  themselves. Small, standalone remainder.
- [#18](https://github.com/Paradoxianer/ScriptureGuide/issues/18) HIG
  audit. 1.4.0 settled the selection menu into one popup and removed
  the borderless palette window -- the reading pane is not mid-change
  for the first time in several releases, which is what this was
  waiting for.

## 1.6.0 -- a verse list you can read by, not just manage

- [#92](https://github.com/Paradoxianer/ScriptureGuide/issues/92) /
  [#105](https://github.com/Paradoxianer/ScriptureGuide/issues/105) A
  verse list shown in the reading pane, and the colour it is shown in.
  One feature from two sides, cross-linked, with the design decisions
  (colour on the collection folder, shown while selected) already
  written up in #105's own comments. One real gap: "Add to Verse List"
  drops the selection's span, so a list of partial-verse entries can't
  be created from the UI yet.
- [#107](https://github.com/Paradoxianer/ScriptureGuide/issues/107)
  Word-distribution statistics (which books, which translations) --
  depends on #83's search existing first.
- [#87](https://github.com/Paradoxianer/ScriptureGuide/issues/87) Print
  a verse list, or export as PDF.
- [#103](https://github.com/Paradoxianer/ScriptureGuide/issues/103)
  Markup parser for Notes and description fields.

## 1.7.0 -- architecture pass and polish

- [#51](https://github.com/Paradoxianer/ScriptureGuide/issues/51) Band
  refinements -- cascading Book/Chapter/Verse picker, compact
  Tracker-style menus, a bookmark button for the list menu.
- [#35](https://github.com/Paradoxianer/ScriptureGuide/issues/35) A BFS
  file type for dropped selections. Its first half is effectively done
  -- bookmarks and highlights already register a MIME type with
  `SetAttrInfo()` and are queryable in Tracker. What remains is the
  unverified half: writing attributes onto the file Tracker itself
  creates on a drop, which needs the asynchronous `B_COPY_TARGET`
  protocol and a real prototype rather than a guess.
- [#77](https://github.com/Paradoxianer/ScriptureGuide/issues/77)
  Search window: allow a narrower minimum width.
- [#34](https://github.com/Paradoxianer/ScriptureGuide/issues/34) Greek
  and Hebrew fonts. Currently a manual step in the manual. Bundling or
  depending on a font would remove it.
- [#96](https://github.com/Paradoxianer/ScriptureGuide/issues/96)
  `Description.txt` as its own file type with ScriptureGuide as the
  preferred app.

## Backlog -- real ideas, not yet scheduled

Genuinely worth doing eventually, or worth having on record, but none
of them earn a version number yet -- either unscoped, low priority
until something needs them, or blocked on a question this project
can't answer by itself.

- **Blocked on an open question:**
  [#91](https://github.com/Paradoxianer/ScriptureGuide/issues/91)
  patristic citation index (BKV's own licensing is unresolved),
  [#108](https://github.com/Paradoxianer/ScriptureGuide/issues/108)
  person/pronoun tagging (no known free dataset yet),
  [#109](https://github.com/Paradoxianer/ScriptureGuide/issues/109)
  paid/locked modules (SWORD's own cipher-key mechanism is real and
  confirmed, but no active vendor is confirmed, and `ScriptureGuideManager`
  only speaks to one hardcoded free repository today),
  [#43](https://github.com/Paradoxianer/ScriptureGuide/issues/43) audio
  modules (open question rather than planned work).
- **Low priority until something needs it:**
  [#16](https://github.com/Paradoxianer/ScriptureGuide/issues/16)
  evaluate a newer SWORD,
  [#104](https://github.com/Paradoxianer/ScriptureGuide/issues/104)
  cross-library tag view -- Tracker's own Find already answers "every
  bookmark tagged X" across every collection; building a worse version
  of a query the system already has is not worth it on its own.
- **Bigger, speculative features**, roughly grouped:
  [#69](https://github.com/Paradoxianer/ScriptureGuide/issues/69)
  verse-of-the-day replicant,
  [#88](https://github.com/Paradoxianer/ScriptureGuide/issues/88) /
  [#89](https://github.com/Paradoxianer/ScriptureGuide/issues/89)
  auto-scroll and automatic chapter transition,
  [#70](https://github.com/Paradoxianer/ScriptureGuide/issues/70)
  attach images to a reference or note,
  [#90](https://github.com/Paradoxianer/ScriptureGuide/issues/90) audio
  notes (record your own reading),
  [#71](https://github.com/Paradoxianer/ScriptureGuide/issues/71) map
  support,
  [#74](https://github.com/Paradoxianer/ScriptureGuide/issues/74) /
  [#75](https://github.com/Paradoxianer/ScriptureGuide/issues/75) /
  [#86](https://github.com/Paradoxianer/ScriptureGuide/issues/86) book
  outlines, a history timeline, and toggle-able book introductions,
  [#76](https://github.com/Paradoxianer/ScriptureGuide/issues/76) a
  library of public-domain Christian literature,
  [#82](https://github.com/Paradoxianer/ScriptureGuide/issues/82)
  parallel/harmonized display of overlapping accounts,
  [#85](https://github.com/Paradoxianer/ScriptureGuide/issues/85)
  BibleSync co-navigation with other SWORD apps.

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

[#12](https://github.com/Paradoxianer/ScriptureGuide/issues/12) got that
deliberate look and is closed: column groups cover it, at a more general
grain than the issue originally asked for (a group's independent
scrolling/alignment/navigation rather than a single column's) —
confirmed against `_Realign()`/`SetColumnLinked()` directly, not assumed.
