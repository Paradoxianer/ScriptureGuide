#ifndef VERSE_LIST_WINDOW_H
#define VERSE_LIST_WINDOW_H

#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <String.h>
#include <Window.h>

#include <vector>

#include "TextDocument.h"

#include "parallelbible/BookmarkFile.h"

class BFilePanel;
class BMenu;
class BMenuBar;
class BMenuField;
class BMenuItem;
class BPopUpMenu;
class BScrollView;
class BStringView;
class TextDocumentView;
class VerseListRowListView;

// Local message constants, same convention as LogosSearchWindow.h's
// FIND_*/M_ACTIVATE_WINDOW #defines -- window-local, not app-wide, so
// they live here rather than in constants.h.
#define VLIST_QUIT				'VLqu'
#define VLIST_NEW				'VLnw'
#define VLIST_DELETE			'VLdl'
#define VLIST_MOVE_UP			'VLmu'
#define VLIST_MOVE_DOWN			'VLmd'
// #57 followup: clicking a column header engages BColumnListView's own
// built-in sort, which has no built-in way back out of again --
// clicking the header again only toggles ascending/descending, and
// while a sort column is active, both drag-reorder
// (VerseListRowListView::MessageDropped()'s own AddRow(row, to)) and
// Move Up/Move Down silently stop having any visible effect, since
// BColumnListView re-sorts right back over whatever position they
// just placed a row at. Edit > Custom Order calls
// BColumnListView::ClearSortColumns() -- confirmed live that this
// alone stops FUTURE re-sorting but does not undo the physical
// reorder a header click already performed on fRowList's own rows --
// plus _RebuildRows() to actually put the rows back, since fBookmarks
// (what drag-reorder/Move Up/Move Down/CreateNew() etc. all read and
// write) was never touched by the sort in the first place and still
// holds the real, user-set order.
#define VLIST_CLEAR_SORT		'VLcs'
#define VLIST_NAV_SELECT		'VLns'
#define VLIST_ROW_SELECTED		'VLrs'
#define VLIST_ROW_REORDER		'VLrr'
#define VLIST_DESCRIPTION_CHANGED	'VLdc'
// Double-click on fNameView (#73) -- renames the open collection's own
// folder in place.
#define VLIST_RENAME			'VLrn'
#define VLIST_RENAME_RESULT	'VLrR'
// File > Import Text List... (#68) -- one reference per line, no header,
// same shape as a QuickVerse/WORDsearch-style plain-text export
// (confirmed against a real sample: AARON.TXT, one OSIS-style
// abbreviation like "EXO 4:14" per line). See _ImportTextFile() in the
// .cpp for why this needed no format-specific parsing beyond splitting
// lines -- sword::VerseKey::setText() already accepts these
// abbreviations directly.
#define VLIST_IMPORT_PANEL		'VLip'
#define VLIST_IMPORT_RESULT	'VLir'
// #97: only sent when nothing was open at import time -- the name/
// location prompt's own result, distinct from kNamePromptOK (New Verse
// List's) so the two can't be confused if a prompt somehow outlives its
// own purpose.
#define VLIST_IMPORT_NAME_RESULT	'VLin'
// File > Export Text List... (#102) -- the reverse of Import: writes
// fBookmarks' own references, one per line, no header. The only
// remaining way to get a collection's content out of the managed tree
// now that Open/Save As are gone (#94) -- Go to List covers navigation
// inside the tree, #58 (not built yet) covers copying/moving within it.
#define VLIST_EXPORT_PANEL		'VLxp'
#define VLIST_EXPORT_RESULT	'VLxr'
// Edit > Add Reference... -- drag-and-drop was previously the only way
// to add a reference, and nothing in the window said so. Opens the same
// prompt window Edit Reference does (typed text, not a book/chapter/
// verse picker -- reuses the exact validation the universal Go to /
// Search box already has, see _CreateReference()/_EditReference() in
// the .cpp), just with a different title/pre-filled text and result
// handler.
#define VLIST_ADD_REFERENCE		'VLar'
#define VLIST_ADD_REFERENCE_RESULT	'VLaR'
#define VLIST_EDIT_REFERENCE_RESULT	'VLeR'
// #93: Edit Reference/Remove, both usable from the Edit menu OR the row
// list's own right-click menu (#57's _ShowRowContextMenu()) -- either
// path posts the same message, acting on whichever row(s) are currently
// selected (fRowList->CurrentSelection()) rather than carrying an index
// of their own.
#define VLIST_EDIT_REFERENCE_SELECTED	'VLes'
#define VLIST_REMOVE_SELECTED	'VLrx'
// #58: File > Move/Copy List to... (the whole open collection, including
// any nested sub-collections) and Edit > Move/Copy to... (just the
// currently selected row(s)) -- all four are cascading submenus built by
// PopulateCollectionMenu(), same tree as "Go to List", each carrying the
// chosen destination as "path".
#define VLIST_MOVE_LIST_TO		'VLmt'
#define VLIST_COPY_LIST_TO		'VLct'
#define VLIST_MOVE_ENTRIES_TO	'VLme'
#define VLIST_COPY_ENTRIES_TO	'VLce'
// #99: opens the currently open collection's own folder in a Tracker
// window -- the payoff of #55's own "Tracker becomes a free browser for
// a collection" reasoning, without needing Save As (removed, #94) as a
// detour to get there first.
#define VLIST_SHOW_IN_TRACKER	'VLtk'
// #72: the name/location prompt _AppendDroppedReferences() shows when a
// drop lands with nothing open -- distinct from kNamePromptOK (plain
// "New Verse List…") so a canceled prompt can never be mistaken for one
// from the other path (see fPendingDropMessage's own comment).
#define VLIST_DROP_NAME_RESULT		'VLdn'
// "New sub-collection here..." (Go to List's own trailing item, every
// level including the root) -- carries the clicked submenu's own path as
// "path", fixed (no location picker, it's already implied by which entry
// was clicked). Reuses kNamePromptOK as the prompt's own resultWhat --
// see _StartNewSubCollectionHere() in the .cpp for why that's enough.
#define VLIST_NEW_SUBCOLLECTION_HERE	'VLsh'
// #57 (first slice): a BMenuField above the row list, one item per
// distinct tag found across the open collection's own bookmarks plus a
// leading "All Tags" -- carries the chosen tag as "tag" (absent/empty
// means no filter). Narrows what _RebuildRows() actually adds to
// fRowList; doesn't touch fBookmarks or any file on disk.
#define VLIST_TAG_FILTER		'VLtf'
// #57: a plain click on a row's Tags cell opens a popup listing every
// tag already used anywhere in the open collection, each checkmarked if
// this row carries it -- clicking one toggles it (VLIST_TAG_TOGGLE,
// carrying "tag" plus the bookmark's own "index"). A trailing
// "New Tag..." entry (VLIST_TAG_NEW) opens the same name prompt every
// other typed-text entry in this window uses, answering with
// VLIST_TAG_NEW_RESULT. Toggling writes the bookmark's SG:tags
// attribute straight through BookmarkFile::Save() -- there is no
// separate tag registry to keep in sync, exactly as the issue asks.
#define VLIST_TAG_TOGGLE		'VLtt'
#define VLIST_TAG_NEW			'VLtn'
#define VLIST_TAG_NEW_RESULT	'VLtN'

