# Roadmap

Where ScriptureGuide goes after 1.2.2. Ordered by what unlocks what, not
by wishlist size. Each entry says what it needs and what is already known
about the ground it stands on.

## The next thing: marking passages

Bookmarks and highlighting are the priority. They look like two features
and are mostly one: **user data attached to a verse range, kept
somewhere, listed, and jumped to.** Building them in the right order
means the second one is small.

Today the app has three separate places for user data, and none of them
fits:

| what | where |
|---|---|
| Notes | a SWORD module of their own (`PersonalNotesModule`) |
| Column layout | a `BMessage` in the settings file |
| Navigation history | memory only, gone on quit |

So the first question is where marks live. Notes are per-verse *text* and
SWORD handles them well; a highlight is a colour on a range, and a
bookmark is a name on a reference. Neither is text, and neither belongs
in a Bible module.

### 1. Bookmarks — [#39](https://github.com/Paradoxianer/ScriptureGuide/issues/39)

The smaller of the two, and worth doing first because it settles the
storage question and produces the list UI that highlighting then reuses.

Most of the machinery exists: `ChainKey(ActiveColumn())` already answers
"where am I", `JumpToKey()` already answers "take me there", and the
navigation history added in 1.2.0 uses both. A bookmark is that pair made
persistent and given a name.

- Store: one file, app-wide rather than per window. A bookmark you can
  only reach from the window that made it is a worse bookmark. This
  answers the issue's first open question.
- Create: Navigation menu, beside Back/Forward, with a shortcut.
- Reach: a submenu of saved bookmarks; clicking one navigates the active
  group.
- Manage: rename and delete. A small window rather than a context menu,
  because the same window becomes the annotations list in step 3.

Open, and worth deciding before starting: does a bookmark remember only
the reference, or also which translations were open? The second is more
useful and more surprising — restoring a bookmark would rearrange your
columns.

Risk: low. Almost entirely new code, no changes to the text engine.

### 2. Highlighting verses — needs an issue

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

### 3. One list of everything marked

Bookmarks, highlights, and verses that have notes, in a single window —
HaikuArchives#29. Only worth building after 1 and 2, and cheap once they
exist, because it is a view over the store rather than a new mechanism.

## After that

Roughly in the order I would take them.

- **[#38](https://github.com/Paradoxianer/ScriptureGuide/issues/38)
  Dropping Bible text into a note inserts a cross-reference link.** Small:
  the drop path and the reference-link rendering both exist, they just
  don't meet.
- **[#32](https://github.com/Paradoxianer/ScriptureGuide/issues/32)
  Automatic reference recognition.** Partly built —
  `FindReferencesInText()` already finds references in commentary and note
  text. This is mostly about where else to apply it.
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

## Done in 1.2.x, worth closing

[#41](https://github.com/Paradoxianer/ScriptureGuide/issues/41) (sort by
module type), [#37](https://github.com/Paradoxianer/ScriptureGuide/issues/37)
(multiple selection),
[#42](https://github.com/Paradoxianer/ScriptureGuide/issues/42) (two-panel
install UI),
[#24](https://github.com/Paradoxianer/ScriptureGuide/issues/24) (saved
position), [#40](https://github.com/Paradoxianer/ScriptureGuide/issues/40)
(illustrated manual).

[#33](https://github.com/Paradoxianer/ScriptureGuide/issues/33) and
[#12](https://github.com/Paradoxianer/ScriptureGuide/issues/12) look
resolved by column groups but are worth a deliberate check first: #33's
title claims notes are lost when a second notes column is added, which
would be data loss, while its body describes the feature that now exists.
