#include "LogosVerseListWindow.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <ControlLook.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <fs_attr.h>
#include <StorageDefs.h>
#include <Language.h>
#include <Locale.h>
#include <GroupLayout.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <OutlineListView.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <String.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextControl.h>

#include <algorithm>
#include <ctype.h>
#include <utility>
#include <vector>

#include <versekey.h>

#include "TextDocumentView.h"
#include "TextListener.h"

#include "constants.h"

// See the identical helper in BibleTextDocument.cpp/ParallelBibleView.cpp:
// VerseKey::setText() only recognizes localized book names (e.g. German
// "1. Mose") if the key's locale has been set first, otherwise it fails
// silently and the key is left unchanged.
static void
SetVerseKeyLocale(sword::VerseKey& key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	key.setLocale(language.Code());
}


// Parses `key` (a single, unhyphenated verse reference -- see
// BibleColumnView::_StartDrag()'s own comment on why a drag never hands
// over a combined "start-end" string for this to re-split) in
// `sourceVersification`, repositions it into `targetVersification`, and
// writes the result into `outText`, localized to the current system
// locale if `localizeOutput` is true, or left in VerseKey's own
// no-locale-set default (always English/ASCII book names, confirmed
// empirically -- see the class comment on BookmarkFile) if false. False
// (leaving `outText` untouched) if `key` doesn't parse at all.
//
// `localizeOutput` matters for anything that gets WRITTEN TO A FILE
// (see _AppendDroppedReferences() below): a bookmark's stored reference
// needs to stay parseable on a system running under a DIFFERENT locale
// than the one that wrote it -- confirmed empirically that
// VerseKey::setText() fails outright on a localized book name with no
// locale set (English is the one form every system recognizes
// regardless of its own current locale), so storing anything other than
// the English form would silently strand the reference the moment it's
// read back under a different locale, or a plain attribute (which
// doesn't survive off BFS at all) tried to record which locale it was.
static bool
ConvertVerseReference(const char* key, const char* sourceVersification,
	const char* targetVersification, bool localizeOutput, BString& outText)
{
	sword::VerseKey source;
	SetVerseKeyLocale(source);
	source.setVersificationSystem(sourceVersification);
	source.setText(key);
	if (source.popError() != 0)
		return false;

	sword::VerseKey target;
	if (localizeOutput)
		SetVerseKeyLocale(target);
	target.setVersificationSystem(targetVersification);
	target.positionFrom(source);

	outText = target.getText();
	return true;
}

// Defined further down, alongside _DeleteList() -- forward-declared here
// so MessageReceived()'s VLIST_SAVE_AS_RESULT case (textually earlier in
// this file) can use them too.
static status_t CopyFile(const char* fromPath, const char* toPath);
static status_t CopyCollectionInto(const char* sourceDir,
	const char* destDir);

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "VerseListWindow"

// How long typing has to pause before the description is written back to
// the list file -- same idea and same order of magnitude as the chain
// description strip's own debounce in the shelved #47 attempt: long
// enough that a rewrite-the-whole-file Save() doesn't run per keystroke,
// short enough that an interrupted session doesn't lose a sentence.
static const bigtime_t kDescriptionSaveDelay = 500000; // 0.5s


// The list's own editable description, using the same TextDocumentView
// engine BibleColumnView/NotesDisplayView already render Bible text and
// notes with (see ScriptureGuide/textview/*, vendored from HaikuDepot) --
// deliberately not a plain BTextView, so future rich formatting has
// somewhere to go. TextDocument has a real listener interface
// (TextListener/TextChangedEvent), unlike BTextView, which has none and
// forces subclassing two protected virtuals instead -- this class
// mirrors ParallelBibleView's own NotesSaveListener, which already
// solved exactly this problem for notes text.
class SGVerseListWindow::DescriptionSaveListener : public TextListener {
public:
	DescriptionSaveListener(SGVerseListWindow* owner)
		:
		fOwner(owner)
	{
	}

	virtual void TextChanged(const TextChangedEvent& event)
	{
		if (fOwner != NULL)
			fOwner->_DescriptionEdited();
	}

private:
	SGVerseListWindow*	fOwner;
};


// One row's plain reference text, with drag-out (matching
// ResultListView's own drag-a-reference idiom) and drag-REORDER support
// -- the latter modeled directly on ParallelBibleView's column
// reordering (see BibleColumnView-adjacent header-drag code,
// ParallelBibleView.cpp): MouseDown() on an already-selected row starts
// a drag carrying VLIST_ROW_REORDER plus the row's own index; the drop
// lands as an ordinary MessageReceived() with WasDropped() true, and the
// target index comes from where the drop landed, not from a separate
// drag-and-drop callback.
class VerseListRowListView : public BOutlineListView {
public:
	VerseListRowListView(const char* name, SGVerseListWindow* owner)
		:
		BOutlineListView(name, B_SINGLE_SELECTION_LIST),
		fOwner(owner)
	{
	}

	virtual void MouseDown(BPoint where)
	{
		int32 index = IndexOf(where);

		// Right-click: a "Remove" context menu on whichever row is
		// under the pointer (#66) -- there was previously no way at
		// all to take a single reference back out of an open list,
		// short of deleting the whole file.
		uint32 buttons = 0;
		BMessage* current = Window() != NULL
			? Window()->CurrentMessage() : NULL;
		if (current != NULL)
			current->FindInt32("buttons", (int32*)&buttons);
		if (buttons == B_SECONDARY_MOUSE_BUTTON) {
			if (index >= 0) {
				Select(index);
				BPoint screenPoint = where;
				ConvertToScreen(&screenPoint);
				_ShowRemoveMenu(index, screenPoint);
			}
			return;
		}

		if (index < 0 || !ItemAt(index)->IsSelected()) {
			BOutlineListView::MouseDown(where);
			return;
		}

		BMessage dragMessage(VLIST_ROW_REORDER);
		dragMessage.AddInt32("from", index);
		DragMessage(&dragMessage, ItemFrame(index));
	}