// A dedicated, standalone window for browsing, editing and reading a
// verse list (#47, second attempt) -- a named, ordered collection of
// references kept as one BookmarkFile per reference in its own folder
// (#55; the first version of this window kept the whole collection in a
// single VerseListFile, since replaced), one folder per reading plan/
// topic/study. Modeled directly on SGSearchWindow (LogosSearchWindow.h/
// .cpp):
// same owner-BMessenger shape, same lazy-create-and-Show() lifecycle
// from SGMainWindow.
//
// Why a separate window rather than a column embedded in a chain (the
// first #47 attempt, still intact but unmerged on
// origin/feature/search-within-verse-list): the original #47 request
// described a lightweight file-management tool (New/Open/Close/Save/
// Delete, reorder, describe) reading a *normal* chapter alongside it,
// navigated by clicking a list entry -- not a
// concatenated multi-section document rendered into the reading pane.
// A third ParallelBibleView column type would have touched 23 functions
// that today only distinguish COLUMN_BIBLE/COLUMN_NOTES; this window
// needs none of that -- it navigates the owning SGMainWindow's active
// chain the exact same way a dropped reference or the universal search
// box already do, by posting SG_BIBLE to it.
class SGVerseListWindow : public BWindow {
public:
							SGVerseListWindow(BRect frame,
								BMessenger* owner);
	virtual					~SGVerseListWindow();

