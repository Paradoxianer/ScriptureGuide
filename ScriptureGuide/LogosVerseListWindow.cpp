#include "LogosVerseListWindow.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <FilePanel.h>
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
#include <TextControl.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "TextDocumentView.h"
#include "TextListener.h"

#include "constants.h"

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
		if (index < 0 || !ItemAt(index)->IsSelected()) {
			BOutlineListView::MouseDown(where);
			return;
		}

		BMessage dragMessage(VLIST_ROW_REORDER);
		dragMessage.AddInt32("from", index);
		DragMessage(&dragMessage, ItemFrame(index));
	}

	virtual void MessageReceived(BMessage* message)
	{
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
		BOutlineListView::MessageReceived(message);
	}

private:
	SGVerseListWindow*	fOwner;
};


SGVerseListWindow::SGVerseListWindow(BRect frame, BMessenger* owner)
	:
	BWindow(frame, "", B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE),
	fHasOpenFile(false),
	fMenuBar(NULL),
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
	minWidth = 320;
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

	fRowList = new VerseListRowListView("verseListRows", this);
	fRowList->SetSelectionMessage(new BMessage(VLIST_ROW_SELECTED));
	fRowList->SetTarget(this);
	fRowScroll = new BScrollView("verseListRowsScroll", fRowList, 0, false,
		true);

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
		fDescriptionView, 0, false, true, B_FANCY_BORDER);
	// A short, fixed-height strip -- overflow scrolls inside it rather
	// than pushing the row list down every time a sentence is added.
	fDescriptionScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 60.0f));
	fDescriptionScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 60.0f));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fDescriptionScroll)
		.Add(fRowScroll)
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
				&& message->FindString("name", &name) == B_OK) {
				BPath dirPath(&dirRef);
				if (fFile.SaveAs(dirPath.Path(), name.String()) == B_OK) {
					fHasOpenFile = true;
					_UpdateTitle();
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
	BEntry dirEntry(VerseListFile::ListsDirectory().String());
	dirEntry.GetRef(&dirRef);

	fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), &dirRef,
		B_FILE_NODE, false, new BMessage(VLIST_OPEN_RESULT));
	fOpenPanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Open"));

	fSaveAsPanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), &dirRef,
		B_FILE_NODE, false, new BMessage(VLIST_SAVE_AS_RESULT));
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
	if (fFile.CreateNew(name, "", "KJV") != B_OK)
		return;
	fHasOpenFile = true;
	_RebuildRows();
	_RebuildDescription();
	_UpdateTitle();
	// A brand-new list, possibly the first one ever, changes what the
	// navigation menu has to offer.
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
	fFile = VerseListFile();
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
	fFile.Save();
}


void
SGVerseListWindow::_SaveListAs()
{
	if (fSaveAsPanel == NULL)
		return;
	fSaveAsPanel->SetSaveText(fFile.Name()[0] != '\0' ? fFile.Name()
		: "Untitled list");
	fSaveAsPanel->Show();
}


