#ifndef VERSE_LIST_WINDOW_H
#define VERSE_LIST_WINDOW_H

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
class BMenuItem;
class BOutlineListView;
class BScrollView;
class BStringView;
class TextDocumentView;

// Local message constants, same convention as LogosSearchWindow.h's
// FIND_*/M_ACTIVATE_WINDOW #defines -- window-local, not app-wide, so
// they live here rather than in constants.h.
#define VLIST_QUIT				'VLqu'
#define VLIST_NEW				'VLnw'
#define VLIST_DELETE			'VLdl'
#define VLIST_MOVE_UP			'VLmu'
#define VLIST_MOVE_DOWN			'VLmd'
#define VLIST_NAV_SELECT		'VLns'
#define VLIST_ROW_SELECTED		'VLrs'
#define VLIST_ROW_REORDER		'VLrr'
#define VLIST_REMOVE_ROW		'VLrm'
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
// Edit > Add Reference... and a row's own right-click "Edit Reference..."
// -- drag-and-drop was previously the only way to add a reference, and
// nothing in the window said so. Both open the same prompt window
// (typed text, not a book/chapter/verse picker -- reuses the exact
// validation the universal Go to / Search box already has, see
// _CreateReference()/_EditReference() in the .cpp), just with different
// titles/pre-filled text and a different result handler.
#define VLIST_ADD_REFERENCE		'VLar'
#define VLIST_ADD_REFERENCE_RESULT	'VLaR'
#define VLIST_EDIT_REFERENCE		'VLer'
#define VLIST_EDIT_REFERENCE_RESULT	'VLeR'
// #93: Edit Reference/Remove, triggered from the Edit menu against
// whichever row is currently selected (fRowList->CurrentSelection()),
// rather than the index a context-menu click already carries -- see
// VLIST_EDIT_REFERENCE/VLIST_REMOVE_ROW above for the right-click path,
// which stays as the shortcut both now have a menu equivalent for.
#define VLIST_EDIT_REFERENCE_SELECTED	'VLes'
#define VLIST_REMOVE_SELECTED	'VLrx'

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
			// Same not-a-friend reasoning: the row list's own right-click
			// "Edit Reference..." item. Opens the prompt window;
			// _EditReference() (private, called once the prompt returns
			// a result) does the actual rewrite.
			void			_StartEditReference(int32 index);

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
			void			_CloseList();
			void			_DeleteList();
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
			// Rebuilds the row list from fBookmarks.
			void			_RebuildRows();
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

			// #97: an import's own file content, held here between
			// _StartImportIntoNewList() showing the name/location prompt
			// and _ImportIntoNewList() consuming it once that prompt
			// returns -- only used when nothing was open at import time.
			BString					fPendingImportContent;

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
			BMenuItem*				fExportItem;
			BMenuItem*				fRenameItem;
			BMenuItem*				fDeleteItem;
			BMenuItem*				fAddReferenceItem;
			BMenuItem*				fEditReferenceItem;
			BMenuItem*				fRemoveItem;
			BMenuItem*				fMoveUpItem;
			BMenuItem*				fMoveDownItem;
			BMenu*					fNavigationMenu;

			// A value member, not a raw pointer -- TextDocumentRef is a
			// BReference<TextDocument>, so it cleans itself up when this
			// window is destroyed instead of needing a manual delete
			// (TextDocument is BReferenceable, not plain-deletable).
			TextDocumentRef			fDescriptionDocument;
			TextDocumentView*		fDescriptionView;
			BScrollView*			fDescriptionScroll;

			BOutlineListView*		fRowList;
			BScrollView*			fRowScroll;

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
