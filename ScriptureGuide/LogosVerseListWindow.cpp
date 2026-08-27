#include "LogosVerseListWindow.h"

#include <ctype.h>

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
#include <MenuField.h>
#include <MenuItem.h>
#include <NodeMonitor.h>
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
#include <utility>
#include <vector>

#include <versekey.h>

#include "TextDocumentView.h"
#include "TextListener.h"

#include "constants.h"
#include "SwordBackend.h"

// The current system locale's BLanguage::Code() (e.g. "de") -- what a
// fresh drag-drop reference gets written in (see
// _AppendDroppedReferences()), and what BookmarkFile records alongside
// it so it can be correctly re-parsed later regardless of whatever
// locale is active BY THEN (see _NavigateToRow()). A real user's own
// point: a German reader should see German book names on disk and in
// Tracker, not an English-only canonical form -- portability comes from
// recording which locale was used, not from forcing one.
static BString
CurrentLocaleCode()
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	return BString(language.Code());
}



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

	// A real end user's own testing feedback: an empty list gives no
	// hint at all that dragging a reference in is how it's filled, or
	// that Edit > Add Reference... exists as the alternative. Two
	// short, centered lines drawn directly onto the empty background,
	// same idiom as a placeholder in an empty search-results list would
	// use -- nothing to clean up, they just stop being drawn once
	// CountItems() > 0.
	virtual void Draw(BRect updateRect)
	{
		BOutlineListView::Draw(updateRect);
		if (CountItems() > 0)
			return;

		BString line1(B_TRANSLATE("Drag a Bible reference here,"));
		BString line2(B_TRANSLATE(
			"or use Edit > Add Reference" B_UTF8_ELLIPSIS));

		SetHighColor(tint_color(ViewColor(), B_DARKEN_2_TINT));
		SetLowColor(ViewColor());
		font_height fh;
		GetFontHeight(&fh);
		float lineHeight = fh.ascent + fh.descent + fh.leading;
		BRect bounds = Bounds();
		float y = bounds.Height() / 2 - lineHeight / 2 + fh.ascent;

		float width1 = StringWidth(line1.String());
		DrawString(line1.String(),
			BPoint(std::max(4.0f, (bounds.Width() - width1) / 2), y));

		float width2 = StringWidth(line2.String());
		DrawString(line2.String(),
			BPoint(std::max(4.0f, (bounds.Width() - width2) / 2),
				y + lineHeight));
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
		if (message->what == VLIST_EDIT_REFERENCE) {
			int32 index;
			if (message->FindInt32("index", &index) == B_OK
				&& fOwner != NULL) {
				fOwner->_StartEditReference(index);
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
		BMessage* edit = new BMessage(VLIST_EDIT_REFERENCE);
		edit->AddInt32("index", index);
		menu->AddItem(new BMenuItem(
			B_TRANSLATE("Edit Reference" B_UTF8_ELLIPSIS), edit));
		menu->AddSeparatorItem();
		BMessage* remove = new BMessage(VLIST_REMOVE_ROW);
		remove->AddInt32("index", index);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Remove"), remove));
		menu->SetTargetForItems(this);
		menu->SetAsyncAutoDestruct(true);
		menu->Go(screenPoint, true, true, true);
	}

	SGVerseListWindow*	fOwner;
};


// The name view at the top of the window (#73): a double-click posts
// VLIST_RENAME to the window, same not-a-friend reasoning as
// VerseListRowListView above. A single click does nothing -- this is a
// label, not a button, so there's no affordance to confuse with one.
// Right-click (#97 follow-up) shows a small "Rename List.../Delete
// File..." context menu -- both already exist as File-menu items, this
// just makes them reachable right where the list's own name already is,
// same menu+gesture pairing this window's other actions already have.
class VerseListNameView : public BStringView {
public:
	VerseListNameView(const char* name, const char* text)
		:
		BStringView(name, text)
	{
	}

	virtual void MouseDown(BPoint where)
	{
		uint32 buttons = 0;
		BMessage* current = Window() != NULL
			? Window()->CurrentMessage() : NULL;
		if (current != NULL)
			current->FindInt32("buttons", (int32*)&buttons);
		if (buttons == B_SECONDARY_MOUSE_BUTTON) {
			BPoint screenPoint = where;
			ConvertToScreen(&screenPoint);
			_ShowContextMenu(screenPoint);
			return;
		}

		int32 clicks = 0;
		if (current != NULL)
			current->FindInt32("clicks", &clicks);
		if (clicks >= 2)
			Window()->PostMessage(VLIST_RENAME);
		BStringView::MouseDown(where);
	}

private:
	void _ShowContextMenu(BPoint screenPoint)
	{
		BPopUpMenu* menu = new BPopUpMenu("verseListNameMenu", false, false);
		menu->AddItem(new BMenuItem(
			B_TRANSLATE("Rename List" B_UTF8_ELLIPSIS),
			new BMessage(VLIST_RENAME)));
		menu->AddItem(new BMenuItem(
			B_TRANSLATE("Delete File" B_UTF8_ELLIPSIS),
			new BMessage(VLIST_DELETE)));
		menu->SetTargetForItems(Window());
		menu->SetAsyncAutoDestruct(true);
		menu->Go(screenPoint, true, true, true);
	}
};


SGVerseListWindow::SGVerseListWindow(BRect frame, BMessenger* owner)
	:
	BWindow(frame, "", B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE),
	fHasOpenFile(false),
	fWatchingCollection(false),
	fWatchingRoot(false),
	fMenuBar(NULL),
	fNameView(NULL),
	fExportItem(NULL),
	fRenameItem(NULL),
	fDeleteItem(NULL),
	fAddReferenceItem(NULL),
	fEditReferenceItem(NULL),
	fRemoveItem(NULL),
	fMoveUpItem(NULL),
	fMoveDownItem(NULL),
	fNavigationMenu(NULL),
	fDescriptionView(NULL),
	fDescriptionScroll(NULL),
	fRowList(NULL),
	fRowScroll(NULL),
	fImportPanel(NULL),
	fExportPanel(NULL),
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
	_WatchRoot();
}