void
SGVerseListWindow::_DeleteList()
{
	if (!fHasOpenFile || fFile.Path()[0] == '\0')
		return;

	// A real, if lightweight, confirmation -- deleting a file (unlike
	// closing this window on it) cannot be undone by reopening it.
	BAlert* alert = new BAlert(B_TRANSLATE("Delete Verse List"),
		B_TRANSLATE("Delete this verse list file? This cannot be undone."),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Delete"), NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return;

	BEntry(fFile.Path()).Remove();
	_CloseList();
	_RebuildNavigationMenu();
}


void
SGVerseListWindow::_LoadFile(const char* path)
{
	VerseListFile file;
	if (file.SetTo(path) != B_OK)
		return;

	fFile = file;
	fHasOpenFile = true;
	_RebuildRows();
	_RebuildDescription();
	_UpdateTitle();
}


void
SGVerseListWindow::_RebuildRows()
{
	fRowList->MakeEmpty();

	BString remaining(fFile.ReferenceText());
	while (remaining.Length() > 0) {
		BString line;
		int32 breakAt = remaining.FindFirst("\n");
		if (breakAt < 0) {
			line = remaining;
			remaining = "";
		} else {
			remaining.CopyInto(line, 0, breakAt);
			remaining.Remove(0, breakAt + 1);
		}
		line.Trim();
		if (!line.IsEmpty())
			fRowList->AddItem(new BStringItem(line));
	}
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
	BString description(fFile.Description());
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
	if (fHasOpenFile && fFile.Name()[0] != '\0')
		SetTitle(fFile.Name());
	else
		SetTitle(B_TRANSLATE("Verse Lists"));

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

	// Uncategorized: files directly under ListsDirectory(), as plain
	// items -- same shape _PopulateModuleMenu() uses for the flat
	// "Notes" entry alongside its category submenus.
	std::vector<BString> loosePaths = VerseListFile::ListPaths();
	std::vector<std::pair<BString, BString> > looseEntries;
	for (size_t i = 0; i < loosePaths.size(); i++) {
		VerseListFile probe;
		if (probe.SetTo(loosePaths[i].String()) == B_OK) {
			looseEntries.push_back(
				std::make_pair(BString(probe.Name()), loosePaths[i]));
		}
	}
	std::sort(looseEntries.begin(), looseEntries.end());
	for (size_t i = 0; i < looseEntries.size(); i++) {
		BMessage* select = new BMessage(VLIST_NAV_SELECT);
		select->AddString("path", looseEntries[i].second);
		fNavigationMenu->AddItem(
			new BMenuItem(looseEntries[i].first.String(), select));
	}

	if (!looseEntries.empty())
		fNavigationMenu->AddSeparatorItem();

	// One submenu per collection subfolder, its files sorted by name --
	// mirrors _PopulateModuleMenu()'s "Biblical Texts"/"Commentaries"
	// category submenus, just fed from VerseListFile::
	// ListCollectionNames()/ListCollectionPaths() instead of
	// fManager->getModules().
	std::vector<BString> collections = VerseListFile::ListCollectionNames();
	for (size_t c = 0; c < collections.size(); c++) {
		BMenu* collectionMenu = new BMenu(collections[c].String());

		std::vector<BString> paths
			= VerseListFile::ListCollectionPaths(collections[c].String());
		std::vector<std::pair<BString, BString> > entries;
		for (size_t i = 0; i < paths.size(); i++) {
			VerseListFile probe;
			if (probe.SetTo(paths[i].String()) == B_OK) {
				entries.push_back(
					std::make_pair(BString(probe.Name()), paths[i]));
			}
		}
		std::sort(entries.begin(), entries.end());

		for (size_t i = 0; i < entries.size(); i++) {
			BMessage* select = new BMessage(VLIST_NAV_SELECT);
			select->AddString("path", entries[i].second);
			collectionMenu->AddItem(
				new BMenuItem(entries[i].first.String(), select));
		}
		collectionMenu->SetTargetForItems(this);

		if (collectionMenu->CountItems() > 0)
			fNavigationMenu->AddItem(collectionMenu);
		else
			delete collectionMenu;
	}

	fNavigationMenu->SetTargetForItems(this);
}


void
SGVerseListWindow::_NavigateToRow(int32 index)
{
	BStringItem* item = dynamic_cast<BStringItem*>(fRowList->ItemAt(index));
	if (item == NULL || fMessenger == NULL)
		return;

	// Exactly the path a dropped reference or the universal search box
	// already take (see BibleColumnView::_HandleReferenceDrop() and
	// SGMainWindow::MessageReceived()'s own SG_BIBLE case) -- no new
	// navigation mechanism, just another source for the same message.
	// It reaches the OWNING window's active chain, not necessarily
	// whichever SGMainWindow currently has focus.
	BMessage jump(SG_BIBLE);
	jump.AddString("key", item->Text());
	fMessenger->SendMessage(&jump);
}


void
SGVerseListWindow::_MoveRow(int32 from, int32 to)
{
	if (from == to || from < 0 || to < 0
		|| from >= fRowList->CountItems() || to >= fRowList->CountItems()) {
		return;
	}

	fRowList->MoveItem(from, to);
	fRowList->Select(to);

	// The list view is now the source of truth for order -- rebuild the
	// stored reference text to match it exactly, rather than trying to
	// replicate the same reorder arithmetic on a separate copy of the
	// lines that could drift out of sync with what's on screen.
	BString text;
	for (int32 i = 0; i < fRowList->CountItems(); i++) {
		BStringItem* item = dynamic_cast<BStringItem*>(fRowList->ItemAt(i));
		if (item == NULL)
			continue;
		if (!text.IsEmpty())
			text << "\n";
		text << item->Text();
	}
	fFile.SetReferenceText(text.String());
	fFile.Save();
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

	BString text(fDescriptionDocument->Text());
	// A description is one line in the file's header (see VerseListFile's
	// own comment); an embedded newline would end that line and turn the
	// rest into what reads as a reference.
	text.ReplaceAll('\n', ' ');
	text.Trim();
	if (BString(fFile.Description()) == text)
		return;

	fFile.SetDescription(text.String());
	fFile.Save();
}
