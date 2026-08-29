# Roadmap

Where ScriptureGuide goes after 1.3.0. Ordered by what unlocks what, not
by wishlist size. Each entry says what it needs and what is already known
about the ground it stands on.

## The next thing: marking passages

Bookmarks and highlighting were the priority, as two features that are
mostly one: **user data attached to a verse range, kept somewhere,
listed, and jumped to.**

Bookmarks shipped in 1.3.0, in a shape richer than [#39](https://github.com/Paradoxianer/ScriptureGuide/issues/39)
(now closed) originally proposed: named, orderable *collections* of
references — a standalone Verse List window instead of a single flat
submenu, one bookmark file per reference rather than one entry in an
app-wide list — because that is what a real user's own workflow
described (see #47, #55). It settled the storage question the same way
#39 would have: a bookmark records a reference and jumps to it via the
existing `JumpToKey()` path, just organized into folders instead of one
list.

Highlighting is next, and its design is unaffected by that shape change:

### Highlighting verses — needs an issue

There is no issue for this yet on this repository; the nearest is
HaikuArchives#8 ("mark text and/or verses, maybe in different colours")
and HaikuArchives#29 (a window listing what has been marked).

The text engine is further along than it looks. `CharacterStyle` already
carries `SetBackgroundColor()`/`BackgroundColor()`; what is missing is
that `ParagraphLayout::_DrawSpan()` never reads it — there is a literal
`// TODO: Implement other style properties` at exactly that spot. The
geometry needed to fill a rectangle behind a span is right there:
`LineInfo` knows each line's `y`, `height` and ascent, and every glyph
knows its `x`.

So this is a contained job, not a rewrite:

- Draw the background in `_DrawSpan()`.
- Give highlighted verses a `CharacterStyle` with a background colour
  when the document is built — the same mechanism Strong's numbers and
  cross-references already use to style their own spans.
- Store (range, colour) in whatever step 1 established.

**Start with whole verses, not arbitrary text.** Cross-column selection
already snaps to whole verses for a reason: translations disagree about
where a sentence ends. A verse-level highlight is meaningful in every
column at once; a character-level one is meaningful in exactly the
translation it was drawn in. Sub-verse highlighting is a reasonable
second step, once it is clear people want it.

### Then: one list of everything marked

Bookmarks, highlights, and verses that have notes, in a single window —
HaikuArchives#29. Only worth building once highlighting exists, and cheap
once it does, because it is a view over the store rather than a new
mechanism.

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

- **The tests only build after the application does.** Their makefile
  reaches into `../` for sources, which puts `..` on make's VPATH — and
  VPATH applies to targets, so make finds the application's objects and
  compiles none of its own. Giving the tests a distinct object directory
  would fix it.
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