SGVerseListWindow::~SGVerseListWindow()
{
	// Releases both watches (root and, if any, the open collection) in
	// one call -- stop_watching(handler) with no explicit node_ref drops
	// every node monitor registration this window's Handler() has, which
	// is exactly right at teardown (see _StopWatchingCollection()'s own
	// comment for why the SAME call would be wrong while switching
	// collections instead of tearing down entirely).
	stop_watching(this);

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

	delete fImportPanel;
	delete fExportPanel;
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
	fNameView = new VerseListNameView("verseListName",
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

	// No "Open"/"Save As" -- Go to List (arbitrarily deep since #78)
	// already covers navigation to anything in the standard tree, and
	// crossing that tree's boundary goes through Import/Export instead
	// of a raw file panel (#94). Relocating within the tree is #58, not
	// built yet. No "Close Verse List" or "Save" either -- closing this
	// window (which hides, not destroys, same as the search window)
	// already gets you away from the current list, and everything here
	// already writes itself immediately (rows, rename, description via
	// a debounce timer that _LoadFile() now flushes before switching
	// lists) -- a manual Save was only ever covering for that flush
	// gap, not a real save-vs-discard choice.
	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("New Verse List" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_NEW), 'N'));
	fileMenu->AddItem(new BMenuItem(
		B_TRANSLATE("Import Text List" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_IMPORT_PANEL)));
	fExportItem = new BMenuItem(
		B_TRANSLATE("Export Text List" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_EXPORT_PANEL));
	fileMenu->AddItem(fExportItem);
	fileMenu->AddSeparatorItem();
	fRenameItem = new BMenuItem(
		B_TRANSLATE("Rename List" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_RENAME));
	fileMenu->AddItem(fRenameItem);
	fileMenu->AddSeparatorItem();
	fDeleteItem = new BMenuItem(
		B_TRANSLATE("Delete File" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_DELETE));
	fileMenu->AddItem(fDeleteItem);
	menuBar->AddItem(fileMenu);

	BMenu* editMenu = new BMenu(B_TRANSLATE("Edit"));
	fAddReferenceItem = new BMenuItem(
		B_TRANSLATE("Add Reference" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_ADD_REFERENCE), 'R');
	editMenu->AddItem(fAddReferenceItem);
	fEditReferenceItem = new BMenuItem(
		B_TRANSLATE("Edit Reference" B_UTF8_ELLIPSIS),
		new BMessage(VLIST_EDIT_REFERENCE_SELECTED));
	editMenu->AddItem(fEditReferenceItem);
	fRemoveItem = new BMenuItem(B_TRANSLATE("Remove"),
		new BMessage(VLIST_REMOVE_SELECTED));
	editMenu->AddItem(fRemoveItem);
	editMenu->AddSeparatorItem();
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


// Minimal name-prompt window for "New Verse List..." and, reused as-is
// (#73), renaming the open one -- same idiom as this app's other small
// utility windows (e.g. SGDictionaryWindow): a non-modal BWindow that
// posts its result back via BMessenger and closes itself, rather than a
// blocking dialog (nothing in this app has one).
// Forward declaration -- defined further down, alongside its other user
// (_RebuildNavigationMenu()'s "Go to List" menu), but VerseListNamePromptWindow
// below needs it too, for its own location-picker popup.
static void PopulateCollectionMenu(BMenu* menu, BHandler* target,
	const char* path, uint32 what, const char* selfLabel);


static const uint32 kNamePromptOK = 'VLpo';
static const uint32 kNamePromptCancel = 'VLpc';
// #97: the optional location row is a real BMenuField dropdown, its menu
// built by the same PopulateCollectionMenu() "Go to List" uses -- clicking
// it shows the cascading tree right where the field is, direct selection,
// no separate button-then-popup step and no file panel.
static const uint32 kLocationMenuSelect = 'VLlm';