	// Hides instead of really closing, and only ever really Quit()s from
	// SGMainWindow::QuitRequested() on real app shutdown -- same idiom
	// (and the same use-after-free race it avoids) as
	// SGDictionaryWindow::QuitRequested()/SGSearchWindow::
	// QuitRequested(). SGMainWindow::EnsureVerseListWindow() calls
	// Show()/Activate(true) directly (both are documented thread-safe
	// BWindow entry points), same shape as EnsureDictionaryWindow() --
	// no self-messaging needed, unlike SGSearchWindow's RunSearch(),
	// because nothing here is called from another window's thread while
	// expecting to touch this window's own fields synchronously.
	virtual bool			QuitRequested();
	virtual void			MessageReceived(BMessage* message);

	// SGMainWindow::SavePrefsForModule() reads this to remember which
	// collection was open across a restart, alongside Frame() (already
	// public, inherited) for the window's own position/size -- empty
	// when nothing is open, matching what a freshly-posted
	// VLIST_NAV_SELECT with this same path back at restore time
	// (EnsureVerseListWindow()) would silently no-op on anyway.
	BString					CollectionPath() const
								{ return fHasOpenFile ? fCollectionPath
									: BString(); }

			// Called by VerseListRowListView's own MessageReceived()
			// (unrelated class, not a friend -- same reasoning as
			// ParallelBibleView's _ColumnScrolled() etc.) once a
			// drag-reorder drop has landed. Reorders both the row list
			// view and the underlying VerseListFile to match.
			void			_MoveRow(int32 from, int32 to);
			// Same not-a-friend reasoning, for a drop that isn't a
			// reorder: one or more references dragged in from outside
			// (a multi-select search-result drag, in particular) --
			// see the definition for why this doesn't just take the
			// dropped keys at face value (#46).
			void			_AppendDroppedReferences(BMessage* message);
			// Same not-a-friend reasoning: the Delete key or the
			// row list's own right-click "Remove" item, both handled in
			// VerseListRowListView. Removes exactly one row (#66) --
			// unlike File > Delete File..., which removes the whole
			// list.
			void			_RemoveRow(int32 index);
			// Same not-a-friend reasoning: removes every currently
			// selected row (#58 -- fRowList allows more than one now),
			// used by both the Delete key and the row list's own
			// right-click "Remove", as well as Edit > Remove.
			void			_RemoveSelectedRows();
			// Same not-a-friend reasoning: the row list's own right-click
			// "Edit Reference..." item. Opens the prompt window;
			// _EditReference() (private, called once the prompt returns
			// a result) does the actual rewrite.
			void			_StartEditReference(int32 index);
			// Same not-a-friend reasoning: VerseListRowColumn's own
			// right-click handler (#57) -- builds and shows the
			// Edit Reference/Remove popup at `screenPoint`, acting on
			// whichever row(s) are already selected by the time this is
			// called (VerseListRowColumn's own MouseDown() makes sure the
			// clicked row is part of that selection first).
			void			_ShowRowContextMenu(BPoint screenPoint);
			// Same not-a-friend reasoning: VerseListDescriptionView's
			// own KeyDown() (#50), called with whatever text sat
			// between the last newline and the caret right as Enter
			// was pressed -- a silent counterpart to _CreateReference()
			// (private, below; no alert on a parse failure, since most
			// finished lines are just prose, not a reference) plus a
			// duplicate check _CreateReference() doesn't need (pressing
			// Enter again after an already-recognized line, e.g. to add
			// a blank line below it, must not add the same bookmark
			// twice). Returns true if it actually added something --
			// the line is only removed from the description (leaving
			// it "vanish" into the list, per #50) when this returns
			// true; a plain Shift+Enter skips calling this at all, so
			// a line that merely looks like a reference can still be
			// kept as ordinary prose.
			bool			_TryAutoAddDescriptionReference(
								const char* line);
			// Same not-a-friend reasoning: VerseListDescriptionView's
			// own MouseDown() (#50/#28 parity with Notes) -- a plain
			// click landing on one of the blue spans
			// _RestyleDescriptionReferences() just styled follows it,
			// same SG_BIBLE path BibleColumnView/NotesDisplayView
			// already use.
			bool			_DescriptionReferenceLinkAt(int32 offset,
								BString& outKey) const;
			// Shared tail of the private _NavigateToRow() -- also
			// called directly by VerseListDescriptionView's own click-
			// follow (#50/#28) with a canonical key from
			// FindReferencesInText() rather than a row index. NOT
			// Window()->PostMessage() -- see the .cpp for why this
			// window in particular has to route through fMessenger.
			void			_NavigateToKey(const char* key);
			// Same not-a-friend reasoning as _ShowRowContextMenu(): called
			// by VerseListRowColumn's own MouseDown() when the Tags cell
			// of `rowIndex` is clicked. Takes a ROW index (translated to
			// a bookmark index inside, see _BookmarkIndexForRow()).
			void			_ShowTagMenu(int32 rowIndex, BPoint screenPoint);

private:
			void			_BuildGUI();
			BMenuBar*		_BuildMenuBar();
			void			_BuildFilePanels();

