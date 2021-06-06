/*
 * Copyright 2020, Paradoxon <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Catalog.h>
#include <Debug.h>
#include <Locale.h>
#include <Rect.h>
#include <StringView.h>

#include <gbfplain.h>
#include <gbfstrongs.h>
#include <localemgr.h>
#include <markupfiltmgr.h>
#include <swmgr.h>
#include <versekey.h>

#include "constants.h"
#include "BibleTextColumn.h"
#include "BibleTextField.h"

using namespace sword;
using namespace std;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BibleTextColumn"

#define CONFIGPATH MODULES_PATH

BibleTextColumn::BibleTextColumn(SWMgr *manager, SWModule* mod,  
										float width, float minWidth,
										float maxWidth, alignment align) 
	: BTitledColumn("Select Bible", width, minWidth, maxWidth,align),
	fIsLineBreak(false),
	fShowVerseNumbers(true),
	fManager(manager),
	fModule(mod)
{
	Init();
}

BibleTextColumn::BibleTextColumn(SWMgr *manager, const char *moduleName, 
										float width, float minWidth,
										float maxWidth, alignment align) 
	: BTitledColumn("Select Bible", width, minWidth, maxWidth,align),
	fIsLineBreak(false),
	fShowVerseNumbers(true),
	fManager(manager)
{
	printf("Module %s\n",moduleName);
	SetModule(moduleName);
	Init();
}

void BibleTextColumn::~BibleTextColumn(){
}


void BibleTextColumn::Init()
{
	BLocale::Default()->GetLanguage(&language);
	verseRenderer	= new BTextView("verseRenderer");
	verseBitmap		= NULL;
	SetTitle(fModule->getName());
}


void BibleTextColumn::DrawField(BField* _field, BRect rect, BView* parent)
{
	BibleTextField* field = static_cast<BibleTextField*>(_field);
	field->SetWidth(rect.Width());
	fModule->setKey(field->Key());
	field->SetHeight(verseRenderer->TextRect().Height());
	DrawBibeltext(fModule->renderText(), parent, rect);
}


int BibleTextColumn::CompareFields(BField* field1, BField* field2)
{
	const BibleTextField *first = dynamic_cast<const BibleTextField*>(field1);
	const BibleTextField *second = dynamic_cast<const BibleTextField*>(field2);
	if (first!=NULL & second!=NULL)
	{
//		return (first->Key() == second->Key());
	}
	else
		return -1;
}


float BibleTextColumn::GetPreferredWidth(BField* field, BView* parent) const
{
}


bool BibleTextColumn::AcceptsField(const BField* field) const
{
	return static_cast<bool>(dynamic_cast<const BibleTextField*>(field));
}

void BibleTextColumn::MouseMoved(BColumnListView* parent, BRow* row,
							BField* field, BRect fieldRect,
							BPoint point, uint32 buttons, int32 code)
{
	if (!fSelectionEnabled)
		return;

	BCursor iBeamCursor(B_CURSOR_ID_I_BEAM);
	SetViewCursor(&iBeamCursor);

	if (fMouseDown)
		SetCaret(where, true);
}


void BibleTextColumn::MouseDown(BColumnListView* parent, BRow* row,
							BField* field, BRect fieldRect,
							BPoint point, uint32 buttons)
{
	if (!fSelectionEnabled)
		return;

	MakeFocus();

	int32 modifiers = 0;
	int32 clicks	= 0;
	if (Window() != NULL && Window()->CurrentMessage() != NULL){
		Window()->CurrentMessage()->FindInt32("modifiers", &modifiers);
		Window()->CurrentMessage()->FindInt32("clicks", &clicks);
	}
	if (clicks >1){
		
		bool rightOfCenter;
		int32 offset = fTextDocumentLayout.TextOffsetAt(where.x,where.y,rightOfCenter);
		float x1,y1,x2,y2;
		fTextDocumentLayout.GetTextBounds(offset,x1,y2,x2,y2);
		SetCaret(BPoint(x1,y1),false);
		SetCaret(BPoint(x2,y2), true);

		//TODO if its 2 then select the TextSpan
		//find the textoffset where the TextSpan starts
		//TextSpan.Text() ->Length
		//if its 3 then select the whole Paragraph
		//find the textoffset where the Paragraph starts
		//ParagraphAt -> Paragraph.Length()
	}
	else {
		fMouseDown = true;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		bool extendSelection = (modifiers & B_SHIFT_KEY) != 0;
		SetCaret(where, extendSelection);
	}
}


void BibleTextColumn::MouseUp(BColumnListView* parent, BRow* row,
							BField* field)
{
		fMouseDown = false;

}


status_t BibleTextColumn::SetModule(SWModule* mod)
{
	fModule=mod;
	//@ToDo Update everything
}


status_t BibleTextColumn::SetModule(const char* modulName)
{
	fModule	= fManager->getModule(modulName);
	//@ToDo Update everything
}


SWModule* BibleTextColumn::CurrentModule(void)
{
	return fModule;
}


void BibleTextColumn::DrawBibeltext(const char* string, BView* parent, BRect rect)
{
	verseRenderer->ResizeTo(rect.Width(),rect.Height());
	verseRenderer->SetTextRect(BRect(0,0,rect.Width(),rect.Height()));
	verseRenderer->SetText(string);
	BRect textRect = verseRenderer->TextRect();
	BRect bitmapRect = textRect;
	verseBitmap = new BBitmap(bitmapRect, B_RGBA32, true, false);
	if (verseBitmap != NULL && verseBitmap->Lock()) 
	{
		verseBitmap->AddChild(verseRenderer);
		verseRenderer->Draw(textRect);
		verseRenderer->Sync();
		verseBitmap->Unlock();
	}
	parent->DrawBitmap(verseBitmap, BPoint(rect.left, rect.top));
	if (verseBitmap != NULL && verseBitmap->Lock()) 
	{
		verseRenderer->RemoveSelf();
		verseBitmap->Unlock();
	}
	delete verseBitmap;
	verseBitmap = NULL;
}


void  BibleTextColumn::SetShowVerseNumbers(bool showVerseNumbers)
{
	if (fShowVerseNumbers != showVerseNumbers)
	{
		fShowVerseNumbers=showVerseNumbers;
	}
};


BSize
BibleTextColumn::MinSize()
{
	return BSize(fInsetLeft + fInsetRight + 50.0f, fInsetTop + fInsetBottom);
}


BSize
BibleTextColumn::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


BSize
BibleTextColumn::PreferredSize()
{
	return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


bool
BibleTextColumn::HasHeightForWidth()
{
	return true;
}


void
BibleTextColumn::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	TextDocumentLayout layout(fTextDocumentLayout);
	layout.SetWidth(_TextLayoutWidth(width));

	float height = layout.Height() + 1 + fInsetTop + fInsetBottom;

	if (min != NULL)
		*min = height;
	if (max != NULL)
		*max = height;
	if (preferred != NULL)
		*preferred = height;
}


void
BibleTextColumn::SetTextDocument(const TextDocumentRef& document)
{
	fTextDocument = document;
	fTextDocumentLayout.SetTextDocument(fTextDocument);
	if (fTextEditor.Get() != NULL)
		fTextEditor->SetDocument(document);

	InvalidateLayout();
	Invalidate();
}


void
BibleTextColumn::SetEditingEnabled(bool enabled)
{
	if (fTextEditor.Get() != NULL)
		fTextEditor->SetEditingEnabled(enabled);
}


void
BibleTextColumn::SetTextEditor(const TextEditorRef& editor)
{
	if (fTextEditor == editor)
		return;

	if (fTextEditor.Get() != NULL) {
		fTextEditor->SetDocument(TextDocumentRef());
		fTextEditor->SetLayout(TextDocumentLayoutRef());
		// TODO: Probably has to remove listeners
	}

	fTextEditor = editor;

	if (fTextEditor.Get() != NULL) {
		fTextEditor->SetDocument(fTextDocument);
		fTextEditor->SetLayout(TextDocumentLayoutRef(
			&fTextDocumentLayout));
		// TODO: Probably has to add listeners
	}
}


void
BibleTextColumn::SetInsets(float inset)
{
	SetInsets(inset, inset, inset, inset);
}


void
BibleTextColumn::SetInsets(float horizontal, float vertical)
{
	SetInsets(horizontal, vertical, horizontal, vertical);
}


void
BibleTextColumn::SetInsets(float left, float top, float right, float bottom)
{
	if (fInsetLeft == left && fInsetTop == top
		&& fInsetRight == right && fInsetBottom == bottom) {
		return;
	}

	fInsetLeft = left;
	fInsetTop = top;
	fInsetRight = right;
	fInsetBottom = bottom;

	InvalidateLayout();
	Invalidate();
}


void
BibleTextColumn::SetSelectionEnabled(bool enabled)
{
	if (fSelectionEnabled == enabled)
		return;
	fSelectionEnabled = enabled;
	Invalidate();
	// TODO: Deselect
}


void
BibleTextColumn::SetCaret(BPoint location, bool extendSelection)
{
	if (!fSelectionEnabled || fTextEditor.Get() == NULL)
		return;

	location.x -= fInsetLeft;
	location.y -= fInsetTop;

	fTextEditor->SetCaret(location, extendSelection);
	_ShowCaret(!extendSelection);
	Invalidate();
}


void
BibleTextColumn::SelectAll()
{
	if (!fSelectionEnabled || fTextEditor.Get() == NULL)
		return;

	fTextEditor->SelectAll();
	_ShowCaret(false);
	Invalidate();
}


bool
BibleTextColumn::HasSelection() const
{
	return fTextEditor.Get() != NULL && fTextEditor->HasSelection();
}


void
BibleTextColumn::GetSelection(int32& start, int32& end) const
{
	if (fTextEditor.Get() != NULL) {
		start = fTextEditor->SelectionStart();
		end = fTextEditor->SelectionEnd();
	}
}

float
BibleTextColumn::_TextLayoutWidth(float viewWidth) const
{
	return viewWidth - (fInsetLeft + fInsetRight);
}


void
BibleTextColumn::_ShowCaret(bool show)
{
	fShowCaret = show;
	if (fCaretBounds.IsValid())
		Invalidate(fCaretBounds);
	else
		Invalidate();
	// Cancel previous blinker, increment blink token so we only accept
	// the message from the blinker we just created
	fCaretBlinkToken++;
	BMessage message(MSG_BLINK_CARET);
	message.AddInt32("token", fCaretBlinkToken);
	delete fCaretBlinker;
	fCaretBlinker = new BMessageRunner(BMessenger(this), &message,
		500000, 1);
}	


void
BibleTextColumn::_BlinkCaret()
{
	if (!fSelectionEnabled || fTextEditor.Get() == NULL)
		return;

	_ShowCaret(!fShowCaret);
}


void
BibleTextColumn::_DrawCaret(int32 textOffset)
{
	if (!IsFocus() || Window() == NULL || !Window()->IsActive())
		return;

	float x1;
	float y1;
	float x2;
	float y2;

	fTextDocumentLayout.GetTextBounds(textOffset, x1, y1, x2, y2);
	x2 = x1 + 1;

	fCaretBounds = BRect(x1, y1, x2, y2);
	fCaretBounds.OffsetBy(fInsetLeft, fInsetTop);

	SetDrawingMode(B_OP_INVERT);
	FillRect(fCaretBounds);
}


void
BibleTextColumn::_DrawSelection()
{
	int32 start;
	int32 end;
	GetSelection(start, end);

	BShape shape;
	_GetSelectionShape(shape, start, end);

	SetDrawingMode(B_OP_SUBTRACT);

	SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	MovePenTo(fInsetLeft - 0.5f, fInsetTop - 0.5f);

	if (IsFocus() && Window() != NULL && Window()->IsActive()) {
		SetHighColor(30, 30, 30);
		FillShape(&shape);
	}

	SetHighColor(40, 40, 40);
	StrokeShape(&shape);
}


void
BibleTextColumn::_GetSelectionShape(BShape& shape, int32 start, int32 end)
{
	float startX1;
	float startY1;
	float startX2;
	float startY2;
	fTextDocumentLayout.GetTextBounds(start, startX1, startY1, startX2,
		startY2);

	startX1 = floorf(startX1);
	startY1 = floorf(startY1);
	startX2 = ceilf(startX2);
	startY2 = ceilf(startY2);

	float endX1;
	float endY1;
	float endX2;
	float endY2;
	fTextDocumentLayout.GetTextBounds(end, endX1, endY1, endX2, endY2);

	endX1 = floorf(endX1);
	endY1 = floorf(endY1);
	endX2 = ceilf(endX2);
	endY2 = ceilf(endY2);

	int32 startLineIndex = fTextDocumentLayout.LineIndexForOffset(start);
	int32 endLineIndex = fTextDocumentLayout.LineIndexForOffset(end);

	if (startLineIndex == endLineIndex) {
		// Selection on one line
		BPoint lt(startX1, startY1);
		BPoint rt(endX1, endY1);
		BPoint rb(endX1, endY2);
		BPoint lb(startX1, startY2);

		shape.MoveTo(lt);
		shape.LineTo(rt);
		shape.LineTo(rb);
		shape.LineTo(lb);
		shape.Close();
	} else if (startLineIndex == endLineIndex - 1 && endX1 <= startX1) {
		// Selection on two lines, with gap:
		// ---------
		// ------###
		// ##-------
		// ---------
		float width = ceilf(fTextDocumentLayout.Width());

		BPoint lt(startX1, startY1);
		BPoint rt(width, startY1);
		BPoint rb(width, startY2);
		BPoint lb(startX1, startY2);

		shape.MoveTo(lt);
		shape.LineTo(rt);
		shape.LineTo(rb);
		shape.LineTo(lb);
		shape.Close();

		lt = BPoint(0, endY1);
		rt = BPoint(endX1, endY1);
		rb = BPoint(endX1, endY2);
		lb = BPoint(0, endY2);

		shape.MoveTo(lt);
		shape.LineTo(rt);
		shape.LineTo(rb);
		shape.LineTo(lb);
		shape.Close();
	} else {
		// Selection over multiple lines
		float width = ceilf(fTextDocumentLayout.Width());

		shape.MoveTo(BPoint(startX1, startY1));
		shape.LineTo(BPoint(width, startY1));
		shape.LineTo(BPoint(width, endY1));
		shape.LineTo(BPoint(endX1, endY1));
		shape.LineTo(BPoint(endX1, endY2));
		shape.LineTo(BPoint(0, endY2));
		shape.LineTo(BPoint(0, startY2));
		shape.LineTo(BPoint(startX1, startY2));
		shape.Close();
	}
}