class VerseListNamePromptWindow : public BWindow {
public:
	// `windowTitle`/`buttonLabel`/`initialName`/`fieldLabel` default to
	// the "New Verse List" shape when NULL/empty -- _StartRename() and
	// _AddReference()/_StartEditReference() pass real ones instead,
	// pre-filling the field with whatever it's replacing and selecting
	// it, so typing immediately replaces it (matching how Tracker's own
	// inline rename already behaves). `index`, when not negative, rides
	// along in the result message as-is -- _EditReference() needs to
	// know which row it's rewriting, which row list.MouseDown() gave to
	// the context menu that opened this window in the first place.
	//
	// `showLocation` (#97) adds a second row: a BMenuField dropdown whose
	// menu is the same PopulateCollectionMenu() tree "Go to List" shows --
	// clicking it opens the cascading menu right there, direct selection
	// of the destination folder, no file panel. Only _NewList() and the
	// "nothing open" half of Import pass true; renaming/editing a
	// reference has no location to pick.
	VerseListNamePromptWindow(BMessenger target, uint32 resultWhat,
		const char* windowTitle = NULL, const char* initialName = NULL,
		const char* buttonLabel = NULL, const char* fieldLabel = NULL,
		int32 index = -1, bool showLocation = false)
		:
		BWindow(BRect(120, 120, 460, 210),
			windowTitle != NULL ? windowTitle
				: B_TRANSLATE("New Verse List"),
			B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
			B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE | B_AUTO_UPDATE_SIZE_LIMITS),
		fTarget(target),
		fResultWhat(resultWhat),
		fIndex(index),
		fLocationPath(BookmarkFile::RootDirectory()),
		fLocationField(NULL)
	{
		fNameControl = new BTextControl("name",
			fieldLabel != NULL ? fieldLabel : B_TRANSLATE("Name:"),
			initialName != NULL ? initialName : "", new BMessage(kNamePromptOK));
		BButton* cancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
			new BMessage(kNamePromptCancel));
		BButton* okButton = new BButton("ok", buttonLabel != NULL
			? buttonLabel : B_TRANSLATE("Create"), new BMessage(kNamePromptOK));
		SetDefaultButton(okButton);

		BLayoutBuilder::Group<> layout(this, B_VERTICAL);
		layout.SetInsets(B_USE_WINDOW_SPACING)
			.Add(fNameControl);

		if (showLocation) {
			BMenu* locationMenu = new BMenu("locationMenu");
			BMessage* selectRoot = new BMessage(kLocationMenuSelect);
			selectRoot->AddString("path", fLocationPath);
			locationMenu->AddItem(new BMenuItem(
				B_TRANSLATE("Verse Lists (top level)"), selectRoot));
			locationMenu->AddSeparatorItem();
			PopulateCollectionMenu(locationMenu, this, fLocationPath.String(),
				kLocationMenuSelect, B_TRANSLATE("(use this collection)"));
			locationMenu->SetTargetForItems(this);

			fLocationField = new BMenuField("location",
				B_TRANSLATE("Location:"), locationMenu);
			if (fLocationField->MenuItem() != NULL) {
				fLocationField->MenuItem()->SetLabel(
					B_TRANSLATE("Verse Lists (top level)"));
			}
			layout.Add(fLocationField);
		}

		layout.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancelButton)
				.Add(okButton)
			.End()
		.End();

		fNameControl->MakeFocus(true);
		if (initialName != NULL && initialName[0] != '\0')
			fNameControl->TextView()->SelectAll();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kNamePromptOK) {
			BString name(fNameControl->Text());
			name.Trim();
			if (!name.IsEmpty()) {
				BMessage result(fResultWhat);
				result.AddString("name", name);
				if (fIndex >= 0)
					result.AddInt32("index", fIndex);
				if (fLocationField != NULL)
					result.AddString("location", fLocationPath);
				fTarget.SendMessage(&result);
				Quit();
			}
			return;
		}
		if (message->what == kNamePromptCancel) {
			Quit();
			return;
		}
		if (message->what == kLocationMenuSelect) {
			BString path;
			if (message->FindString("path", &path) == B_OK) {
				fLocationPath = path;
				if (fLocationField != NULL
						&& fLocationField->MenuItem() != NULL) {
					BString label;
					if (path == BookmarkFile::RootDirectory()) {
						label = B_TRANSLATE("Verse Lists (top level)");
					} else {
						BPath bpath(path.String());
						label = bpath.Leaf() != NULL ? bpath.Leaf()
							: path.String();
					}
					fLocationField->MenuItem()->SetLabel(label.String());
				}
			}
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	BMessenger		fTarget;
	uint32			fResultWhat;
	int32			fIndex;
	BTextControl*	fNameControl;
	BString			fLocationPath;
	BMenuField*		fLocationField;
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
			BString name, location;
			if (message->FindString("name", &name) == B_OK) {
				message->FindString("location", &location);
				_CreateNewList(name.String(), location.String());
			}
			break;
		}

		case VLIST_RENAME:
			_StartRename();
			break;

		case VLIST_RENAME_RESULT:
		{
			BString name;
			if (message->FindString("name", &name) == B_OK)
				_RenameList(name.String());
			break;
		}

		case VLIST_IMPORT_PANEL:
			_ImportPanel();
			break;

		case VLIST_IMPORT_RESULT:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				_ImportTextFile(path.Path());
			}
			break;
		}

		case VLIST_IMPORT_NAME_RESULT:
		{
			BString name, location;
			if (message->FindString("name", &name) == B_OK) {
				message->FindString("location", &location);
				_ImportIntoNewList(name.String(), location.String());
			}
			break;
		}

		case VLIST_EXPORT_PANEL:
			_ExportPanel();
			break;

		case VLIST_EXPORT_RESULT:
		{
			entry_ref dirRef;
			BString name;
			if (message->FindRef("directory", &dirRef) == B_OK
				&& message->FindString("name", &name) == B_OK) {
				BPath dirPath(&dirRef);
				BPath filePath(dirPath.Path());
				filePath.Append(name.String());
				_ExportTextFile(filePath.Path());
			}
			break;
		}

		case VLIST_ADD_REFERENCE:
			_AddReference();
			break;

		case VLIST_ADD_REFERENCE_RESULT:
		{
			BString text;
			if (message->FindString("name", &text) == B_OK)
				_CreateReference(text.String());
			break;
		}

		case VLIST_EDIT_REFERENCE_SELECTED:
		{
			int32 selected = fRowList->CurrentSelection();
			if (selected >= 0)
				_StartEditReference(selected);
			break;
		}

		case VLIST_EDIT_REFERENCE_RESULT:
		{
			BString text;
			int32 index;
			if (message->FindString("name", &text) == B_OK
				&& message->FindInt32("index", &index) == B_OK) {
				_EditReference(index, text.String());
			}
			break;
		}

		case VLIST_REMOVE_SELECTED:
		{
			int32 selected = fRowList->CurrentSelection();
			if (selected >= 0)
				_RemoveRow(selected);
			break;
		}

		case VLIST_NAV_SELECT:
		{
			BString path;
			if (message->FindString("path", &path) == B_OK)
				_OpenList(path.String());
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
			_UpdateRowActionState();
			break;
		}

		case VLIST_DESCRIPTION_CHANGED:
			_SaveDescription();
			break;

		case B_NODE_MONITOR:
			_HandleNodeMonitorMessage(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
SGVerseListWindow::_BuildFilePanels()
{
	// B_FILE_NODE, not B_DIRECTORY_NODE -- both panels pick a plain-text
	// file (the import source / the export destination), not a
	// collection folder. Collection navigation itself goes through Go
	// to List, not a file panel (#94).
	fImportPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
		B_FILE_NODE, false, new BMessage(VLIST_IMPORT_RESULT));
	fImportPanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Import"));

	entry_ref dirRef;
	BEntry dirEntry(BookmarkFile::RootDirectory().String());
	dirEntry.GetRef(&dirRef);
	fExportPanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), &dirRef,
		B_FILE_NODE, false, new BMessage(VLIST_EXPORT_RESULT));
	fExportPanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Export"));
}