	virtual void KeyDown(const char* bytes, int32 numBytes)
	{
		if (numBytes == 1 && bytes[0] == B_DELETE) {
			int32 index = CurrentSelection();
			if (index >= 0 && fOwner != NULL) {
				fOwner->_RemoveRow(index);
				return;
			}
		}
		BOutlineListView::KeyDown(bytes, numBytes);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == VLIST_REMOVE_ROW) {
			int32 index;
			if (message->FindInt32("index", &index) == B_OK
				&& fOwner != NULL) {
				fOwner->_RemoveRow(index);
			}
			return;
		}
		if (message->WasDropped() && message->what == VLIST_ROW_REORDER) {
			int32 from;
			if (message->FindInt32("from", &from) == B_OK) {
				BPoint dropPoint = message->DropPoint();
				ConvertFromScreen(&dropPoint);
				int32 to = IndexOf(dropPoint);
				if (to < 0)
					to = CountItems() - 1;
				if (fOwner != NULL)
					fOwner->_MoveRow(from, to);
			}
			return;
		}
		// Any other dropped message carrying "key" strings -- one per
		// reference, from either drag source in this app (ResultListView's
		// own search-result drag, one per selected result, or
		// BibleColumnView's own reading-pane selection drag, always
		// exactly one -- both build the same shape, see _StartDrag()'s
		// own comment on why this was unified rather than left as two)
		// -- means "add these references to the open list", the same
		// way a drop onto a chain reading list mode already worked in
		// #52. Checked by content, not just WasDropped(), so a drop
		// from somewhere unrelated that happens to land here doesn't
		// misfire.
		if (message->WasDropped() && message->HasString("key")
			&& fOwner != NULL) {
			fOwner->_AppendDroppedReferences(message);
			return;
		}
		BOutlineListView::MessageReceived(message);
	}

private:
	// Fire-and-forget, same idiom this app already uses for its other
	// context menus (e.g. ParallelBibleView's "Add to list"/"Remove
	// from list" popups) -- not owned by anything, cleans itself up.
	void _ShowRemoveMenu(int32 index, BPoint screenPoint)
	{
		BPopUpMenu* menu = new BPopUpMenu("removeRow", false, false);
		BMessage* remove = new BMessage(VLIST_REMOVE_ROW);
		remove->AddInt32("index", index);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Remove"), remove));
		menu->SetTargetForItems(this);
		menu->SetAsyncAutoDestruct(true);
		menu->Go(screenPoint, true, true, true);
	}

	SGVerseListWindow*	fOwner;
};


SGVerseListWindow::SGVerseListWindow(BRect frame, BMessenger* owner)
	:
	BWindow(frame, "", B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE),
	fHasOpenFile(false),
	fMenuBar(NULL),
	fNameView(NULL),
	fSaveItem(NULL),
	fSaveAsItem(NULL),
	fDeleteItem(NULL),
	fMoveUpItem(NULL),
	fMoveDownItem(NULL),
	fNavigationMenu(NULL),
	fDescriptionView(NULL),
	fDescriptionScroll(NULL),
	fRowList(NULL),
	fRowScroll(NULL),
	fOpenPanel(NULL),
	fSaveAsPanel(NULL),
	fDescriptionSaveRunner(NULL),
	fMessenger(owner)
{
	float minWidth, minHeight, maxWidth, maxHeight;
	GetSizeLimits(&minWidth, &maxWidth, &minHeight, &maxHeight);
	// Narrow enough to tile comfortably alongside other windows via
	// Stack & Tile -- a row's own reference text and the box labels
	// don't need much horizontal room.
	minWidth = 180;
	minHeight = 360;
	SetSizeLimits(minWidth, maxWidth, minHeight, maxHeight);

	_BuildGUI();
	_BuildFilePanels();
	_RebuildNavigationMenu();
	_UpdateTitle();
}


SGVerseListWindow::~SGVerseListWindow()
{
	// Flush any still-pending debounced save before this window (and
	// with it, the listener it owns) goes away -- the same reasoning
	// the shelved chain-description strip already had for its own
	// teardown.
	if (fDescriptionSaveRunner != NULL)
		_SaveDescription();

	// Same idiom as SGDictionaryWindow's destructor: tells the owning
	// SGMainWindow this window is really being destroyed (only happens
	// on real app shutdown -- QuitRequested() otherwise always hides
	// instead), so it nulls its own pointer rather than calling Show()/
	// Activate() on a deleted BWindow next time EnsureVerseListWindow()
	// runs.
	if (fMessenger != NULL)
		fMessenger->SendMessage(VLIST_QUIT);

	delete fOpenPanel;
	delete fSaveAsPanel;
	delete fMessenger;
}


bool
SGVerseListWindow::QuitRequested()
{
	Hide();
	return false;
}


