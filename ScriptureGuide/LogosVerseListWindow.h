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
#define VLIST_OPEN_PANEL		'VLop'
#define VLIST_OPEN_RESULT		'VLor'
#define VLIST_SAVE				'VLsv'
#define VLIST_SAVE_AS_PANEL		'VLsp'
#define VLIST_SAVE_AS_RESULT	'VLsr'
#define VLIST_CLOSE				'VLcl'
#define VLIST_DELETE			'VLdl'
#define VLIST_MOVE_UP			'VLmu'
#define VLIST_MOVE_DOWN			'VLmd'
#define VLIST_NAV_SELECT		'VLns'
#define VLIST_ROW_SELECTED		'VLrs'
#define VLIST_ROW_REORDER		'VLrr'
#define VLIST_REMOVE_ROW		'VLrm'
#define VLIST_DESCRIPTION_CHANGED	'VLdc'
// Double-click on fNameView (#73) -- renames the open collection's own
// folder in place, unlike Save As... (which duplicates it elsewhere).
#define VLIST_RENAME			'VLrn'
#define VLIST_RENAME_RESULT	'VLrR'
// File > Import Text List... (#68) -- one reference per line, no header,
// same shape as the real end user's own WORDsearch/QuickVerse-style
// exports (confirmed against real sample files: AARON.TXT, one OSIS-
// style abbreviation like "EXO 4:14" per line). See _ImportTextFile()
// in the .cpp for why this needed no format-specific parsing beyond
// splitting lines -- sword::VerseKey::setText() already accepts these
// abbreviations directly.
#define VLIST_IMPORT_PANEL		'VLip'
#define VLIST_IMPORT_RESULT	'VLir'

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
// origin/feature/search-within-verse-list): a real end user's own
// feedback described a lightweight file-management tool (New/Open/
// Close/Save/Save As/Delete, reorder, describe) reading a *normal*
// chapter alongside it, navigated by clicking a list entry -- not a
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

private:
			void			_BuildGUI();
			BMenuBar*		_BuildMenuBar();
			void			_BuildFilePanels();

			// File menu handlers.
			void			_NewList();
			// Called once VerseListNamePromptWindow reports a chosen
			// collection name.
			void			_CreateNewList(const char* name);
			void			_OpenList(const char* path);
			void			_OpenPanel();
			// #68: a plain-text file, one reference per line (no
			// header) -- creates a new top-level collection named after
			// the file and opens it. See the .cpp for the exact parsing
			// and what happens to a line that doesn't parse.
			void			_ImportPanel();
			void			_ImportTextFile(const char* path);
			void			_CloseList();
			void			_SaveList();
			void			_SaveListAs();
			void			_DeleteList();
			// Double-click on the name view (#73) -- opens the same
			// name-prompt window _NewList() uses, pre-filled with the
			// current name, then renames the collection's own folder in
			// place once it returns.
			void			_StartRename();
			void			_RenameList(const char* name);

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
			BMenuItem*				fSaveItem;
			BMenuItem*				fSaveAsItem;
			BMenuItem*				fDeleteItem;
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

			BFilePanel*				fOpenPanel;
			BFilePanel*				fSaveAsPanel;
			// B_FILE_NODE, unlike the two above -- an import source is
			// an actual file (the plain-text export), not a collection
			// folder.
			BFilePanel*				fImportPanel;

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