void
SGVerseListWindow::_NewList()
{
	VerseListNamePromptWindow* prompt = new VerseListNamePromptWindow(
		BMessenger(this), kNamePromptOK, NULL, NULL, NULL, NULL, -1, true);
	prompt->Show();
}


void
SGVerseListWindow::_StartRename()
{
	if (!fHasOpenFile)
		return;

	BPath current(fCollectionPath.String());
	VerseListNamePromptWindow* prompt = new VerseListNamePromptWindow(
		BMessenger(this), VLIST_RENAME_RESULT, B_TRANSLATE("Rename Verse List"),
		current.Leaf(), B_TRANSLATE("Rename"));
	prompt->Show();
}


void
SGVerseListWindow::_CreateNewList(const char* name, const char* parentPath)
{
	// #97: the New Verse List prompt's own location picker chooses the
	// parent (defaulting to the root) -- no longer always the top level.
	BString path = BookmarkFile::CreateCollection(
		parentPath != NULL && parentPath[0] != '\0' ? parentPath : NULL,
		name);
	if (path.IsEmpty())
		return;

	// Same flush-before-switch as _LoadFile() -- a list can already be
	// open (with a pending description edit) when "New Verse List" is
	// used, not just when nothing is.
	if (fDescriptionSaveRunner != NULL)
		_SaveDescription();

	fCollectionPath = path;
	fBookmarks.clear();
	fHasOpenFile = true;
	_WatchCollection(fCollectionPath.String());
	_RebuildRows();
	_RebuildDescription();
	_UpdateTitle();
	// A brand-new collection, possibly the first one ever, changes what
	// the navigation menu has to offer.
	_RebuildNavigationMenu();
}


void
SGVerseListWindow::_RenameList(const char* name)
{
	if (!fHasOpenFile)
		return;

	BString sanitized(name);
	sanitized.Trim();
	for (int32 i = 0; i < sanitized.Length(); i++) {
		if (sanitized.ByteAt(i) == '/')
			sanitized.SetByteAt(i, '-');
	}
	if (sanitized.Trim().IsEmpty())
		return;

	BEntry entry(fCollectionPath.String());
	if (entry.InitCheck() != B_OK)
		return;

	BPath parent(fCollectionPath.String());
	parent.GetParent(&parent);

	// Same numbered-collision handling as CreateCollection()/CreateNew()
	// -- a rename landing on an existing name gets a number, not a
	// silent overwrite or a failed, silently-ignored rename.
	BString candidate(sanitized);
	int suffix = 2;
	BPath candidatePath(parent.Path());
	candidatePath.Append(candidate.String());
	while (BEntry(candidatePath.Path()).Exists()) {
		candidate = sanitized;
		candidate << " " << suffix;
		candidatePath.SetTo(parent.Path());
		candidatePath.Append(candidate.String());
		suffix++;
	}

	if (entry.Rename(candidate.String()) != B_OK)
		return;

	BPath renamed(parent.Path());
	renamed.Append(candidate.String());
	fCollectionPath = renamed.Path();

	_UpdateTitle();
	_RebuildNavigationMenu();
}