void
SGVerseListWindow::_BuildGUI()
{
	fMenuBar = _BuildMenuBar();

	// Always visible, regardless of how tall the description/row boxes
	// below grow -- the one place that answers "which list is this" at
	// a glance.
	fNameView = new BStringView("verseListName",
		B_TRANSLATE("(No list open)"));
	fNameView->SetFont(be_bold_font);
	fNameView->SetAlignment(B_ALIGN_CENTER);

	fRowList = new VerseListRowListView("verseListRows", this);
	fRowList->SetSelectionMessage(new BMessage(VLIST_ROW_SELECTED));
	fRowList->SetTarget(this);
	fRowScroll = new BScrollView("verseListRowsScroll", fRowList, 0, false,
		true, B_NO_BORDER);

	fDescriptionDocument.SetTo(new TextDocument(), true);
	// A brand-new TextDocument has ZERO paragraphs, not one empty one --
	// confirmed the hard way: TextDocument::ParagraphIndexFor() returns
	// -1 unconditionally when fParagraphs is empty, which makes
	// Insert()/Replace() fail silently (nothing here checked the return
	// value, so the very first population of this field did nothing at
	// all, with no error). Append() has no such requirement -- it just
	// appends to the paragraph vector -- so it's used once, here, to
	// seed the one paragraph every Insert()/Remove() after this point
	// needs to already exist. _Remove() only ever clears a paragraph's
	// own text, never erases the last remaining one, so this holds for
	// the document's whole lifetime, not just at construction.
	fDescriptionDocument->Append(Paragraph());
	fDescriptionListenerRef.SetTo(new DescriptionSaveListener(this), true);
	fDescriptionDocument->AddListener(fDescriptionListenerRef);

	fDescriptionView = new TextDocumentView("verseListDescription");
	fDescriptionView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	fDescriptionView->SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	fDescriptionView->SetInsets(4.0f, 3.0f, 4.0f, 3.0f);
	fDescriptionView->SetSelectionEnabled(true);
	fDescriptionView->SetEditingEnabled(true);
	fDescriptionView->SetTextDocument(fDescriptionDocument);
	fDescriptionScroll = new BScrollView("verseListDescriptionScroll",
		fDescriptionView, 0, false, true, B_NO_BORDER);
	// A short, fixed-height strip -- overflow scrolls inside it rather
	// than pushing the row list down every time a sentence is added.
	fDescriptionScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 60.0f));
	fDescriptionScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 60.0f));

	// Labeled boxes around each field, same idiom Haiku apps use
	// elsewhere for "group of controls with a caption" (BBox::SetLabel()
	// plus its own BGroupLayout, continued via BLayoutBuilder::Group<>'s
	// AddGroup(BGroupLayout*) overload below) -- so it's visually
	// unambiguous which box is the description and which is the list of
	// references, not just implied by vertical order.
	BBox* descriptionBox = new BBox("descriptionBox");
	descriptionBox->SetLabel(B_TRANSLATE("Description"));
	BGroupLayout* descriptionBoxLayout = new BGroupLayout(B_VERTICAL, 0);
	descriptionBox->SetLayout(descriptionBoxLayout);

	BBox* rowsBox = new BBox("versesBox");
	rowsBox->SetLabel(B_TRANSLATE("Verses"));
	BGroupLayout* rowsBoxLayout = new BGroupLayout(B_VERTICAL, 0);
	rowsBox->SetLayout(rowsBoxLayout);

	// A plain fixed inset (what SetInsets(B_USE_ITEM_INSETS) alone gives
	// every side) has no idea how tall the box's own floating label is,
	// so the content's top edge landed almost under the label text
	// itself -- confirmed live, not just suspected. TopBorderOffset()
	// is exactly the label's own reserved height (see BBox::
	// _ValidateLayoutData(), which grows the box's own top inset to fit
	// it); adding the normal item padding on top of that is the same
	// pattern MediaConverterWindow uses for its own labeled boxes
	// (_UpdateBBoxLayoutInsets()) -- left/right/bottom get plain
	// padding, top gets padding *plus* the label's height.
	float padding = be_control_look->DefaultItemSpacing();
	descriptionBoxLayout->SetInsets(padding,
		descriptionBox->TopBorderOffset() + padding, padding, padding);
	rowsBoxLayout->SetInsets(padding, rowsBox->TopBorderOffset() + padding,
		padding, padding);
	descriptionBoxLayout->AddView(fDescriptionScroll);
	rowsBoxLayout->AddView(fRowScroll);

	// SetInsets() applies to the whole group it's called on -- calling it
	// directly on this outer one would have inset fMenuBar too, leaving
	// it with a margin on every side instead of flush against the
	// window edges like every other menu bar in this app (confirmed live:
	// it visibly floated instead of docking). The content below it gets
	// its own nested group with the inset instead, same shape
	// LogosMainWindow.cpp already uses for fMenuBar/fToolBar vs. the
	// content beneath them.
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_SMALL_INSETS)
			.Add(fNameView)
			.Add(descriptionBox)
			.Add(rowsBox)
		.End()
	.End();
}