			// File menu handlers.
			void			_NewList();
			// Called once VerseListNamePromptWindow reports a chosen
			// collection name and (#97) location -- NULL/empty
			// `parentPath` means the root, same as before #97.
			void			_CreateNewList(const char* name,
								const char* parentPath = NULL);
			// "New sub-collection here..." -- same prompt as _NewList(),
			// just with `path` fixed instead of user-chosen.
			void			_StartNewSubCollectionHere(const char* path);
			// Also the target of Go to List's own navigation, not just
			// startup restore -- see VLIST_NAV_SELECT.
			void			_OpenList(const char* path);
			// #68: a plain-text file, one reference per line (no
			// header) -- creates a new top-level collection named after
			// the file and opens it. See the .cpp for the exact parsing
			// and what happens to a line that doesn't parse.
			void			_ImportPanel();
			// #95: merges into the open collection if there is one.
			// #97: if not, hands off to the two below for a destination.
			void			_ImportTextFile(const char* path);
			void			_StartImportIntoNewList(const char* path,
								const BString& content);
			void			_ImportIntoNewList(const char* name,
								const char* parentPath);
			// #102: the reverse of Import -- writes fBookmarks' own
			// references, one per line, to a plain-text file.
			void			_ExportPanel();
			void			_ExportTextFile(const char* path);
			// #99: opens fCollectionPath in a Tracker window.
			void			_ShowInTracker();
			// #72: shows the same New-Verse-List prompt _NewList() does,
			// but with VLIST_DROP_NAME_RESULT as the result -- called from
			// _AppendDroppedReferences() when a drop lands with nothing
			// open, after that message has already been saved to
			// fPendingDropMessage.
			void			_StartNewListForDrop();
			void			_CloseList();
			void			_DeleteList();
			// #58: relocates/duplicates the whole open collection folder
			// (nested sub-collections included) under `destParentPath`.
			void			_MoveListTo(const char* destParentPath);
			void			_CopyListTo(const char* destParentPath);
			// #58: copies (or, if `move`, copies then removes the
			// originals) every currently selected row into the collection
			// at `destPath`, appended after whatever is already there.
			void			_CopyOrMoveSelectedEntries(const char* destPath,
								bool move);
			// Double-click on the name view (#73) -- opens the same
			// name-prompt window _NewList() uses, pre-filled with the
			// current name, then renames the collection's own folder in
			// place once it returns.
			void			_StartRename();
			void			_RenameList(const char* name);

