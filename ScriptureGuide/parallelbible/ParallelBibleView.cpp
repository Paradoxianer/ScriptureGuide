/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "ParallelBibleView.h"

#include <GroupLayout.h>
#include <LayoutItem.h>

#include <versekey.h>

#include "VerseAligner.h"

const float ParallelBibleView::kMinColumnWidth = 150.0f;


// Persists edits made in the notes column back into the personal SWORD
// module, one verse (== one paragraph) at a time.
class NotesWriteBackListener : public TextListener {
public:
	NotesWriteBackListener(BibleTextDocument* document,
		PersonalNotesModule* notes, const BString& chapterKey)
		:
		fDocument(document),
		fNotes(notes),
		fChapterKey(chapterKey)
	{
	}

	virtual void TextChanged(const TextChangedEvent& event)
	{
		int32 first = event.FirstChangedParagraph();
		int32 count = event.ChangedParagraphCount();
		for (int32 i = first; i < first + count; i++) {
			int verse = fDocument->VerseForParagraphIndex(i);
			if (verse < 0)
				continue;

			VerseKey key;
			key.setText(fChapterKey.String());
			key.setVerse(verse);

			BString text = fDocument->ParagraphAtIndex(i).Text();
			fNotes->SetNote(key.getText(), text.String());
		}
	}

private:
	BibleTextDocument*		fDocument;
	PersonalNotesModule*	fNotes;
	BString					fChapterKey;
};


ParallelBibleView::ParallelBibleView(const char* name, SWMgr* manager)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS, new BGroupLayout(B_HORIZONTAL)),
	fManager(manager),
	fGroupLayout(NULL),
	fNotes(NULL),
	fNotesView(NULL)
{
	fGroupLayout = (BGroupLayout*)GetLayout();
}


ParallelBibleView::~ParallelBibleView()
{
	delete fNotes;
}


void
ParallelBibleView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_RebuildLayout();
}


void
ParallelBibleView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);

	float columnWidth = _ColumnWidth();
	for (size_t i = 0; i < fTextViews.size(); i++) {
		fTextViews[i]->SetExplicitMinSize(BSize(columnWidth, B_SIZE_UNSET));
		fTextViews[i]->SetExplicitMaxSize(
			BSize(columnWidth, B_SIZE_UNLIMITED));
		fTextViews[i]->SetExplicitPreferredSize(
			BSize(columnWidth, B_SIZE_UNSET));
	}

	_Realign();
}


status_t
ParallelBibleView::AddColumn(const char* moduleName)
{
	if (fManager == NULL)
		return B_NO_INIT;

	SWModule* module = fManager->getModule(moduleName);
	if (module == NULL)
		return B_NAME_NOT_FOUND;

	// Render filters (GBFPlain etc.) are expected to already be configured
	// on fManager, the same way SwordBackend configures its SWMgr with a
	// MarkupFilterMgr for every module it manages.
	if (!fCurrentKey.IsEmpty())
		module->setKey(fCurrentKey.String());

	fModules.push_back(module);
	fDocuments.push_back(
		BReference<BibleTextDocument>(new BibleTextDocument(module), true));

	_RebuildLayout();
	return B_OK;
}


status_t
ParallelBibleView::RemoveColumn(int32 index)
{
	if (index < 0 || (size_t)index >= fModules.size())
		return B_BAD_INDEX;

	fModules.erase(fModules.begin() + index);
	fDocuments.erase(fDocuments.begin() + index);

	_RebuildLayout();
	return B_OK;
}


int32
ParallelBibleView::CountColumns() const
{
	return (int32)fModules.size();
}


status_t
ParallelBibleView::SetNotesEnabled(bool enabled)
{
	if (enabled == (fNotes != NULL))
		return B_OK;

	if (enabled) {
		fNotes = new PersonalNotesModule();
		status_t status = fNotes->Open();
		if (status != B_OK) {
			delete fNotes;
			fNotes = NULL;
			return status;
		}

		fNotesDocument = BReference<BibleTextDocument>(
			new BibleTextDocument(fNotes->Module()), true);
		fNotesDocument->SetShowVerseNumbers(false);
		fNotesDocument->SetSkipEmptyVerses(false);
		if (!fCurrentKey.IsEmpty())
			fNotesDocument->SetKey(fCurrentKey.String());

		fNotesDocument->AddListener(TextListenerRef(
			new NotesWriteBackListener(fNotesDocument.Get(), fNotes,
				fCurrentKey), true));
	} else {
		fNotesDocument = BReference<BibleTextDocument>();
		delete fNotes;
		fNotes = NULL;
	}

	_RebuildLayout();
	return B_OK;
}