BMenuBar*
SGVerseListWindow::_BuildMenuBar()
{
	BMenuBar* menuBar = new BMenuBar("verseListMenuBar");

	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("New Verse List" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_NEW), 'N'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Open Verse List" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_OPEN_PANEL), 'O'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Close Verse List"),
		new BMessage(VLIST_CLOSE), 'W'));
	fileMenu->AddSeparatorItem();
	fSaveItem = new BMenuItem(B_TRANSLATE("Save"), new BMessage(VLIST_SAVE),
		'S');
	fileMenu->AddItem(fSaveItem);
	fSaveAsItem = new BMenuItem(
		B_TRANSLATE("Save As" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_SAVE_AS_PANEL), 'S', B_SHIFT_KEY);
	fileMenu->AddItem(fSaveAsItem);
	fileMenu->AddSeparatorItem();
	fDeleteItem = new BMenuItem(
		B_TRANSLATE("Delete File" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_DELETE));
	fileMenu->AddItem(fDeleteItem);
	menuBar->AddItem(fileMenu);

	BMenu* editMenu = new BMenu(B_TRANSLATE("Edit"));
	fMoveUpItem = new BMenuItem(B_TRANSLATE("Move Up"),
		new BMessage(VLIST_MOVE_UP), B_UP_ARROW);
	editMenu->AddItem(fMoveUpItem);
	fMoveDownItem = new BMenuItem(B_TRANSLATE("Move Down"),
		new BMessage(VLIST_MOVE_DOWN), B_DOWN_ARROW);
	editMenu->AddItem(fMoveDownItem);
	menuBar->AddItem(editMenu);

	fNavigationMenu = new BMenu(B_TRANSLATE("Go to List"));
	menuBar->AddItem(fNavigationMenu);

	menuBar->SetTargetForItems(this);
	return menuBar;
}


// Minimal name-prompt window for "New Verse List..." -- same idiom as
// this app's other small utility windows (e.g. SGDictionaryWindow): a
// non-modal BWindow that posts its result back via BMessenger and closes
// itself, rather than a blocking dialog (nothing in this app has one).
// "Save As..." doesn't need this -- a native BFilePanel's B_SAVE_PANEL
// mode already has its own filename field built in.
static const uint32 kNamePromptOK = 'VLpo';

class VerseListNamePromptWindow : public BWindow {
public:
	VerseListNamePromptWindow(BMessenger target, uint32 resultWhat)
		:
		BWindow(BRect(120, 120, 460, 210), B_TRANSLATE("New Verse List"),
			B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
			B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE | B_AUTO_UPDATE_SIZE_LIMITS),
		fTarget(target),
		fResultWhat(resultWhat)
	{
		fNameControl = new BTextControl("name", B_TRANSLATE("Name:"), "",
			new BMessage(kNamePromptOK));
		BButton* okButton = new BButton("ok", B_TRANSLATE("Create"),
			new BMessage(kNamePromptOK));
		SetDefaultButton(okButton);

		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(fNameControl)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(okButton)
			.End()
		.End();

		fNameControl->MakeFocus(true);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kNamePromptOK) {
			BString name(fNameControl->Text());
			name.Trim();
			if (!name.IsEmpty()) {
				BMessage result(fResultWhat);
				result.AddString("name", name);
				fTarget.SendMessage(&result);
				Quit();
			}
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	BMessenger		fTarget;
	uint32			fResultWhat;
	BTextControl*	fNameControl;
};


void
SGVerseListWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case VLIST_NEW:
			_NewList();
			break;

		case kNamePromptOK:
		{
			BString name;
			if (message->FindString("name", &name) == B_OK)
				_CreateNewList(name.String());
			break;
		}

		case VLIST_OPEN_PANEL:
			_OpenPanel();
			break;

		case VLIST_OPEN_RESULT:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				_OpenList(path.Path());
			}
			break;
		}

		case VLIST_NAV_SELECT:
		{
			BString path;
			if (message->FindString("path", &path) == B_OK)
				_OpenList(path.String());
			break;
		}

		case VLIST_CLOSE:
			_CloseList();
			break;

		case VLIST_SAVE:
			_SaveList();
			break;

		case VLIST_SAVE_AS_PANEL:
			_SaveListAs();
			break;

		case VLIST_SAVE_AS_RESULT:
		{
			entry_ref dirRef;
			BString name;
			if (message->FindRef("directory", &dirRef) == B_OK
				&& message->FindString("name", &name) == B_OK
				&& fHasOpenFile) {
				BPath dirPath(&dirRef);
				// A fresh collection folder, then every bookmark (plus
				// Description.txt) copied into it byte-for-byte,
				// attributes included -- the original at fCollectionPath
				// is left untouched, same "Save As..." contract
				// VerseListFile::SaveAs() had.
				BString newPath = BookmarkFile::CreateCollection(
					dirPath.Path(), name.String());
				if (!newPath.IsEmpty()) {
					CopyCollectionInto(fCollectionPath.String(),
						newPath.String());
					_LoadFile(newPath.String());
					_RebuildNavigationMenu();
				}
			}
			break;
		}

		case VLIST_DELETE:
			_DeleteList();
			break;

		case VLIST_MOVE_UP:
		{
			int32 selected = fRowList->CurrentSelection();
			if (selected > 0)
				_MoveRow(selected, selected - 1);
			break;
		}

		case VLIST_MOVE_DOWN:
		{
			int32 selected = fRowList->CurrentSelection();
			if (selected >= 0 && selected + 1 < fRowList->CountItems())
				_MoveRow(selected, selected + 1);
			break;
		}

		case VLIST_ROW_SELECTED:
		{
			int32 index = fRowList->CurrentSelection();
			if (index >= 0)
				_NavigateToRow(index);
			break;
		}

		case VLIST_DESCRIPTION_CHANGED:
			_SaveDescription();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