			// Edit > Add Reference... and (via the public
			// _StartEditReference() above) a row's own right-click "Edit
			// Reference...". Both open the same prompt window;
			// _CreateReference() appends a new bookmark at the end,
			// _EditReference() rewrites an existing row in place without
			// moving it. Either shows an alert instead, rather than
			// silently doing nothing, when the typed text doesn't parse
			// as a reference.
			void			_AddReference();
			void			_CreateReference(const char* text);
			void			_EditReference(int32 index, const char* text);

			// Loads `path` (a collection FOLDER, one bookmark file per
			// reference -- see BookmarkFile) into the window (list rows +
			// description), replacing whatever was open. Shared by
			// _OpenList(), startup restore, and the navigation menu.
			void			_LoadFile(const char* path);
			// Rebuilds the row list from fBookmarks -- applying
			// fTagFilter (#57) if one is set, and refreshing
			// fVisibleBookmarkIndices/the tag filter menu's own
			// contents to match.
			void			_RebuildRows();
			// #57: every distinct tag across fBookmarks (each one's
			// comma-separated Tags() split and trimmed), sorted, as
			// fTagFilterMenu's items -- plus a leading "All Tags".
			// Re-marks whichever one matches fTagFilter, or falls back
			// to "All Tags" if that tag no longer exists in the
			// (possibly just-changed) collection. Called from
			// _RebuildRows() -- see there for why that single call site
			// is enough to keep this current.
			void			_RebuildTagFilterMenu();
			// Every distinct tag used anywhere in the open collection,
			// sorted -- scanned from the bookmarks themselves rather
			// than a separate registry (#57), so it can never drift out
			// of sync with what the files actually carry. Shared by the
			// filter dropdown and a row's own tag menu.
			std::vector<BString>	_CollectionTags() const;
			// Adds `tag` to bookmark `index` if it isn't there, removes
			// it if it is, then saves that one file and rebuilds.
			void			_ToggleTag(int32 index, const BString& tag);
			// "New Tag..." -- opens the name prompt, then _AddTag().
			void			_StartNewTag(int32 index);
			void			_AddTag(int32 index, const char* tag);
			// A row's position in fRowList is no longer necessarily its
			// fBookmarks[] index once a tag filter can skip entries
			// (see fVisibleBookmarkIndices) -- this is the one place
			// that translates back, used at every point a row/selection
			// index from fRowList feeds into code that indexes
			// fBookmarks directly. -1 for an out-of-range row.
			int32			_BookmarkIndexForRow(int32 rowIndex) const;
			// Pushes the current description text into the (quiet, not
			// user-edit-triggering) TextDocument.
			void			_RebuildDescription();
			void			_UpdateTitle();
			// #93: Edit Reference/Remove/Move Up/Move Down all act on
			// whichever row is selected -- called both from
			// _UpdateTitle() (list opened/closed) and on every
			// VLIST_ROW_SELECTED (selection itself changed).
			void			_UpdateRowActionState();
			// The versification new bookmarks in the currently open
			// collection should be written in: the first already-loaded
			// bookmark's own, or "KJV" for a still-empty collection. Each
			// bookmark file remembers its own versification independently
			// (see BookmarkFile), so this is only a default for what's
			// about to be created, not a stored collection-level setting.
			BString			_CollectionVersification() const;
			BString			_DescriptionPath() const;