void
SGVerseListWindow::_AddReference()
{
	if (!fHasOpenFile)
		return;

	VerseListNamePromptWindow* prompt = new VerseListNamePromptWindow(
		BMessenger(this), VLIST_ADD_REFERENCE_RESULT,
		B_TRANSLATE("Add Reference"), NULL, B_TRANSLATE("Add"),
		B_TRANSLATE("Reference:"));
	prompt->Show();
}


// Shared by _CreateReference()/_EditReference(): the typed text is
// whatever the user just wrote in their own current locale (the same
// universal Go to / Search box already accepts, e.g. English "John
// 3:16" or German "Johannes 3, 16") -- ConvertVerseReference() with the
// same locale/versification on both sides both validates it (false if
// it doesn't parse as a reference at all) and re-renders it in
// VerseKey's own canonical form, the same normalization a dropped
// reference already gets.
static bool
_NormalizeTypedReference(const char* text, BString& versification,
	BString& locale, BString& normalized)
{
	locale = CurrentLocaleCode();
	return ConvertVerseReference(text, locale.String(), versification.String(),
		locale.String(), versification.String(), normalized);
}


void
SGVerseListWindow::_CreateReference(const char* text)
{
	if (!fHasOpenFile)
		return;

	BString versification = _CollectionVersification();
	BString locale, normalized;
	if (!_NormalizeTypedReference(text, versification, locale, normalized)) {
		BString message(B_TRANSLATE("\"%text%\" isn't a Bible reference "
			"ScriptureGuide recognizes. Try something like \"John 3:16\" "
			"or \"Genesis 1:1-3\"."));
		message.ReplaceFirst("%text%", text);
		BAlert* alert = new BAlert(B_TRANSLATE("Add Reference"),
			message.String(), B_TRANSLATE("OK"));
		alert->Go();
		return;
	}

	BookmarkFile bookmark;
	if (bookmark.CreateNew(fCollectionPath.String(), normalized.String(),
			versification.String(), locale.String(),
			(int32)fBookmarks.size()) == B_OK) {
		fBookmarks.push_back(bookmark);
		_RebuildRows();
	}
}


void
SGVerseListWindow::_StartEditReference(int32 index)
{
	if (!fHasOpenFile || index < 0 || index >= (int32)fBookmarks.size())
		return;

	VerseListNamePromptWindow* prompt = new VerseListNamePromptWindow(
		BMessenger(this), VLIST_EDIT_REFERENCE_RESULT,
		B_TRANSLATE("Edit Reference"), fBookmarks[index].Reference(),
		B_TRANSLATE("Save"), B_TRANSLATE("Reference:"), index);
	prompt->Show();
}


void
SGVerseListWindow::_EditReference(int32 index, const char* text)
{
	if (!fHasOpenFile || index < 0 || index >= (int32)fBookmarks.size())
		return;

	BString versification = _CollectionVersification();
	BString locale, normalized;
	if (!_NormalizeTypedReference(text, versification, locale, normalized)) {
		BString message(B_TRANSLATE("\"%text%\" isn't a Bible reference "
			"ScriptureGuide recognizes. Try something like \"John 3:16\" "
			"or \"Genesis 1:1-3\"."));
		message.ReplaceFirst("%text%", text);
		BAlert* alert = new BAlert(B_TRANSLATE("Edit Reference"),
			message.String(), B_TRANSLATE("OK"));
		alert->Go();
		return;
	}

	fBookmarks[index].SetReference(normalized.String());
	if (fBookmarks[index].Save() == B_OK)
		_RebuildRows();
}


void
SGVerseListWindow::_OpenList(const char* path)
{
	_LoadFile(path);
}


void
SGVerseListWindow::_ImportPanel()
{
	if (fImportPanel != NULL)
		fImportPanel->Show();
}


// Title-cases each word of a filename-derived collection name
// ("JONATHAN LEVITE" -> "Jonathan Levite") -- the real end user's own
// sample files are all-caps, a DOS-era naming habit that would
// otherwise become the collection's visible name verbatim.
static BString
TitleCaseWords(const BString& raw)
{
	BString result(raw);
	bool startOfWord = true;
	for (int32 i = 0; i < result.Length(); i++) {
		char c = result.ByteAt(i);
		if (c == ' ' || c == '_' || c == '-') {
			startOfWord = true;
			continue;
		}
		result.SetByteAt(i, startOfWord ? toupper(c) : tolower(c));
		startOfWord = false;
	}
	return result;
}


// Splits `content` into lines and parses each as a reference the same
// way #68 always has -- one per line, no header, OSIS-style
// abbreviations like "EXO 4:14" that sword::VerseKey::setText() already
// accepts directly. A line that doesn't parse is skipped rather than
// aborting the whole import; there's no header line declaring a
// versification, so KJV (this format's own numbering) is assumed
// throughout.
static void
ParseReferenceLines(const BString& content, std::vector<BString>& outRefs)
{
	int32 lineStart = 0;
	while (lineStart <= content.Length()) {
		int32 lineEnd = content.FindFirst('\n', lineStart);
		if (lineEnd < 0)
			lineEnd = content.Length();

		BString line;
		content.CopyInto(line, lineStart, lineEnd - lineStart);
		line.Trim(); // also strips a trailing \r from a CRLF export

		if (!line.IsEmpty()) {
			sword::VerseKey key;
			key.setVersificationSystem("KJV");
			key.setText(line.String());
			if (key.popError() == 0)
				outRefs.push_back(BString(key.getText()));
		}

		lineStart = lineEnd + 1;
	}
}