SGVerseListWindow::_BuildFilePanels()
{
	entry_ref dirRef;
	BEntry dirEntry(BookmarkFile::RootDirectory().String());
	dirEntry.GetRef(&dirRef);

	// B_DIRECTORY_NODE, not B_FILE_NODE -- a collection is a folder now
	// (#55), so both panels browse and select folders instead of files.
	// The result messages' own shape (VLIST_OPEN_RESULT's "refs",
	// VLIST_SAVE_AS_RESULT's "directory"+"name") is unaffected either
	// way -- that comes from B_OPEN_PANEL/B_SAVE_PANEL mode, not the node
	// flavor.
	fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), &dirRef,
		B_DIRECTORY_NODE, false, new BMessage(VLIST_OPEN_RESULT));
	fOpenPanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Open"));

	fSaveAsPanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), &dirRef,
		B_DIRECTORY_NODE, false, new BMessage(VLIST_SAVE_AS_RESULT));
	fSaveAsPanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Save"));
}


void
SGVerseListWindow::_NewList()
{
	VerseListNamePromptWindow* prompt = new VerseListNamePromptWindow(
		BMessenger(this), kNamePromptOK);
	prompt->Show();
}


void
SGVerseListWindow::_CreateNewList(const char* name)
{
	// Always created at the top level -- nesting a new collection inside
	// whichever one happens to be open would be surprising, and "New"
	// inside an open collection isn't offered anywhere in the menu (the
	// way to grow a nested structure is Tracker itself, or a future
	// "New Sub-Collection" -- issue #56).
	BString path = BookmarkFile::CreateCollection(NULL, name);
	if (path.IsEmpty())
		return;

	fCollectionPath = path;
	fBookmarks.clear();
	fHasOpenFile = true;
	_RebuildRows();
	_RebuildDescription();
	_UpdateTitle();
	// A brand-new collection, possibly the first one ever, changes what
	// the navigation menu has to offer.
	_RebuildNavigationMenu();
}


void
SGVerseListWindow::_OpenPanel()
{
	if (fOpenPanel != NULL)
		fOpenPanel->Show();
}


void
SGVerseListWindow::_OpenList(const char* path)
{
	_LoadFile(path);
}


void
SGVerseListWindow::_CloseList()
{
	fCollectionPath = "";
	fBookmarks.clear();
	fHasOpenFile = false;
	_RebuildRows();
	_RebuildDescription();
	_UpdateTitle();
}


void
SGVerseListWindow::_SaveList()
{
	if (!fHasOpenFile)
		return;
	// Every bookmark file already writes itself immediately on creation,
	// removal and reorder (see _AppendDroppedReferences()/_RemoveRow()/
	// _MoveRow()) -- the one thing that CAN still be sitting unflushed is
	// the description, if the debounce timer (_DescriptionEdited()) hasn't
	// fired yet. "Save" means "don't wait for it".
	if (fDescriptionSaveRunner != NULL)
		_SaveDescription();
}


void
SGVerseListWindow::_SaveListAs()
{
	if (fSaveAsPanel == NULL)
		return;
	BPath current(fCollectionPath.String());
	fSaveAsPanel->SetSaveText(fHasOpenFile && current.Leaf() != NULL
		? current.Leaf() : "Untitled list");
	fSaveAsPanel->Show();
}


// Copies one bookmark (or the description) file's raw bytes AND its BFS
// attributes -- no BFS "copy a file" call exists in the Storage Kit
// outside of Tracker's own private implementation, so this is the same
// manual open/read/write a handful of small files needs regardless.
// Attributes matter here specifically for Position/Tags (see
// BookmarkFile) -- a byte-only copy would silently reset every bookmark
// to "no position" (sorts last) and "no tags" in the new collection,
// which "Save As..." copying the SAME collection under a new name should
// never do.
static status_t
CopyFile(const char* fromPath, const char* toPath)
{
	BFile from(fromPath, B_READ_ONLY);
	status_t status = from.InitCheck();
	if (status != B_OK)
		return status;

	BFile to(toPath, B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	status = to.InitCheck();
	if (status != B_OK)
		return status;

	off_t size = 0;
	from.GetSize(&size);
	if (size > 0) {
		char* buffer = new(std::nothrow) char[size];
		if (buffer == NULL)
			return B_NO_MEMORY;
		ssize_t bytesRead = from.Read(buffer, size);
		if (bytesRead > 0)
			to.Write(buffer, bytesRead);
		delete[] buffer;
	}

	char attrName[B_ATTR_NAME_LENGTH];
	while (from.GetNextAttrName(attrName) == B_OK) {
		attr_info info;
		if (from.GetAttrInfo(attrName, &info) != B_OK)
			continue;
		char* attrBuffer = new(std::nothrow) char[info.size];
		if (attrBuffer == NULL)
			continue;
		ssize_t bytesRead = from.ReadAttr(attrName, info.type, 0, attrBuffer,
			info.size);
		if (bytesRead > 0) {
			to.WriteAttr(attrName, info.type, 0, attrBuffer,
				(size_t)bytesRead);
		}
		delete[] attrBuffer;
	}

	return B_OK;
}


// Copies every file directly inside `sourceDir` (bookmarks plus
// Description.txt, if present) into `destDir`, which CreateCollection()
// has already created -- what "Save As..." needs: the same content,
// under a new name/location, the original left untouched (matching how
// VerseListFile::SaveAs() behaved before it). Not recursive -- a
// collection is deliberately flat (see _DeleteList()'s own comment).
static status_t
CopyCollectionInto(const char* sourceDir, const char* destDir)
{
	BDirectory dir(sourceDir);
	if (dir.InitCheck() != B_OK)
		return B_ERROR;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory())
			continue;
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;

		BPath fromPath;
		entry.GetPath(&fromPath);
		BPath toPath(destDir);
		toPath.Append(name);
		CopyFile(fromPath.Path(), toPath.Path());
	}
	return B_OK;
}


