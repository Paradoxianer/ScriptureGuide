#ifndef VERSE_LIST_WINDOW_H
#define VERSE_LIST_WINDOW_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <vector>

#include "TextDocument.h"

#include "parallelbible/VerseListFile.h"

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
#define VLIST_DESCRIPTION_CHANGED	'VLdc'

// A dedicated, standalone window for browsing, editing and reading a
// verse list (#47, second attempt) -- a named, ordered collection of
// references kept in its own VerseListFile, one per reading plan/topic/
// study. Modeled directly on SGSearchWindow (LogosSearchWindow.h/.cpp):
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

private:
			void			_BuildGUI();
			BMenuBar*		_BuildMenuBar();
			void			_BuildFilePanels();

			// File menu handlers.
			void			_NewList();
			// Called once VerseListNamePromptWindow reports a chosen name.
			void			_CreateNewList(const char* name);
			void			_OpenList(const char* path);
			void			_OpenPanel();
			void			_CloseList();
			void			_SaveList();
			void			_SaveListAs();
			void			_DeleteList();

			// Loads `path` into the window (list rows + description),
			// replacing whatever was open. Shared by _OpenList(),
			// startup restore, and the navigation menu.
			void			_LoadFile(const char* path);
			// Rebuilds the row list from fFile.ReferenceText().
			void			_RebuildRows();
			// Pushes the current description text into the (quiet, not
			// user-edit-triggering) TextDocument.
			void			_RebuildDescription();
			void			_UpdateTitle();

			// The cascading "Go to list" menu: one submenu per
			// VerseListFile::ListCollectionNames() entry, plus the
			// uncategorized files directly in ListsDirectory() as plain
			// items -- same shape as ParallelBibleView::
			// _PopulateModuleMenu()'s category-submenu pattern.
			void			_RebuildNavigationMenu();

			// Single click on a row: posts SG_BIBLE with that row's
			// reference to fMessenger (the owning SGMainWindow), the
			// same message a dropped reference or the universal search
			// box already posts -- no new navigation mechanism.
			void			_NavigateToRow(int32 index);

			// Debounced write-back for the description TextDocument --
			// same idea as ParallelBibleView's NotesSaveListener, just
			// targeting VerseListFile::SetDescription() instead of a
			// PersonalNotesModule.
			void			_DescriptionEdited();
			void			_SaveDescription();

private:
			VerseListFile			fFile;
			bool					fHasOpenFile;

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
