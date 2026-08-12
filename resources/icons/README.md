# Icons

## What ships

The icons the applications actually carry are **compiled HVIF**, embedded
directly in each app's `.rdef` as its `vector_icon` resource — see
`ScriptureGuide/ScriptureGuide.rdef` and
`ScriptureGuideManager/ScriptureGuideManager.rdef` (819 bytes each, added
in `6a98f98`). Nothing in this directory is read at build time.

## What is kept here, and why

`*.icon` are the **editable Icon-O-Matic sources**. Without them the
compiled icons cannot be changed again — HVIF is a one-way export, so
losing the source means redrawing from scratch.

Note the format, because the extension is easy to get wrong: an
Icon-O-Matic native file is a flattened `BMessage` and starts with
`IMSG`. Compiled HVIF starts with `ncif`. `Bookmark.hvif` really is HVIF
(174 bytes); the two `.icon` files really are sources (13.7 KB each), and
were previously misnamed `.hvif` — embedding one of those as a
`vector_icon` would produce garbage, not an icon.

## Changing an icon

1. Edit the `.icon` file in Icon-O-Matic.
2. Export as **rdef** (File → Export, "rdef" format).
3. Replace the `resource vector_icon { … }` block in the app's `.rdef`
   with the exported one.
4. Save the `.icon` file back here, so the next person can edit it too.