void
SGVerseListWindow::_DeleteList()
{
	if (!fHasOpenFile || fCollectionPath.IsEmpty())
		return;

	// A real, if lightweight, confirmation -- deleting a collection
	// (unlike closing this window on it) cannot be undone by reopening
	// it. Warns about the whole folder now, not a single file, since
	// that's what's actually about to disappear (#55).
	BAlert* alert = new BAlert(B_TRANSLATE("Delete Verse List"),
		B_TRANSLATE("Delete this entire collection and everything in it? "
			"This cannot be undone."),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Delete"), NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return;

	// A collection is deliberately flat (no bookmark is itself a
	// directory -- nesting is expressed as a SEPARATE, sibling
	// collection, not as content of this one), so removing every entry
	// directly inside it, then the now-empty folder itself, is enough;
	// no recursive descent needed.
	BDirectory dir(fCollectionPath.String());
	if (dir.InitCheck() == B_OK) {
		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK)
			entry.Remove();
	}
	BEntry(fCollectionPath.String()).Remove();

	_CloseList();
	_RebuildNavigationMenu();
}


void
SGVerseListWindow::_LoadFile(const char* path)
{
	BEntry entry(path);
	if (entry.InitCheck() != B_OK || !entry.IsDirectory())
		return;

	fCollectionPath = path;
	fBookmarks.clear();

	std::vector<BString> paths = BookmarkFile::ListBookmarkPaths(path);
	for (size_t i = 0; i < paths.size(); i++) {
		BookmarkFile bookmark;
		if (bookmark.SetTo(paths[i].String()) == B_OK)
			fBookmarks.push_back(bookmark);
	}

	fHasOpenFile = true;
	_RebuildRows();
	_RebuildDescription();
	_UpdateTitle();
}


// A bookmark's own Reference() is always the portable, English/ASCII
// form (see ConvertVerseReference()'s own comment) -- this re-renders it
// into whatever locale is CURRENTLY active for display, the same way a
// fresh drop would have under that locale. Recomputed every time rather
// than cached, so the row list automatically follows a locale change
// instead of staying frozen in whatever locale happened to be active
// when each bookmark was created.
static BString
DisplayReference(const BookmarkFile& bookmark)
{
	BString reference(bookmark.Reference());

	// A stored range ("John 3:12-16") has its trailing "-<verse>" split
	// off first -- ConvertVerseReference() only understands a single,
	// unhyphenated reference (same reasoning _AppendDroppedReferences()
	// already has for why a range can't be handed to VerseKey::setText()
	// whole).
	BString suffix;
	int32 dash = reference.FindLast('-');
	if (dash >= 0) {
		bool isRangeEnd = dash + 1 < reference.Length();
		for (int32 i = dash + 1; i < reference.Length() && isRangeEnd; i++) {
			if (!isdigit((unsigned char)reference.ByteAt(i)))
				isRangeEnd = false;
		}
		if (isRangeEnd) {
			reference.CopyInto(suffix, dash, reference.Length() - dash);
			reference.Truncate(dash);
		}
	}

	BString displayText;
	if (!ConvertVerseReference(reference.String(), bookmark.Versification(),
			bookmark.Versification(), true, displayText)) {
		return BString(bookmark.Reference());
	}
	displayText << suffix;
	return displayText;
}


void
SGVerseListWindow::_RebuildRows()
{
	fRowList->MakeEmpty();
	for (size_t i = 0; i < fBookmarks.size(); i++)
		fRowList->AddItem(new BStringItem(DisplayReference(fBookmarks[i])));
}


BString
SGVerseListWindow::_CollectionVersification() const
{
	if (!fBookmarks.empty() && fBookmarks[0].Versification()[0] != '\0')
		return BString(fBookmarks[0].Versification());
	return BString("KJV");
}


BString
SGVerseListWindow::_DescriptionPath() const
{
	if (fCollectionPath.IsEmpty())
		return BString();
	BPath path(fCollectionPath.String());
	path.Append(BookmarkFile::kDescriptionFileName);
	return BString(path.Path());
}


void
SGVerseListWindow::_RebuildDescription()
{
	// Detached around the programmatic repopulation, exactly so this
	// doesn't arm the save-debounce timer for text the file itself just
	// supplied -- see the member comment on fDescriptionListenerRef.
	fDescriptionDocument->RemoveListener(fDescriptionListenerRef);

	int32 length = fDescriptionDocument->Length();
	if (length > 0)
		fDescriptionDocument->Remove(0, length);

	// A plain sibling text file, not a bookmark with no reference set
	// (see BookmarkFile::kDescriptionFileName's own comment on that
	// design choice) -- read directly rather than through BookmarkFile,
	// since it deliberately isn't one.
	BString description;
	BString descriptionPath = _DescriptionPath();
	if (!descriptionPath.IsEmpty()) {
		BFile file(descriptionPath.String(), B_READ_ONLY);
		if (file.InitCheck() == B_OK) {
			off_t size = 0;
			file.GetSize(&size);
			if (size > 0) {
				char* buffer = new(std::nothrow) char[size + 1];
				if (buffer != NULL) {
					ssize_t bytesRead = file.Read(buffer, size);
					if (bytesRead > 0) {
						buffer[bytesRead] = '\0';
						description = buffer;
					}
					delete[] buffer;
				}
			}
		}
	}
	if (!description.IsEmpty())
		fDescriptionDocument->Insert(0, description);

	fDescriptionDocument->AddListener(fDescriptionListenerRef);
	// Relayout() alone only invalidates the cached layout -- it doesn't
	// repaint. A document mutated directly (bypassing the TextEditor a
	// real keystroke goes through) needs both, same pairing
	// ParallelBibleView uses everywhere a document's content changes out
	// from under an already-visible view.
	fDescriptionView->Relayout();
	fDescriptionView->Invalidate();
}