static bool
ReadWholeFile(const char* path, BString& content)
{
	BFile file(path, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;

	off_t size = 0;
	file.GetSize(&size);
	if (size <= 0)
		return false;

	char* buffer = content.LockBuffer((int32)size);
	ssize_t bytesRead = file.Read(buffer, (size_t)size);
	content.UnlockBuffer(bytesRead > 0 ? bytesRead : 0);
	return !content.IsEmpty();
}


// #68/#95: imports into whichever collection is already open, the same
// way a manual Add Reference would, one call per line -- no new
// collection, no location to choose. Only when nothing is open does
// this need a destination at all; that path (#97) is
// _StartImportIntoNewList()/_ImportIntoNewList() below.
void
SGVerseListWindow::_ImportTextFile(const char* path)
{
	BString content;
	if (!ReadWholeFile(path, content))
		return;

	if (!fHasOpenFile) {
		_StartImportIntoNewList(path, content);
		return;
	}

	std::vector<BString> refs;
	ParseReferenceLines(content, refs);

	BString versification = _CollectionVersification();
	int32 position = (int32)fBookmarks.size();
	bool appendedAny = false;
	for (size_t i = 0; i < refs.size(); i++) {
		BookmarkFile bookmark;
		if (bookmark.CreateNew(fCollectionPath.String(), refs[i].String(),
				versification.String(), "", position) == B_OK) {
			fBookmarks.push_back(bookmark);
			position++;
			appendedAny = true;
		}
	}
	if (appendedAny)
		_RebuildRows();
}


// #97: nothing was open, so unlike the merge path above this needs a
// destination -- reuses the same name-plus-location prompt New Verse
// List already has, pre-filled with a name derived from the file, and
// remembers the source content (`fPendingImportContent`) until the
// prompt returns.
void
SGVerseListWindow::_StartImportIntoNewList(const char* path,
	const BString& content)
{
	fPendingImportContent = content;

	BPath sourcePath(path);
	BString name(sourcePath.Leaf());
	int32 dot = name.FindLast('.');
	if (dot > 0)
		name.Truncate(dot);
	name = TitleCaseWords(name);

	VerseListNamePromptWindow* prompt = new VerseListNamePromptWindow(
		BMessenger(this), VLIST_IMPORT_NAME_RESULT,
		B_TRANSLATE("Import Text List"), name.String(),
		B_TRANSLATE("Import"), NULL, -1, true);
	prompt->Show();
}


void
SGVerseListWindow::_ImportIntoNewList(const char* name,
	const char* parentPath)
{
	if (fPendingImportContent.IsEmpty())
		return;

	BString collectionPath = BookmarkFile::CreateCollection(
		parentPath != NULL && parentPath[0] != '\0' ? parentPath : NULL,
		name);
	if (collectionPath.IsEmpty())
		return;

	std::vector<BString> refs;
	ParseReferenceLines(fPendingImportContent, refs);

	int32 position = 0;
	for (size_t i = 0; i < refs.size(); i++) {
		BookmarkFile bookmark;
		if (bookmark.CreateNew(collectionPath.String(), refs[i].String(),
				"KJV", "", position) == B_OK) {
			position++;
		}
	}

	fPendingImportContent = "";

	_LoadFile(collectionPath.String());
	_RebuildNavigationMenu();
}


void
SGVerseListWindow::_ExportPanel()
{
	if (fExportPanel == NULL || !fHasOpenFile)
		return;

	BPath current(fCollectionPath.String());
	BString suggestedName(current.Leaf() != NULL ? current.Leaf() : "list");
	suggestedName << ".txt";
	fExportPanel->SetSaveText(suggestedName.String());
	fExportPanel->Show();
}


// #102: the reverse of _ImportTextFile() -- one reference per line, no
// header, the same plain-text shape Import reads back in. Deliberately
// just the references themselves, not position/tags/locale/
// versification -- portable and human-readable rather than a full
// round-trip of everything a bookmark file carries.
void
SGVerseListWindow::_ExportTextFile(const char* path)
{
	if (!fHasOpenFile)
		return;

	BFile file(path, B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	BString body;
	for (size_t i = 0; i < fBookmarks.size(); i++)
		body << fBookmarks[i].Reference() << "\n";
	file.Write(body.String(), body.Length());
}


void
SGVerseListWindow::_CloseList()
{
	_StopWatchingCollection();
	fCollectionPath = "";
	fBookmarks.clear();
	fHasOpenFile = false;
	_RebuildRows();
	_RebuildDescription();
	_UpdateTitle();
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

	// Flush a still-pending description edit against the OLD
	// fCollectionPath before it's overwritten below -- without this,
	// switching lists (Go to List) while the debounce timer from
	// _DescriptionEdited() hadn't fired yet silently discarded the edit
	// once _RebuildDescription() re-read the file from disk.
	if (fDescriptionSaveRunner != NULL)
		_SaveDescription();

	fCollectionPath = path;
	fBookmarks.clear();
	_WatchCollection(fCollectionPath.String());

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




void
SGVerseListWindow::_RebuildRows()
{
	fRowList->MakeEmpty();
	for (size_t i = 0; i < fBookmarks.size(); i++)
		fRowList->AddItem(new BStringItem(fBookmarks[i].Reference()));
	// MakeEmpty() clears the selection -- Edit Reference/Remove/Move Up/
	// Move Down all need to fall back to disabled rather than keep
	// whatever state a previous, now-gone selection left them in.
	_UpdateRowActionState();
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
	fExportItem->SetEnabled(onList);
	fRenameItem->SetEnabled(onList);
	fDeleteItem->SetEnabled(onList);
	fAddReferenceItem->SetEnabled(onList);
	_UpdateRowActionState();
}


// Edit Reference/Remove/Move Up/Move Down all act on whichever row is
// currently selected -- unlike the rest of _UpdateTitle()'s items, their
// enabled state depends on selection, not just whether a list is open,
// so this needs to run both when the list itself changes and every time
// the row selection does (see VLIST_ROW_SELECTED).
void
SGVerseListWindow::_UpdateRowActionState()
{
	int32 selected = fHasOpenFile ? fRowList->CurrentSelection() : -1;
	bool hasSelection = selected >= 0;
	fEditReferenceItem->SetEnabled(hasSelection);
	fRemoveItem->SetEnabled(hasSelection);
	fMoveUpItem->SetEnabled(hasSelection && selected > 0);
	fMoveDownItem->SetEnabled(hasSelection
		&& selected + 1 < fRowList->CountItems());
}


// Adds one item per subfolder of `path` directly into `menu` (#78) --
// arbitrarily nested, unlike the original one-level-only design
// (BookmarkFile::ListCollectionNames()'s own scope comment describes
// that original limit, not this caller's). A folder with no
// sub-collections of its own becomes a plain leaf item; one that does
// becomes a submenu, its OWN folder still reachable as that submenu's
// first item (a plain click on a submenu's title isn't a thing in
// Haiku's menu model, so this is the equivalent), followed by a
// separator, then its children recursed the same way. Every submenu
// created needs its own SetTargetForItems() call -- unlike a plain
// BView, it does not inherit targeting from its parent menu.
//
// `what`/`selfLabel` are parameterized (#97) so this same tree-walk
// builds both "Go to List" (VLIST_NAV_SELECT, "open this collection")
// and the New-List/Import location picker's popup (a different message
// so the two don't collide, "use this collection" reads better for a
// destination pick than a navigation).
static void
PopulateCollectionMenu(BMenu* menu, BHandler* target, const char* path,
	uint32 what, const char* selfLabel)
{
	std::vector<BString> names = BookmarkFile::ListCollectionNames(path);
	for (size_t i = 0; i < names.size(); i++) {
		BPath childPath(path);
		childPath.Append(names[i].String());

		std::vector<BString> grandchildren
			= BookmarkFile::ListCollectionNames(childPath.Path());
		if (grandchildren.empty()) {
			BMessage* select = new BMessage(what);
			select->AddString("path", childPath.Path());
			menu->AddItem(new BMenuItem(names[i].String(), select));
			continue;
		}

		BMenu* submenu = new BMenu(names[i].String());
		BMessage* selectSelf = new BMessage(what);
		selectSelf->AddString("path", childPath.Path());
		submenu->AddItem(new BMenuItem(selfLabel, selectSelf));
		submenu->AddSeparatorItem();
		PopulateCollectionMenu(submenu, target, childPath.Path(), what,
			selfLabel);
		submenu->SetTargetForItems(target);
		menu->AddItem(submenu);
	}
}


void
SGVerseListWindow::_RebuildNavigationMenu()
{
	if (fNavigationMenu == NULL)
		return;
	for (int32 i = fNavigationMenu->CountItems() - 1; i >= 0; i--)
		delete fNavigationMenu->RemoveItem(i);

	// Every collection is a folder now (#55), nested arbitrarily deep --
	// see PopulateCollectionMenu()'s own comment.
	BString root = BookmarkFile::RootDirectory();
	PopulateCollectionMenu(fNavigationMenu, this, root.String(),
		VLIST_NAV_SELECT, B_TRANSLATE("(open this collection)"));

	fNavigationMenu->SetTargetForItems(this);
}


void
SGVerseListWindow::_WatchRoot()
{
	if (fWatchingRoot)
		return;

	BString root = BookmarkFile::RootDirectory();
	BEntry entry(root.String());
	if (entry.InitCheck() != B_OK
		|| entry.GetNodeRef(&fRootNodeRef) != B_OK) {
		return;
	}

	// B_WATCH_DIRECTORY: a collection folder appearing, disappearing, or
	// being renamed directly inside RootDirectory() -- what the "Go to
	// List" menu's TOP level shows. A collection nested further down
	// changing is only caught if it's the one currently open (see
	// _WatchCollection()) -- see the header comment on why this doesn't
	// try to watch the whole tree recursively.
	if (watch_node(&fRootNodeRef, B_WATCH_DIRECTORY, this) == B_OK)
		fWatchingRoot = true;
}


void
SGVerseListWindow::_WatchCollection(const char* path)
{
	_StopWatchingCollection();

	BEntry entry(path);
	if (entry.InitCheck() != B_OK
		|| entry.GetNodeRef(&fCollectionNodeRef) != B_OK) {
		return;
	}

	if (watch_node(&fCollectionNodeRef, B_WATCH_DIRECTORY, this) == B_OK)
		fWatchingCollection = true;
}


void
SGVerseListWindow::_StopWatchingCollection()
{
	if (!fWatchingCollection)
		return;

	// The specific node_ref, not stop_watching(this) -- that call has no
	// node_ref parameter at all and would drop EVERY watch this window's
	// Handler() has, including the root one from _WatchRoot(), which is
	// wrong here: this only means "stop watching the collection that was
	// open, a different one may be opening next" (see _LoadFile()/
	// _CreateNewList(), both call _WatchCollection() again right after),
	// not "tear this window's watches down entirely" (~SGVerseListWindow()
	// is the one place that actually wants that).
	watch_node(&fCollectionNodeRef, B_STOP_WATCHING, this);
	fWatchingCollection = false;
}


void
SGVerseListWindow::_HandleNodeMonitorMessage(BMessage* message)
{
	int32 opcode;
	if (message->FindInt32("opcode", &opcode) != B_OK)
		return;

	// B_ENTRY_CREATED/REMOVED report the event's own parent under
	// "directory"; B_ENTRY_MOVED reports both ends, since a move can
	// cross from one watched directory into the other (e.g. a
	// collection dragged into/out of the currently open one in
	// Tracker). Either side matching either watched node is enough to
	// know something relevant happened, whether the reference itself
	// (dev_t, node) points into the watched directory or the entry
	// underneath it.
	int32 device;
	if (message->FindInt32("device", &device) != B_OK)
		return;

	bool touchesRoot = false;
	bool touchesCollection = false;

	int64 directory;
	if (message->FindInt64("directory", &directory) == B_OK) {
		if (fWatchingRoot && device == fRootNodeRef.device
			&& directory == fRootNodeRef.node) {
			touchesRoot = true;
		}
		if (fWatchingCollection && device == fCollectionNodeRef.device
			&& directory == fCollectionNodeRef.node) {
			touchesCollection = true;
		}
	}
	int64 fromDirectory, toDirectory;
	if (opcode == B_ENTRY_MOVED
		&& message->FindInt64("from directory", &fromDirectory) == B_OK
		&& message->FindInt64("to directory", &toDirectory) == B_OK) {
		if (fWatchingRoot && device == fRootNodeRef.device
			&& (fromDirectory == fRootNodeRef.node
				|| toDirectory == fRootNodeRef.node)) {
			touchesRoot = true;
		}
		if (fWatchingCollection && device == fCollectionNodeRef.device
			&& (fromDirectory == fCollectionNodeRef.node
				|| toDirectory == fCollectionNodeRef.node)) {
			touchesCollection = true;
		}
	}

	if (touchesRoot)
		_RebuildNavigationMenu();

	if (touchesCollection && fHasOpenFile) {
		// Re-scan rather than a full _LoadFile() -- that would also
		// reset the description box's own listener dance for no reason;
		// only the row list needs to follow what changed on disk.
		fBookmarks.clear();
		std::vector<BString> paths
			= BookmarkFile::ListBookmarkPaths(fCollectionPath.String());
		for (size_t i = 0; i < paths.size(); i++) {
			BookmarkFile bookmark;
			if (bookmark.SetTo(paths[i].String()) == B_OK)
				fBookmarks.push_back(bookmark);
		}
		_RebuildRows();
	}
}


void
SGVerseListWindow::_NavigateToRow(int32 index)
{
	if (index < 0 || index >= (int32)fBookmarks.size() || fMessenger == NULL)
		return;

	// BookmarkFile::NavigationKey(), not Reference() directly --
	// BookFromKey()/ChapterFromKey()/VerseFromKey() (what JumpToKey()
	// below actually calls) always parse under the CURRENT system
	// locale, so handing them the reference's own (possibly different)
	// locale would only work by coincidence, when that still happens to
	// match. See NavigationKey()'s own comment.
	//
	// Exactly the path a dropped reference or the universal search box
	// already take (see BibleColumnView::_HandleReferenceDrop() and
	// SGMainWindow::MessageReceived()'s own SG_BIBLE case) -- no new
	// navigation mechanism, just another source for the same message.
	// It reaches the OWNING window's active chain, not necessarily
	// whichever SGMainWindow currently has focus.
	BMessage jump(SG_BIBLE);
	jump.AddString("key", fBookmarks[index].NavigationKey());
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

	// What every new bookmark's own content gets written in -- see
	// BookmarkFile::Locale()'s own comment on why storing the CURRENT
	// locale (rather than forcing English) plus recording which one is
	// the portable choice, not the un-portable one.
	BString currentLocale = CurrentLocaleCode();

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
		if (!ConvertVerseReference(key, currentLocale.String(),
				sourceVersification.String(), currentLocale.String(),
				targetVersification.String(), startText)) {
			continue;
		}

		BString line(startText);
		if (hasRange) {
			BString endText;
			if (ConvertVerseReference(endKey.String(), currentLocale.String(),
					sourceVersification.String(), currentLocale.String(),
					targetVersification.String(), endText)
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
				targetVersification.String(), currentLocale.String(),
				(int32)fBookmarks.size())
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