			// The "Go to list" menu: one item per collection folder
			// (BookmarkFile::ListCollectionNames()), nested arbitrarily
			// deep as cascading submenus (#78) -- see
			// PopulateCollectionMenu() in the .cpp.
			void			_RebuildNavigationMenu();

			// Single click on a row: posts SG_BIBLE with that row's
			// reference to fMessenger (the owning SGMainWindow), the
			// same message a dropped reference or the universal search
			// box already posts -- no new navigation mechanism.
			void			_NavigateToRow(int32 index);

			// Debounced write-back for the description TextDocument --
			// same idea as ParallelBibleView's NotesSaveListener, just
			// targeting the collection's sibling Description.txt (see
			// BookmarkFile::kDescriptionFileName) instead of a
			// PersonalNotesModule.
			void			_DescriptionEdited();
			void			_SaveDescription();
			// #50/#28: re-styles every recognized reference in the
			// description blue, same look Notes gives one (see
			// BibleTextDocument's fReferenceLinkStyle) -- called after
			// every edit (from _DescriptionEdited(), immediately, not
			// debounced like the disk save) and once after loading a
			// collection (from _RebuildDescription()). Rebuilds
			// fDescriptionReferenceLinks and fDescriptionDocument's
			// whole content from scratch each time (the text is always
			// short, so this is cheap) rather than trying to patch
			// spans in place, and restores the caret afterward via
			// SetSelection() -- a full Remove()+Insert() pass doesn't
			// go through TextEditor, which is the only thing that
			// otherwise keeps the caret in sync with the document.
			void			_RestyleDescriptionReferences();

			// #79: live-reflect filesystem changes made from outside this
			// window (Tracker, another instance, a script) instead of
			// only ever refreshing at points this window itself already
			// triggers a change. Two watches, not one recursive watch on
			// the whole tree -- Haiku's node monitor is per-directory,
			// not recursive, and there's no bound on how deep a user's
			// own collection nesting goes -- see the .cpp for the exact
			// scope this covers (root + the one currently open
			// collection, not every nested folder at every depth).
			void			_WatchRoot();
			void			_WatchCollection(const char* path);
			void			_StopWatchingCollection();
			void			_HandleNodeMonitorMessage(BMessage* message);

private:
			// The open collection's folder -- empty when none is open.
			// One BookmarkFile per reference inside it, in Position
			// order (see _RebuildRows()/_MoveRow()); replaces the single
			// VerseListFile the first version of this window used (#55).
			BString					fCollectionPath;
			std::vector<BookmarkFile>	fBookmarks;
			bool					fHasOpenFile;

			// #57 (first slice): filters what _RebuildRows() actually
			// adds to fRowList down to bookmarks carrying this tag --
			// empty means no filter, every bookmark shown, same as
			// before this existed. fVisibleBookmarkIndices[rowIndex] is
			// that row's real fBookmarks[] index (see
			// _BookmarkIndexForRow()); identity 0..n-1 when fTagFilter
			// is empty. Reset to empty whenever a different collection
			// loads (_LoadFile()/_CreateNewList()/_CloseList()) -- a
			// filter chosen for one list has no reason to silently
			// apply to the next one opened, even if it happens to share
			// a tag name.
			BString					fTagFilter;
			std::vector<int32>			fVisibleBookmarkIndices;

			// #97: an import's own file content, held here between
			// _StartImportIntoNewList() showing the name/location prompt
			// and _ImportIntoNewList() consuming it once that prompt
			// returns -- only used when nothing was open at import time.
			BString					fPendingImportContent;
			// #72: same idea, for a reference (or range) dropped onto the
			// row list while nothing is open -- a verbatim copy of the
			// drop message, held between the name/location prompt
			// _AppendDroppedReferences() shows in that case and the same
			// method consuming it again once the prompt returns and a
			// collection exists to append into. Overwritten by each new
			// drop, so a canceled prompt just leaves stale content that's
			// never read again rather than leaking into an unrelated
			// later "New Verse List" -- nothing else ever triggers
			// VLIST_DROP_NAME_RESULT.
			BMessage				fPendingDropMessage;