status_t
ParallelBibleView::SetKey(const char* key)
{
	fCurrentKey = key;

	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->SetKey(key);
	if (fNotesDocument.Get() != NULL)
		fNotesDocument->SetKey(key);

	_Realign();
	return B_OK;
}


status_t
ParallelBibleView::NextChapter()
{
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->NextChapter();
	if (fNotesDocument.Get() != NULL)
		fNotesDocument->NextChapter();

	if (!fDocuments.empty())
		fCurrentKey = fDocuments[0]->Key();
	else if (fNotesDocument.Get() != NULL)
		fCurrentKey = fNotesDocument->Key();

	_Realign();
	return B_OK;
}


status_t
ParallelBibleView::PrevChapter()
{
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->PrevChapter();
	if (fNotesDocument.Get() != NULL)
		fNotesDocument->PrevChapter();

	if (!fDocuments.empty())
		fCurrentKey = fDocuments[0]->Key();
	else if (fNotesDocument.Get() != NULL)
		fCurrentKey = fNotesDocument->Key();

	_Realign();
	return B_OK;
}


void
ParallelBibleView::_RebuildLayout()
{
	while (fGroupLayout->CountItems() > 0) {
		BLayoutItem* item = fGroupLayout->RemoveItem((int32)0);
		BView* view = item->View();
		delete item;
		if (view != NULL) {
			view->RemoveSelf();
			delete view;
		}
	}
	fTextViews.clear();
	fNotesView = NULL;

	float width = _ColumnWidth();

	for (size_t i = 0; i < fDocuments.size(); i++) {
		TextDocumentView* view = new TextDocumentView("bibleColumn");
		view->SetInsets(4.0f);
		view->SetSelectionEnabled(true);
		view->SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
		view->SetExplicitMaxSize(BSize(width, B_SIZE_UNLIMITED));
		view->SetExplicitPreferredSize(BSize(width, B_SIZE_UNSET));
		view->SetTextDocument(fDocuments[i]);
		fGroupLayout->AddView(view);
		fTextViews.push_back(view);
	}

	if (fNotesDocument.Get() != NULL) {
		TextDocumentView* view = new TextDocumentView("notesColumn");
		view->SetInsets(4.0f);
		view->SetSelectionEnabled(true);
		view->SetEditingEnabled(true);
		view->SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
		view->SetExplicitMaxSize(BSize(width, B_SIZE_UNLIMITED));
		view->SetExplicitPreferredSize(BSize(width, B_SIZE_UNSET));
		view->SetTextDocument(fNotesDocument);
		fGroupLayout->AddView(view);
		fNotesView = view;
	}

	_Realign();
}


void
ParallelBibleView::_Realign()
{
	std::vector<BibleTextDocument*> columns;
	for (size_t i = 0; i < fDocuments.size(); i++)
		columns.push_back(fDocuments[i].Get());
	if (fNotesDocument.Get() != NULL)
		columns.push_back(fNotesDocument.Get());

	if (columns.size() >= 2)
		VerseAligner::Align(columns, _ColumnWidth());

	for (size_t i = 0; i < fTextViews.size(); i++)
		fTextViews[i]->Relayout();
	if (fNotesView != NULL)
		fNotesView->Relayout();
}


float
ParallelBibleView::_ColumnWidth() const
{
	int32 columnCount = (int32)fModules.size() + (fNotesView != NULL ? 1 : 0);
	if (columnCount == 0)
		return Bounds().Width();

	float spacing = fGroupLayout->Spacing();
	float available = Bounds().Width() - spacing * (columnCount - 1);
	float width = available / columnCount;
	if (width < kMinColumnWidth)
		width = kMinColumnWidth;
	return width;
}