void
SGVerseListWindow::_UpdateTitle()
{
	BPath current(fCollectionPath.String());
	if (fHasOpenFile && current.Leaf() != NULL) {
		SetTitle(current.Leaf());
		fNameView->SetText(current.Leaf());
	} else {
		SetTitle(B_TRANSLATE("Verse Lists"));
		fNameView->SetText(B_TRANSLATE("(No list open)"));
	}

	bool onList = fHasOpenFile;
	fSaveItem->SetEnabled(onList);
	fSaveAsItem->SetEnabled(onList);
	fDeleteItem->SetEnabled(onList);
	fMoveUpItem->SetEnabled(onList);
	fMoveDownItem->SetEnabled(onList);
}


void
SGVerseListWindow::_RebuildNavigationMenu()
{
	if (fNavigationMenu == NULL)
		return;
	for (int32 i = fNavigationMenu->CountItems() - 1; i >= 0; i--)
		delete fNavigationMenu->RemoveItem(i);

	// One item per top-level collection folder -- every collection is a
	// folder now (#55), so there's no more "loose file vs. collection
	// subfolder" distinction to show as two tiers the way the old
	// VerseListFile-backed menu did. A nested sub-collection (a subfolder
	// of one of these) is reachable through Open's own folder picker,
	// not this menu -- same one-level-only scope
	// BookmarkFile::ListCollectionNames() itself documents.
	BString root = BookmarkFile::RootDirectory();
	std::vector<BString> names = BookmarkFile::ListCollectionNames();
	for (size_t i = 0; i < names.size(); i++) {
		BPath path(root.String());
		path.Append(names[i].String());

		BMessage* select = new BMessage(VLIST_NAV_SELECT);
		select->AddString("path", path.Path());
		fNavigationMenu->AddItem(new BMenuItem(names[i].String(), select));
	}

	fNavigationMenu->SetTargetForItems(this);
}


void
SGVerseListWindow::_NavigateToRow(int32 index)
{
	if (index < 0 || index >= (int32)fBookmarks.size() || fMessenger == NULL)
		return;

	// The bookmark's own Reference() (always English/ASCII -- see
	// ConvertVerseReference()'s comment), not the row's displayed,
	// locale-rendered text (see DisplayReference()) -- BookFromKey()/
	// ChapterFromKey()/VerseFromKey() (what JumpToKey() below actually
	// calls) always parse under the CURRENT system locale, so handing
	// them anything other than the universally-recognized English form
	// would only work by coincidence, when the display locale and the
	// system's current locale happen to still match.
	//
	// Exactly the path a dropped reference or the universal search box
	// already take (see BibleColumnView::_HandleReferenceDrop() and
	// SGMainWindow::MessageReceived()'s own SG_BIBLE case) -- no new
	// navigation mechanism, just another source for the same message.
	// It reaches the OWNING window's active chain, not necessarily
	// whichever SGMainWindow currently has focus.
	BMessage jump(SG_BIBLE);
	jump.AddString("key", fBookmarks[index].Reference());
	fMessenger->SendMessage(&jump);
}


void
SGVerseListWindow::_MoveRow(int32 from, int32 to)
{
	if (from == to || from < 0 || to < 0
		|| from >= (int32)fBookmarks.size() || to >= (int32)fBookmarks.size()) {
		return;
	}

	fRowList->MoveItem(from, to);
	fRowList->Select(to);

	// The row list is now the source of truth for order -- move the same
	// element within fBookmarks to match, then renumber every bookmark's
	// Position attribute (and save each) rather than trying to patch just
	// the two affected files. A collection is small enough that a full
	// renumber is cheap, and it's the only approach that can't drift out
	// of sync with what's on screen -- a partial update has to get the
	// shift direction right for every index between `from` and `to`,
	// which a full renumber sidesteps entirely.
	BookmarkFile moved = fBookmarks[from];
	fBookmarks.erase(fBookmarks.begin() + from);
	fBookmarks.insert(fBookmarks.begin() + to, moved);

	for (size_t i = 0; i < fBookmarks.size(); i++) {
		fBookmarks[i].SetPosition((int32)i);
		fBookmarks[i].Save();
	}
}


// Removes exactly one row (#66) -- the Delete key or the row list's own
// right-click "Remove" item, unlike File > Delete File..., which removes
// the whole collection. Deletes that one bookmark's own file (#55; used
// to be VerseListFile::RemoveLine() rewriting one shared file); this is
// just the row-list side plus keeping the selection somewhere sensible
// afterward. Positions are deliberately left as they are on the
// remaining bookmarks -- a gap in the sequence is harmless, sorting by
// Position only needs the relative order to stay correct, not a
// contiguous range.
void
SGVerseListWindow::_RemoveRow(int32 index)
{
	if (!fHasOpenFile || index < 0 || index >= (int32)fBookmarks.size())
		return;

	if (fBookmarks[index].Remove() != B_OK)
		return;
	fBookmarks.erase(fBookmarks.begin() + index);

	_RebuildRows();

	int32 count = fRowList->CountItems();
	if (count > 0)
		fRowList->Select(std::min(index, count - 1));
}