			// Which node_ref the currently-active collection watch
			// (if any) is for -- needed to tell a B_NODE_MONITOR
			// message's "directory"/"to directory" field apart from
			// the root watch's, and to stop_watching() this ONE node
			// specifically when switching to a different collection
			// (stop_watching(handler) with no node_ref would drop
			// every watch this window has, including the root one).
			node_ref				fRootNodeRef;
			node_ref				fCollectionNodeRef;
			bool					fWatchingCollection;
			bool					fWatchingRoot;

			BMenuBar*				fMenuBar;
			// The list's own name, shown above everything else -- so
			// the window reads as "this is the list you have open" even
			// once the description/row boxes below it fill up.
			BStringView*			fNameView;
			// Breadcrumb line right under fNameView, showing where the
			// open collection lives in the tree (not just its own name) --
			// see _UpdateTitle().
			BStringView*			fPathView;
			BMenuItem*				fExportItem;
			BMenuItem*				fShowInTrackerItem;
			BMenuItem*				fRenameItem;
			BMenuItem*				fDeleteItem;
			BMenuItem*				fAddReferenceItem;
			BMenuItem*				fEditReferenceItem;
			BMenuItem*				fRemoveItem;
			BMenuItem*				fMoveUpItem;
			BMenuItem*				fMoveDownItem;
			BMenu*					fNavigationMenu;
			// #58: cascading destination pickers, same tree as
			// fNavigationMenu -- see PopulateCollectionMenu().
			BMenu*					fMoveListMenu;
			BMenu*					fCopyListMenu;
			BMenu*					fMoveEntriesMenu;
			BMenu*					fCopyEntriesMenu;

			// A value member, not a raw pointer -- TextDocumentRef is a
			// BReference<TextDocument>, so it cleans itself up when this
			// window is destroyed instead of needing a manual delete
			// (TextDocument is BReferenceable, not plain-deletable).
			TextDocumentRef			fDescriptionDocument;
			TextDocumentView*		fDescriptionView;
			BScrollView*			fDescriptionScroll;
			// One entry per recognized reference currently styled blue in
			// fDescriptionDocument (#50/#28 parity with Notes) -- rebuilt
			// from scratch by _RestyleDescriptionReferences() every time
			// the description's text changes, since a span's offsets
			// don't survive an edit anywhere before it. Looked up by
			// _DescriptionReferenceLinkAt() (below), the same not-a-
			// friend pattern _ShowRowContextMenu() etc. already use, for
			// VerseListDescriptionView's own click-to-follow.
			struct DescriptionReferenceLink {
				int32	start;
				int32	end;
				BString	key;
			};
			std::vector<DescriptionReferenceLink>	fDescriptionReferenceLinks;

			// #57 (first slice): a dropdown right above fRowList,
			// narrowing what it shows down to one tag -- see
			// _RebuildTagFilterMenu()/VLIST_TAG_FILTER.
			BMenuField*				fTagFilterField;
			BPopUpMenu*				fTagFilterMenu;

			// #57: BColumnListView manages its own scrolling internally
			// (unlike the old BOutlineListView, which needed an external
			// BScrollView) -- no separate scroll-view field needed.
			VerseListRowListView*	fRowList;

			// B_FILE_NODE -- an import source/export destination is an
			// actual file (the plain-text list), not a collection folder.
			BFilePanel*				fImportPanel;
			BFilePanel*				fExportPanel;

			class DescriptionSaveListener;
			// Kept as the ref-counted wrapper (not just the raw listener
			// pointer) so _RebuildDescription() can detach/reattach it around
			// a programmatic repopulation (loading a different file) without
			// that being mistaken for a user edit -- see the .cpp.
			TextListenerRef				fDescriptionListenerRef;
			class BMessageRunner*		fDescriptionSaveRunner;

			BMessenger*				fMessenger;
};

#endif // VERSE_LIST_WINDOW_H