// One "key" per dropped reference (ResultListView's own drag carries one
// per selected search result -- confirmed live: a multi-select drag
// previously did nothing at all, because nothing in this window listened
// for a drop that wasn't its own internal reorder message).
//
// Each key is parsed in the SOURCE's own versification -- the searched
// module's, carried as "scriptureguide:versification" on the drag message
// (see ResultListView::SetSourceVersification()) -- and repositioned into
// this list's own declared versification before being written down.
// Skipping that conversion is exactly the #46 mistake #52 already found
// and fixed once for dropping onto a chain reading list mode: a verse
// read in one counting silently becomes a DIFFERENT verse if written
// down unconverted in another.
void
SGVerseListWindow::_AppendDroppedReferences(BMessage* message)
{
	if (!fHasOpenFile)
		return;

	BString targetVersification = _CollectionVersification();

	BString sourceVersification;
	message->FindString("scriptureguide:versification", &sourceVersification);
	if (sourceVersification.IsEmpty()) {
		// Nothing outside the app declares one (a Tracker clipping, a
		// plain-text drag) -- this collection's own counting is the best
		// available guess, the same fallback AppendDroppedReferences()
		// already established on the chain-drop side of #52.
		sourceVersification = targetVersification;
	}

	// One "key" per reference regardless of which drag source this came
	// from -- ResultListView's own search-result drag and
	// BibleColumnView's reading-pane selection drag both build the same
	// shape now (see BibleColumnView::_StartDrag()'s own comment, #63).
	// "endKey" is BibleColumnView-only and only present for an actual
	// multi-verse selection -- ResultListView never sends it (a search
	// result is always exactly one verse), and BibleColumnView never
	// sends more than one "key" at all, so treating "a single key plus
	// endKey" as one range covers exactly the one case that needs it.
	bool appendedAny = false;
	BString endKey;
	bool hasRange = message->FindString("endKey", &endKey) == B_OK;

	const char* key;
	for (int32 i = 0; message->FindString("key", i, &key) == B_OK; i++) {
		BString startText;
		// false: this is what gets written to the bookmark file's own
		// content (see BookmarkFile::CreateNew() below), which has to
		// stay parseable regardless of which locale eventually reads it
		// back -- see ConvertVerseReference()'s own comment on why.
		if (!ConvertVerseReference(key, sourceVersification.String(),
				targetVersification.String(), false, startText)) {
			continue;
		}

		BString line(startText);
		if (hasRange) {
			BString endText;
			if (ConvertVerseReference(endKey.String(),
					sourceVersification.String(),
					targetVersification.String(), false, endText)
				&& endText != startText) {
				// `startText`/`endText` are both "<Book> <Chapter>:<Verse>"
				// -- VerseKey::getText() always uses ':', regardless of
				// locale (confirmed empirically, unlike the display
				// separator _ReferenceFor() uses for `reference`). When
				// they share the same book/chapter, appending the bare
				// trailing verse number keeps the stored line
				// "<Book> <Chapter>:<Start>-<End>" -- the one shape
				// _NavigateToRow() can feed back into VerseKey::setText()
				// as a single reference (which silently truncates to the
				// start verse, exactly like clicking a plain single-verse
				// row) instead of two references awkwardly concatenated
				// with a bare "-" between them.
				int32 startColon = startText.FindLast(':');
				int32 endColon = endText.FindLast(':');
				BString startPrefix, endPrefix, endVerse;
				if (startColon >= 0)
					startText.CopyInto(startPrefix, 0, startColon);
				if (endColon >= 0) {
					endText.CopyInto(endPrefix, 0, endColon);
					endText.CopyInto(endVerse, endColon + 1,
						endText.Length() - endColon - 1);
				}

				if (startColon >= 0 && endColon >= 0
					&& startPrefix == endPrefix) {
					line << "-" << endVerse;
				} else {
					// Book/chapter differ (a versification-driven shift
					// across a boundary) -- can't collapse to one
					// trailing number, so keep both full references.
					line << " - " << endText;
				}
			}
		}

		BookmarkFile bookmark;
		if (bookmark.CreateNew(fCollectionPath.String(), line.String(),
				targetVersification.String(), (int32)fBookmarks.size())
				== B_OK) {
			fBookmarks.push_back(bookmark);
			appendedAny = true;
		}
	}

	if (appendedAny)
		_RebuildRows();
}


void
SGVerseListWindow::_DescriptionEdited()
{
	if (!fHasOpenFile || Looper() == NULL)
		return;

	delete fDescriptionSaveRunner;
	BMessage message(VLIST_DESCRIPTION_CHANGED);
	fDescriptionSaveRunner = new BMessageRunner(BMessenger(this), &message,
		kDescriptionSaveDelay, 1);
}


void
SGVerseListWindow::_SaveDescription()
{
	delete fDescriptionSaveRunner;
	fDescriptionSaveRunner = NULL;

	if (!fHasOpenFile)
		return;

	BString descriptionPath = _DescriptionPath();
	if (descriptionPath.IsEmpty())
		return;

	// A plain sibling file now (see BookmarkFile::kDescriptionFileName),
	// not one line in a shared file's header -- an embedded newline is
	// completely fine, unlike the old VerseListFile-backed version, which
	// had to collapse them to keep the description on its own header
	// line.
	BString text(fDescriptionDocument->Text());
	BFile file(descriptionPath.String(),
		B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		file.Write(text.String(), text.Length());
}
