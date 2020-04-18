/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/

/*******************************************************************************
/
/	File:			ColumnListView.cpp
/
/   Description:    Experimental multi-column list view.
/
/	Copyright 2000+, Be Incorporated, All Rights Reserved
/					 By Jeff Bush
/
*******************************************************************************/

#include "ColumnListView.h"

#include <typeinfo>

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>

#include <Application.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <Cursor.h>
#include <Debug.h>
#include <GraphicsDefs.h>
#include <LayoutUtils.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <ScrollBar.h>
#include <String.h>
#include <SupportDefs.h>
#include <Window.h>

#include <ObjectListPrivate.h>

#include "ObjectList.h"



BField::BField()
{
}


BField::~BField()
{
}


// #pragma mark -


void
BColumn::MouseMoved(BColumnListView* /*parent*/, BRow* /*row*/,
	BField* /*field*/, BRect /*field_rect*/, BPoint/*point*/,
	uint32 /*buttons*/, int32 /*code*/)
{
}


void
BColumn::MouseDown(BColumnListView* /*parent*/, BRow* /*row*/,
	BField* /*field*/, BRect /*field_rect*/, BPoint /*point*/,
	uint32 /*buttons*/)
{
}


void
BColumn::MouseUp(BColumnListView* /*parent*/, BRow* /*row*/, BField* /*field*/)
{
}


// #pragma mark -


BRow::BRow()
	:
//	fChildList(NULL),
//	fIsExpanded(false),
	fHeight(std::max(kMinRowHeight,
		ceilf(be_plain_font->Size() * kRowSpacing))),
	fNextSelected(NULL),
	fPrevSelected(NULL),
	//fParent(NULL),
	fList(NULL)
{
}


BRow::BRow(float height)
	:
	//fChildList(NULL),
	//fIsExpanded(false),
	fHeight(height),
	fNextSelected(NULL),
	fPrevSelected(NULL),
	//fParent(NULL),
	fList(NULL)
{
}


BRow::~BRow()
{
	while (true) {
		BField* field = (BField*) fFields.RemoveItem((int32)0);
		if (field == 0)
			break;

		delete field;
	}
}


int32
BRow::CountFields() const
{
	return fFields.CountItems();
}


BField*
BRow::GetField(int32 index)
{
	return (BField*)fFields.ItemAt(index);
}


const BField*
BRow::GetField(int32 index) const
{
	return (const BField*)fFields.ItemAt(index);
}


void
BRow::SetField(BField* field, int32 logicalFieldIndex)
{
	if (fFields.ItemAt(logicalFieldIndex) != 0)
		delete (BField*)fFields.RemoveItem(logicalFieldIndex);

	if (NULL != fList) {
		ValidateField(field, logicalFieldIndex);
		Invalidate();
	}

	fFields.AddItem(field, logicalFieldIndex);
}


float
BRow::Height() const
{
	return fHeight;
}


/*bool
BRow::IsExpanded() const
{
	return fIsExpanded;
}*/


bool
BRow::IsSelected() const
{
	return fPrevSelected != NULL;
}


void
BRow::Invalidate()
{
	if (fList != NULL)
		fList->InvalidateRow(this);
}


void
BRow::ValidateFields() const
{
	for (int32 i = 0; i < CountFields(); i++)
		ValidateField(GetField(i), i);
}


void
BRow::ValidateField(const BField* field, int32 logicalFieldIndex) const
{
	// The Fields may be moved by the user, but the logicalFieldIndexes
	// do not change, so we need to map them over when checking the
	// Field types.
	BColumn* column = NULL;
	int32 items = fList->CountColumns();
	for (int32 i = 0 ; i < items; ++i) {
		column = fList->ColumnAt(i);
		if(column->LogicalFieldNum() == logicalFieldIndex )
			break;
	}

	if (column == NULL) {
		BString dbmessage("\n\n\tThe parent BColumnListView does not have "
			"\n\ta BColumn at the logical field index ");
		dbmessage << logicalFieldIndex << ".\n";
		puts(dbmessage.String());
	} else {
		if (!column->AcceptsField(field)) {
			BString dbmessage("\n\n\tThe BColumn of type ");
			dbmessage << typeid(*column).name() << "\n\tat logical field index "
				<< logicalFieldIndex << "\n\tdoes not support the field type "
				<< typeid(*field).name() << ".\n\n";
			debugger(dbmessage.String());
		}
	}
}


// #pragma mark -


BColumn::BColumn(float width, float minWidth, float maxWidth, alignment align)
	:
	fWidth(width),
	fMinWidth(minWidth),
	fMaxWidth(maxWidth),
	fVisible(true),
	fList(0),
	fShowHeading(true),
	fAlignment(align)
{
}


BColumn::~BColumn()
{
}


float
BColumn::Width() const
{
	return fWidth;
}


void
BColumn::SetWidth(float width)
{
	fWidth = width;
}


float
BColumn::MinWidth() const
{
	return fMinWidth;
}


float
BColumn::MaxWidth() const
{
	return fMaxWidth;
}


void
BColumn::DrawTitle(BRect, BView*)
{
}


void
BColumn::DrawField(BField*, BRect, BView*)
{
}


int
BColumn::CompareFields(BField*, BField*)
{
	return 0;
}


void
BColumn::GetColumnName(BString* into) const
{
	*into = "(Unnamed)";
}


float
BColumn::GetPreferredWidth(BField* field, BView* parent) const
{
	return fWidth;
}


bool
BColumn::IsVisible() const
{
	return fVisible;
}


void
BColumn::SetVisible(bool visible)
{
	if (fList && (fVisible != visible))
		fList->SetColumnVisible(this, visible);
}


bool
BColumn::ShowHeading() const
{
	return fShowHeading;
}


void
BColumn::SetShowHeading(bool state)
{
	fShowHeading = state;
}


alignment
BColumn::Alignment() const
{
	return fAlignment;
}


void
BColumn::SetAlignment(alignment align)
{
	fAlignment = align;
}


bool
BColumn::WantsEvents() const
{
	return fWantsEvents;
}


void
BColumn::SetWantsEvents(bool state)
{
	fWantsEvents = state;
}


int32
BColumn::LogicalFieldNum() const
{
	return fFieldID;
}


bool
BColumn::AcceptsField(const BField*) const
{
	return true;
}


// #pragma mark -


BColumnListView::BColumnListView(BRect rect, const char* name,
	uint32 resizingMode, uint32 flags, border_style border,
	bool showHorizontalScrollbar)
	:
	BView(rect, name, resizingMode,
		flags | B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fStatusView(NULL),
	fSelectionMessage(NULL),
	fSortingEnabled(true),
	fBorderStyle(border),
	fShowingHorizontalScrollBar(showHorizontalScrollbar)
{
	_Init();
}


BColumnListView::BColumnListView(const char* name, uint32 flags,
	border_style border, bool showHorizontalScrollbar)
	:
	BView(name, flags | B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fStatusView(NULL),
	fSelectionMessage(NULL),
	fSortingEnabled(true),
	fBorderStyle(border),
	fShowingHorizontalScrollBar(showHorizontalScrollbar)
{
	_Init();
}


BColumnListView::~BColumnListView()
{
	while (BColumn* column = (BColumn*)fColumns.RemoveItem((int32)0))
		delete column;
}


bool
BColumnListView::InitiateDrag(BPoint, bool)
{
	return false;
}


void
BColumnListView::MessageDropped(BMessage*, BPoint)
{
}


/*void
BColumnListView::ExpandOrCollapse(BRow* row, bool Open)
{
	fOutlineView->ExpandOrCollapse(row, Open);
}*/


status_t
BColumnListView::Invoke(BMessage* message)
{
	if (message == 0)
		message = Message();

	return BInvoker::Invoke(message);
}


void
BColumnListView::ItemInvoked()
{
	Invoke();
}


void
BColumnListView::SetInvocationMessage(BMessage* message)
{
	SetMessage(message);
}


BMessage*
BColumnListView::InvocationMessage() const
{
	return Message();
}


uint32
BColumnListView::InvocationCommand() const
{
	return Command();
}


BRow*
BColumnListView::FocusRow() const
{
	return fOutlineView->FocusRow();
}


void
BColumnListView::SetFocusRow(int32 Index, bool Select)
{
	SetFocusRow(RowAt(Index), Select);
}


void
BColumnListView::SetFocusRow(BRow* row, bool Select)
{
	fOutlineView->SetFocusRow(row, Select);
}


void
BColumnListView::SetMouseTrackingEnabled(bool Enabled)
{
	fOutlineView->SetMouseTrackingEnabled(Enabled);
}


list_view_type
BColumnListView::SelectionMode() const
{
	return fOutlineView->SelectionMode();
}


void
BColumnListView::Deselect(BRow* row)
{
	fOutlineView->Deselect(row);
}


void
BColumnListView::AddToSelection(BRow* row)
{
	fOutlineView->AddToSelection(row);
}


void
BColumnListView::DeselectAll()
{
	fOutlineView->DeselectAll();
}


BRow*
BColumnListView::CurrentSelection(BRow* lastSelected) const
{
	return fOutlineView->CurrentSelection(lastSelected);
}


void
BColumnListView::SelectionChanged()
{
	if (fSelectionMessage)
		Invoke(fSelectionMessage);
}


void
BColumnListView::SetSelectionMessage(BMessage* message)
{
	if (fSelectionMessage == message)
		return;

	delete fSelectionMessage;
	fSelectionMessage = message;
}


BMessage*
BColumnListView::SelectionMessage()
{
	return fSelectionMessage;
}


uint32
BColumnListView::SelectionCommand() const
{
	if (fSelectionMessage)
		return fSelectionMessage->what;

	return 0;
}


void
BColumnListView::SetSelectionMode(list_view_type mode)
{
	fOutlineView->SetSelectionMode(mode);
}


void
BColumnListView::SetSortingEnabled(bool enabled)
{
	fSortingEnabled = enabled;
	fSortColumns.MakeEmpty();
	fTitleView->Invalidate();
		// erase sort indicators
}


bool
BColumnListView::SortingEnabled() const
{
	return fSortingEnabled;
}


void
BColumnListView::SetSortColumn(BColumn* column, bool add, bool ascending)
{
	if (!SortingEnabled())
		return;

	if (!add)
		fSortColumns.MakeEmpty();

	if (!fSortColumns.HasItem(column))
		fSortColumns.AddItem(column);

	column->fSortAscending = ascending;
	fTitleView->Invalidate();
	fOutlineView->StartSorting();
}


void
BColumnListView::ClearSortColumns()
{
	fSortColumns.MakeEmpty();
	fTitleView->Invalidate();
		// erase sort indicators
}


void
BColumnListView::AddStatusView(BView* view)
{
	BRect bounds = Bounds();
	float width = view->Bounds().Width();
	if (width > bounds.Width() / 2)
		width = bounds.Width() / 2;

	fStatusView = view;

	Window()->BeginViewTransaction();
	fHorizontalScrollBar->ResizeBy(-(width + 1), 0);
	fHorizontalScrollBar->MoveBy((width + 1), 0);
	AddChild(view);

	BRect viewRect(bounds);
	viewRect.right = width;
	viewRect.top = viewRect.bottom - B_H_SCROLL_BAR_HEIGHT;
	if (fBorderStyle == B_PLAIN_BORDER)
		viewRect.OffsetBy(1, -1);
	else if (fBorderStyle == B_FANCY_BORDER)
		viewRect.OffsetBy(2, -2);

	view->SetResizingMode(B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
	view->ResizeTo(viewRect.Width(), viewRect.Height());
	view->MoveTo(viewRect.left, viewRect.top);
	Window()->EndViewTransaction();
}


BView*
BColumnListView::RemoveStatusView()
{
	if (fStatusView) {
		float width = fStatusView->Bounds().Width();
		Window()->BeginViewTransaction();
		fStatusView->RemoveSelf();
		fHorizontalScrollBar->MoveBy(-width, 0);
		fHorizontalScrollBar->ResizeBy(width, 0);
		Window()->EndViewTransaction();
	}

	BView* view = fStatusView;
	fStatusView = 0;
	return view;
}


void
BColumnListView::AddColumn(BColumn* column, int32 logicalFieldIndex)
{
	ASSERT(column != NULL);

	column->fList = this;
	column->fFieldID = logicalFieldIndex;

	// sanity check -- if there is already a field with this ID, remove it.
	for (int32 index = 0; index < fColumns.CountItems(); index++) {
		BColumn* existingColumn = (BColumn*) fColumns.ItemAt(index);
		if (existingColumn && existingColumn->fFieldID == logicalFieldIndex) {
			RemoveColumn(existingColumn);
			break;
		}
	}

	if (column->Width() < column->MinWidth())
		column->SetWidth(column->MinWidth());
	else if (column->Width() > column->MaxWidth())
		column->SetWidth(column->MaxWidth());

	fColumns.AddItem((void*) column);
	fTitleView->ColumnAdded(column);
}


void
BColumnListView::MoveColumn(BColumn* column, int32 index)
{
	ASSERT(column != NULL);
	fTitleView->MoveColumn(column, index);
}


void
BColumnListView::RemoveColumn(BColumn* column)
{
	if (fColumns.HasItem(column)) {
		SetColumnVisible(column, false);
		if (Window() != NULL)
			Window()->UpdateIfNeeded();
		fColumns.RemoveItem(column);
	}
}


int32
BColumnListView::CountColumns() const
{
	return fColumns.CountItems();
}


BColumn*
BColumnListView::ColumnAt(int32 field) const
{
	return (BColumn*) fColumns.ItemAt(field);
}


BColumn*
BColumnListView::ColumnAt(BPoint point) const
{
	float left = kLeftMargin;

	for (int i = 0; BColumn* column = (BColumn*)fColumns.ItemAt(i); i++) {
		if (column == NULL || !column->IsVisible())
			continue;

		float right = left + column->Width();
		if (point.x >= left && point.x <= right)
			return column;

		left = right + 1;
	}

	return NULL;
}


void
BColumnListView::SetColumnVisible(BColumn* column, bool visible)
{
	fTitleView->SetColumnVisible(column, visible);
}


void
BColumnListView::SetColumnVisible(int32 index, bool isVisible)
{
	BColumn* column = ColumnAt(index);
	if (column != NULL)
		column->SetVisible(isVisible);
}


bool
BColumnListView::IsColumnVisible(int32 index) const
{
	BColumn* column = ColumnAt(index);
	if (column != NULL)
		return column->IsVisible();

	return false;
}


void
BColumnListView::SetColumnFlags(column_flags flags)
{
	fTitleView->SetColumnFlags(flags);
}


void
BColumnListView::ResizeColumnToPreferred(int32 index)
{
	BColumn* column = ColumnAt(index);
	if (column == NULL)
		return;

	// get the preferred column width
	float width = fOutlineView->GetColumnPreferredWidth(column);

	// set it
	float oldWidth = column->Width();
	column->SetWidth(width);

	fTitleView->ColumnResized(column, oldWidth);
	fOutlineView->Invalidate();
}


void
BColumnListView::ResizeAllColumnsToPreferred()
{
	int32 count = CountColumns();
	for (int32 i = 0; i < count; i++)
		ResizeColumnToPreferred(i);
}

const BRow*
BColumnListView::RowAt(int32 Index) const
{
	return fOutlineView->RowList()->ItemAt(Index);
}


BRow*
BColumnListView::RowAt(int32 Index)
{
		return fOutlineView->RowList()->ItemAt(Index);
}


const BRow*
BColumnListView::RowAt(BPoint point) const
{
	float top;
	int32 indent;
	return fOutlineView->FindRow(point.y, &indent, &top);
}


BRow*
BColumnListView::RowAt(BPoint point)
{
	float top;
	int32 indent;
	return fOutlineView->FindRow(point.y, &indent, &top);
}


bool
BColumnListView::GetRowRect(const BRow* row, BRect* outRect) const
{
	return fOutlineView->FindRect(row, outRect);
}

int32
BColumnListView::IndexOf(BRow* row)
{
	return fOutlineView->IndexOf(row);
}

int32
BColumnListView::CountRows() const
{
	
	return fOutlineView->RowList()->CountItems();
}


void
BColumnListView::AddRow(BRow* row)
{
	AddRow(row, -1);
}


void
BColumnListView::AddRow(BRow* row, int32 index)
{
	row->fList = this;
	row->ValidateFields();
	fOutlineView->AddRow(row, index);
}


void
BColumnListView::RemoveRow(BRow* row)
{
	fOutlineView->RemoveRow(row);
	row->fList = NULL;
}


void
BColumnListView::UpdateRow(BRow* row)
{
	fOutlineView->UpdateRow(row);
}

bool
BColumnListView::SwapRows(int32 index1, int32 index2)
{
	BRow* row1 = NULL;
	BRow* row2 = NULL;

	BRowContainer* container1 = NULL;
	BRowContainer* container2 = NULL;

	container1 = fOutlineView->RowList();
	
	if (container1 == NULL)
		return false;

	container2 = fOutlineView->RowList();
	
	if (container2 == NULL)
		return false;

	row1 = container1->ItemAt(index1);

	if (row1 == NULL)
		return false;

	row2 = container2->ItemAt(index2);

	if (row2 == NULL)
		return false;

	container1->ReplaceItem(index2, row1);
	container2->ReplaceItem(index1, row2);

	BRect rect1;
	BRect rect2;
	BRect rect;

	fOutlineView->FindRect(row1, &rect1);
	fOutlineView->FindRect(row2, &rect2);

	rect = rect1 | rect2;

	fOutlineView->Invalidate(rect);

	return true;
}


void
BColumnListView::ScrollTo(const BRow* row)
{
	fOutlineView->ScrollTo(row);
}


void
BColumnListView::ScrollTo(BPoint point)
{
	fOutlineView->ScrollTo(point);
}


void
BColumnListView::Clear()
{
	fOutlineView->Clear();
}


void
BColumnListView::InvalidateRow(BRow* row)
{
	BRect updateRect;
	GetRowRect(row, &updateRect);
	fOutlineView->Invalidate(updateRect);
}


// This method is deprecated.
void
BColumnListView::SetFont(const BFont* font, uint32 mask)
{
	fOutlineView->SetFont(font, mask);
	fTitleView->SetFont(font, mask);
}


void
BColumnListView::SetFont(ColumnListViewFont font_num, const BFont* font,
	uint32 mask)
{
	switch (font_num) {
		case B_FONT_ROW:
			fOutlineView->SetFont(font, mask);
			break;

		case B_FONT_HEADER:
			fTitleView->SetFont(font, mask);
			break;

		default:
			ASSERT(false);
			break;
	}
}


void
BColumnListView::GetFont(ColumnListViewFont font_num, BFont* font) const
{
	switch (font_num) {
		case B_FONT_ROW:
			fOutlineView->GetFont(font);
			break;

		case B_FONT_HEADER:
			fTitleView->GetFont(font);
			break;

		default:
			ASSERT(false);
			break;
	}
}


void
BColumnListView::SetColor(ColumnListViewColor colorIndex, const rgb_color color)
{
	if ((int)colorIndex < 0) {
		ASSERT(false);
		colorIndex = (ColumnListViewColor)0;
	}

	if ((int)colorIndex >= (int)B_COLOR_TOTAL) {
		ASSERT(false);
		colorIndex = (ColumnListViewColor)(B_COLOR_TOTAL - 1);
	}

	fColorList[colorIndex] = color;
	fCustomColors = true;
}


void
BColumnListView::ResetColors()
{
	fCustomColors = false;
	_UpdateColors();
	Invalidate();
}


rgb_color
BColumnListView::Color(ColumnListViewColor colorIndex) const
{
	if ((int)colorIndex < 0) {
		ASSERT(false);
		colorIndex = (ColumnListViewColor)0;
	}

	if ((int)colorIndex >= (int)B_COLOR_TOTAL) {
		ASSERT(false);
		colorIndex = (ColumnListViewColor)(B_COLOR_TOTAL - 1);
	}

	return fColorList[colorIndex];
}


void
BColumnListView::SetHighColor(rgb_color color)
{
	BView::SetHighColor(color);
//	fOutlineView->Invalidate();
		// Redraw with the new color.
		// Note that this will currently cause an infinite loop, refreshing
		// over and over. A better solution is needed.
}


void
BColumnListView::SetSelectionColor(rgb_color color)
{
	fColorList[B_COLOR_SELECTION] = color;
	fCustomColors = true;
}


void
BColumnListView::SetBackgroundColor(rgb_color color)
{
	fColorList[B_COLOR_BACKGROUND] = color;
	fCustomColors = true;
	fOutlineView->Invalidate();
		// repaint with new color
}


void
BColumnListView::SetEditColor(rgb_color color)
{
	fColorList[B_COLOR_EDIT_BACKGROUND] = color;
	fCustomColors = true;
}


const rgb_color
BColumnListView::SelectionColor() const
{
	return fColorList[B_COLOR_SELECTION];
}


const rgb_color
BColumnListView::BackgroundColor() const
{
	return fColorList[B_COLOR_BACKGROUND];
}


const rgb_color
BColumnListView::EditColor() const
{
	return fColorList[B_COLOR_EDIT_BACKGROUND];
}


BPoint
BColumnListView::SuggestTextPosition(const BRow* row,
	const BColumn* inColumn) const
{
	BRect rect(GetFieldRect(row, inColumn));

	font_height fh;
	fOutlineView->GetFontHeight(&fh);
	float baseline = floor(rect.top + fh.ascent
		+ (rect.Height() + 1 - (fh.ascent + fh.descent)) / 2);
	return BPoint(rect.left + 8, baseline);
}


BRect
BColumnListView::GetFieldRect(const BRow* row, const BColumn* inColumn) const
{
	BRect rect;
	GetRowRect(row, &rect);
	if (inColumn != NULL) {
		float leftEdge = kLeftMargin;
		for (int index = 0; index < fColumns.CountItems(); index++) {
			BColumn* column = (BColumn*) fColumns.ItemAt(index);
			if (column == NULL || !column->IsVisible())
				continue;

			if (column == inColumn) {
				rect.left = leftEdge;
				rect.right = rect.left + column->Width();
				break;
			}

			leftEdge += column->Width() + 1;
		}
	}

	return rect;
}



void
BColumnListView::MakeFocus(bool isFocus)
{
	if (fBorderStyle != B_NO_BORDER) {
		// Redraw focus marks around view
		Invalidate();
		fHorizontalScrollBar->SetBorderHighlighted(isFocus);
		fVerticalScrollBar->SetBorderHighlighted(isFocus);
	}

	BView::MakeFocus(isFocus);
}


void
BColumnListView::MessageReceived(BMessage* message)
{
	// Propagate mouse wheel messages down to child, so that it can
	// scroll.  Note we have done so, so we don't go into infinite
	// recursion if this comes back up here.
	if (message->what == B_MOUSE_WHEEL_CHANGED) {
		bool handled;
		if (message->FindBool("be:clvhandled", &handled) != B_OK) {
			message->AddBool("be:clvhandled", true);
			fOutlineView->MessageReceived(message);
			return;
		}
	} else if (message->what == B_COLORS_UPDATED) {
		// Todo: Is it worthwhile to optimize this?
		_UpdateColors();
	}

	BView::MessageReceived(message);
}


void
BColumnListView::KeyDown(const char* bytes, int32 numBytes)
{
	char c = bytes[0];
	switch (c) {
		case B_RIGHT_ARROW:
		case B_LEFT_ARROW:
		{
			if ((modifiers() & B_SHIFT_KEY) != 0) {
				float  minVal, maxVal;
				fHorizontalScrollBar->GetRange(&minVal, &maxVal);
				float smallStep, largeStep;
				fHorizontalScrollBar->GetSteps(&smallStep, &largeStep);
				float oldVal = fHorizontalScrollBar->Value();
				float newVal = oldVal;

				if (c == B_LEFT_ARROW)
					newVal -= smallStep;
				else if (c == B_RIGHT_ARROW)
					newVal += smallStep;

				if (newVal < minVal)
					newVal = minVal;
				else if (newVal > maxVal)
					newVal = maxVal;

				fHorizontalScrollBar->SetValue(newVal);
			} else {
				BRow* focusRow = fOutlineView->FocusRow();
				if (focusRow == NULL)
					break;

			}
			break;
		}

		case B_DOWN_ARROW:
			fOutlineView->ChangeFocusRow(false,
				(modifiers() & B_CONTROL_KEY) == 0,
				(modifiers() & B_SHIFT_KEY) != 0);
			break;

		case B_UP_ARROW:
			fOutlineView->ChangeFocusRow(true,
				(modifiers() & B_CONTROL_KEY) == 0,
				(modifiers() & B_SHIFT_KEY) != 0);
			break;

		case B_PAGE_UP:
		case B_PAGE_DOWN:
		{
			float minValue, maxValue;
			fVerticalScrollBar->GetRange(&minValue, &maxValue);
			float smallStep, largeStep;
			fVerticalScrollBar->GetSteps(&smallStep, &largeStep);
			float currentValue = fVerticalScrollBar->Value();
			float newValue = currentValue;

			if (c == B_PAGE_UP)
				newValue -= largeStep;
			else
				newValue += largeStep;

			if (newValue > maxValue)
				newValue = maxValue;
			else if (newValue < minValue)
				newValue = minValue;

			fVerticalScrollBar->SetValue(newValue);

			// Option + pgup or pgdn scrolls and changes the selection.
			if (modifiers() & B_OPTION_KEY)
				fOutlineView->MoveFocusToVisibleRect();

			break;
		}

		case B_ENTER:
			Invoke();
			break;

		case B_SPACE:
			fOutlineView->ToggleFocusRowSelection(
				(modifiers() & B_SHIFT_KEY) != 0);
			break;

		default:
			BView::KeyDown(bytes, numBytes);
	}
}


void
BColumnListView::AttachedToWindow()
{
	if (!Messenger().IsValid())
		SetTarget(Window());

	if (SortingEnabled()) fOutlineView->StartSorting();
}


void
BColumnListView::WindowActivated(bool active)
{
	fOutlineView->Invalidate();
		// focus and selection appearance changes with focus

	Invalidate();
		// redraw focus marks around view
	BView::WindowActivated(active);
}


void
BColumnListView::Draw(BRect updateRect)
{
	BRect rect = Bounds();

	uint32 flags = 0;
	if (IsFocus() && Window()->IsActive())
		flags |= BControlLook::B_FOCUSED;

	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

	BRect verticalScrollBarFrame;
	if (!fVerticalScrollBar->IsHidden())
		verticalScrollBarFrame = fVerticalScrollBar->Frame();

	BRect horizontalScrollBarFrame;
	if (!fHorizontalScrollBar->IsHidden())
		horizontalScrollBarFrame = fHorizontalScrollBar->Frame();

	if (fBorderStyle == B_NO_BORDER) {
		// We still draw the left/top border, but not focused.
		// The scrollbars cannot be displayed without frame and
		// it looks bad to have no frame only along the left/top
		// side.
		rgb_color borderColor = tint_color(base, B_DARKEN_2_TINT);
		SetHighColor(borderColor);
		StrokeLine(BPoint(rect.left, rect.bottom),
			BPoint(rect.left, rect.top));
		StrokeLine(BPoint(rect.left + 1, rect.top),
			BPoint(rect.right, rect.top));
	}

	be_control_look->DrawScrollViewFrame(this, rect, updateRect,
		verticalScrollBarFrame, horizontalScrollBarFrame,
		base, fBorderStyle, flags);

	if (fStatusView != NULL) {
		rect = Bounds();
		BRegion region(rect & fStatusView->Frame().InsetByCopy(-2, -2));
		ConstrainClippingRegion(&region);
		rect.bottom = fStatusView->Frame().top - 1;
		be_control_look->DrawScrollViewFrame(this, rect, updateRect,
			BRect(), BRect(), base, fBorderStyle, flags);
	}
}


void
BColumnListView::SaveState(BMessage* message)
{
	message->MakeEmpty();

	for (int32 i = 0; BColumn* column = (BColumn*)fColumns.ItemAt(i); i++) {
		message->AddInt32("ID", column->fFieldID);
		message->AddFloat("width", column->fWidth);
		message->AddBool("visible", column->fVisible);
	}

	message->AddBool("sortingenabled", fSortingEnabled);

	if (fSortingEnabled) {
		for (int32 i = 0; BColumn* column = (BColumn*)fSortColumns.ItemAt(i);
				i++) {
			message->AddInt32("sortID", column->fFieldID);
			message->AddBool("sortascending", column->fSortAscending);
		}
	}
}


void
BColumnListView::LoadState(BMessage* message)
{
	int32 id;
	for (int i = 0; message->FindInt32("ID", i, &id) == B_OK; i++) {
		for (int j = 0; BColumn* column = (BColumn*)fColumns.ItemAt(j); j++) {
			if (column->fFieldID == id) {
				// move this column to position 'i' and set its attributes
				MoveColumn(column, i);
				float width;
				if (message->FindFloat("width", i, &width) == B_OK)
					column->SetWidth(width);
				bool visible;
				if (message->FindBool("visible", i, &visible) == B_OK)
					column->SetVisible(visible);
			}
		}
	}
	bool b;
	if (message->FindBool("sortingenabled", &b) == B_OK) {
		SetSortingEnabled(b);
		for (int k = 0; message->FindInt32("sortID", k, &id) == B_OK; k++) {
			for (int j = 0; BColumn* column = (BColumn*)fColumns.ItemAt(j);
					j++) {
				if (column->fFieldID == id) {
					// add this column to the sort list
					bool value;
					if (message->FindBool("sortascending", k, &value) == B_OK)
						SetSortColumn(column, true, value);
				}
			}
		}
	}
}


void
BColumnListView::SetEditMode(bool state)
{
	fOutlineView->SetEditMode(state);
	fTitleView->SetEditMode(state);
}


void
BColumnListView::Refresh()
{
	if (LockLooper()) {
		Invalidate();
		fOutlineView->FixScrollBar (true);
		fOutlineView->Invalidate();
		Window()->UpdateIfNeeded();
		UnlockLooper();
	}
}


BSize
BColumnListView::MinSize()
{
	BSize size;
	size.width = 100;
	size.height = std::max(kMinTitleHeight,
		ceilf(be_plain_font->Size() * kTitleSpacing))
		+ 4 * B_H_SCROLL_BAR_HEIGHT;
	if (!fHorizontalScrollBar->IsHidden())
		size.height += fHorizontalScrollBar->Frame().Height() + 1;
	// TODO: Take border size into account

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


BSize
BColumnListView::PreferredSize()
{
	BSize size = MinSize();
	size.height += ceilf(be_plain_font->Size()) * 20;

	// return MinSize().width if there are no columns.
	int32 count = CountColumns();
	if (count > 0) {
		BRect titleRect;
		BRect outlineRect;
		BRect vScrollBarRect;
		BRect hScrollBarRect;
		_GetChildViewRects(Bounds(), titleRect, outlineRect, vScrollBarRect,
			hScrollBarRect);
		// Start with the extra width for border and scrollbars etc.
		size.width = titleRect.left - Bounds().left;
		size.width += Bounds().right - titleRect.right;
		// If we want all columns to be visible at their preferred width,
		// we also need to add the extra margin width that the TitleView
		// uses to compute its _VirtualWidth() for the horizontal scroll bar.
		size.width += fTitleView->MarginWidth();
		for (int32 i = 0; i < count; i++) {
			BColumn* column = ColumnAt(i);
			if (column != NULL)
				size.width += fOutlineView->GetColumnPreferredWidth(column);
		}
	}

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


BSize
BColumnListView::MaxSize()
{
	BSize size(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


void
BColumnListView::LayoutInvalidated(bool descendants)
{
}


void
BColumnListView::DoLayout()
{
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		return;

	BRect titleRect;
	BRect outlineRect;
	BRect vScrollBarRect;
	BRect hScrollBarRect;
	_GetChildViewRects(Bounds(), titleRect, outlineRect, vScrollBarRect,
		hScrollBarRect);

	fTitleView->MoveTo(titleRect.LeftTop());
	fTitleView->ResizeTo(titleRect.Width(), titleRect.Height());

	fOutlineView->MoveTo(outlineRect.LeftTop());
	fOutlineView->ResizeTo(outlineRect.Width(), outlineRect.Height());

	fVerticalScrollBar->MoveTo(vScrollBarRect.LeftTop());
	fVerticalScrollBar->ResizeTo(vScrollBarRect.Width(),
		vScrollBarRect.Height());

	if (fStatusView != NULL) {
		BSize size = fStatusView->MinSize();
		if (size.height > B_H_SCROLL_BAR_HEIGHT)
			size.height = B_H_SCROLL_BAR_HEIGHT;
		if (size.width > Bounds().Width() / 2)
			size.width = floorf(Bounds().Width() / 2);

		BPoint offset(hScrollBarRect.LeftTop());

		if (fBorderStyle == B_PLAIN_BORDER) {
			offset += BPoint(0, 1);
		} else if (fBorderStyle == B_FANCY_BORDER) {
			offset += BPoint(-1, 2);
			size.height -= 1;
		}

		fStatusView->MoveTo(offset);
		fStatusView->ResizeTo(size.width, size.height);
		hScrollBarRect.left = offset.x + size.width + 1;
	}

	fHorizontalScrollBar->MoveTo(hScrollBarRect.LeftTop());
	fHorizontalScrollBar->ResizeTo(hScrollBarRect.Width(),
		hScrollBarRect.Height());

	fOutlineView->FixScrollBar(true);
}


void
BColumnListView::_Init()
{
	SetViewColor(B_TRANSPARENT_32_BIT);

	BRect bounds(Bounds());
	if (bounds.Width() <= 0)
		bounds.right = 100;

	if (bounds.Height() <= 0)
		bounds.bottom = 100;

	fCustomColors = false;
	_UpdateColors();

	BRect titleRect;
	BRect outlineRect;
	BRect vScrollBarRect;
	BRect hScrollBarRect;
	_GetChildViewRects(bounds, titleRect, outlineRect, vScrollBarRect,
		hScrollBarRect);

	fOutlineView = new OutlineView(outlineRect, &fColumns, &fSortColumns, this);
	AddChild(fOutlineView);


	fTitleView = new TitleView(titleRect, fOutlineView, &fColumns,
		&fSortColumns, this, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
	AddChild(fTitleView);

	fVerticalScrollBar = new BScrollBar(vScrollBarRect, "vertical_scroll_bar",
		fOutlineView, 0.0, bounds.Height(), B_VERTICAL);
	AddChild(fVerticalScrollBar);

	fHorizontalScrollBar = new BScrollBar(hScrollBarRect,
		"horizontal_scroll_bar", fTitleView, 0.0, bounds.Width(), B_HORIZONTAL);
	AddChild(fHorizontalScrollBar);

	if (!fShowingHorizontalScrollBar)
		fHorizontalScrollBar->Hide();

	fOutlineView->FixScrollBar(true);
}


void
BColumnListView::_UpdateColors()
{
	if (fCustomColors)
		return;

	fColorList[B_COLOR_BACKGROUND] = ui_color(B_LIST_BACKGROUND_COLOR);
	fColorList[B_COLOR_TEXT] = ui_color(B_LIST_ITEM_TEXT_COLOR);
	fColorList[B_COLOR_ROW_DIVIDER] = tint_color(
		ui_color(B_LIST_SELECTED_BACKGROUND_COLOR), B_DARKEN_2_TINT);
	fColorList[B_COLOR_SELECTION] = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
	fColorList[B_COLOR_SELECTION_TEXT] =
		ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);

	// For non focus selection uses the selection color as BListView
	fColorList[B_COLOR_NON_FOCUS_SELECTION] =
		ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);

	// edit mode doesn't work very well
	fColorList[B_COLOR_EDIT_BACKGROUND] = tint_color(
		ui_color(B_LIST_SELECTED_BACKGROUND_COLOR), B_DARKEN_1_TINT);
	fColorList[B_COLOR_EDIT_BACKGROUND].alpha = 180;

	// Unused color
	fColorList[B_COLOR_EDIT_TEXT] = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);

	fColorList[B_COLOR_HEADER_BACKGROUND] = ui_color(B_PANEL_BACKGROUND_COLOR);
	fColorList[B_COLOR_HEADER_TEXT] = ui_color(B_PANEL_TEXT_COLOR);

	// Unused colors
	fColorList[B_COLOR_SEPARATOR_LINE] = ui_color(B_LIST_ITEM_TEXT_COLOR);
	fColorList[B_COLOR_SEPARATOR_BORDER] = ui_color(B_LIST_ITEM_TEXT_COLOR);
}


void
BColumnListView::_GetChildViewRects(const BRect& bounds, BRect& titleRect,
	BRect& outlineRect, BRect& vScrollBarRect, BRect& hScrollBarRect)
{
	titleRect = bounds;
	titleRect.bottom = titleRect.top + std::max(kMinTitleHeight,
		ceilf(be_plain_font->Size() * kTitleSpacing));
#if !LOWER_SCROLLBAR
	titleRect.right -= B_V_SCROLL_BAR_WIDTH;
#endif

	outlineRect = bounds;
	outlineRect.top = titleRect.bottom + 1.0;
	outlineRect.right -= B_V_SCROLL_BAR_WIDTH;
	if (fShowingHorizontalScrollBar)
		outlineRect.bottom -= B_H_SCROLL_BAR_HEIGHT;

	vScrollBarRect = bounds;
#if LOWER_SCROLLBAR
	vScrollBarRect.top += std::max(kMinTitleHeight,
		ceilf(be_plain_font->Size() * kTitleSpacing));
#endif

	vScrollBarRect.left = vScrollBarRect.right - B_V_SCROLL_BAR_WIDTH;
	if (fShowingHorizontalScrollBar)
		vScrollBarRect.bottom -= B_H_SCROLL_BAR_HEIGHT;

	hScrollBarRect = bounds;
	hScrollBarRect.top = hScrollBarRect.bottom - B_H_SCROLL_BAR_HEIGHT;
	hScrollBarRect.right -= B_V_SCROLL_BAR_WIDTH;

	// Adjust stuff so the border will fit.
	if (fBorderStyle == B_PLAIN_BORDER || fBorderStyle == B_NO_BORDER) {
		titleRect.InsetBy(1, 0);
		titleRect.OffsetBy(0, 1);
		outlineRect.InsetBy(1, 1);
	} else if (fBorderStyle == B_FANCY_BORDER) {
		titleRect.InsetBy(2, 0);
		titleRect.OffsetBy(0, 2);
		outlineRect.InsetBy(2, 2);

		vScrollBarRect.OffsetBy(-1, 0);
#if LOWER_SCROLLBAR
		vScrollBarRect.top += 2;
		vScrollBarRect.bottom -= 1;
#else
		vScrollBarRect.InsetBy(0, 1);
#endif
		hScrollBarRect.OffsetBy(0, -1);
		hScrollBarRect.InsetBy(1, 0);
	}
}


// #pragma mark -





// #pragma mark - OutlineView


OutlineView::OutlineView(BRect rect, BList* visibleColumns, BList* sortColumns,
	BColumnListView* listView)
	:
	BView(rect, "outline_view", B_FOLLOW_ALL_SIDES,
		B_WILL_DRAW | B_FRAME_EVENTS),
	fColumns(visibleColumns),
	fSortColumns(sortColumns),
	fItemsHeight(0.0),
	fVisibleRect(rect.OffsetToCopy(0, 0)),
	fFocusRow(0),
	fRollOverRow(0),
	fLastSelectedItem(0),
	fFirstSelectedItem(0),
	fSortThread(B_BAD_THREAD_ID),
	fCurrentState(INACTIVE),
	fMasterView(listView),
	fSelectionMode(B_MULTIPLE_SELECTION_LIST),
	fTrackMouse(false),
	fCurrentField(0),
	fCurrentRow(0),
	fCurrentColumn(0),
	fMouseDown(false),
	fCurrentCode(B_OUTSIDE_VIEW),
	fEditMode(false),
	fDragging(false),
	fClickCount(0),
	fDropHighlightY(-1)
{
	SetViewColor(B_TRANSPARENT_COLOR);

#if DOUBLE_BUFFERED_COLUMN_RESIZE
	// TODO: This needs to be smart about the size of the buffer.
	// Also, the buffer can be shared with the title's buffer.
	BRect doubleBufferRect(0, 0, 600, 35);
	fDrawBuffer = new BBitmap(doubleBufferRect, B_RGB32, true);
	fDrawBufferView = new BView(doubleBufferRect, "double_buffer_view",
		B_FOLLOW_ALL_SIDES, 0);
	fDrawBuffer->Lock();
	fDrawBuffer->AddChild(fDrawBufferView);
	fDrawBuffer->Unlock();
#endif

	FixScrollBar(true);
	fSelectionListDummyHead.fNextSelected = &fSelectionListDummyHead;
	fSelectionListDummyHead.fPrevSelected = &fSelectionListDummyHead;
}


OutlineView::~OutlineView()
{
#if DOUBLE_BUFFERED_COLUMN_RESIZE
	delete fDrawBuffer;
#endif

	Clear();
}


void
OutlineView::Clear()
{
	// Make sure selection list doesn't point to deleted rows!
	DeselectAll();
	DeleteRows(&fRows, false);
	fItemsHeight = 0.0;
	FixScrollBar(true);
	Invalidate();
}


void
OutlineView::SetSelectionMode(list_view_type mode)
{
	DeselectAll();
	fSelectionMode = mode;
}


list_view_type
OutlineView::SelectionMode() const
{
	return fSelectionMode;
}


void
OutlineView::Deselect(BRow* row)
{
	if (row == NULL)
		return;

	if (row->fNextSelected != 0) {
		row->fNextSelected->fPrevSelected = row->fPrevSelected;
		row->fPrevSelected->fNextSelected = row->fNextSelected;
		row->fNextSelected = 0;
		row->fPrevSelected = 0;
		Invalidate();
	}
}


void
OutlineView::AddToSelection(BRow* row)
{
	if (row == NULL)
		return;

	if (row->fNextSelected == 0) {
		if (fSelectionMode == B_SINGLE_SELECTION_LIST)
			DeselectAll();

		row->fNextSelected = fSelectionListDummyHead.fNextSelected;
		row->fPrevSelected = &fSelectionListDummyHead;
		row->fNextSelected->fPrevSelected = row;
		row->fPrevSelected->fNextSelected = row;

		BRect invalidRect;
		if (FindVisibleRect(row, &invalidRect))
			Invalidate(invalidRect);
	}
}


void
OutlineView::DeleteRows(BRowContainer* list, bool isOwner)
{
	if (list == NULL)
		return;

	while (true) {
		BRow* row = list->RemoveItemAt(0L);
		if (row == 0)
			break;
		delete row;
	}

	if (isOwner)
		delete list;
}


void
OutlineView::RedrawColumn(BColumn* column, float leftEdge, bool isFirstColumn)
{
	// TODO: Remove code duplication (private function which takes a view
	// pointer, pass "this" in non-double buffered mode)!
	// Watch out for sourceRect versus destRect though!
	if (!column)
		return;

	font_height fh;
	GetFontHeight(&fh);
	float line = 0.0;
	bool tintedLine = true;
	BRow	*row	= NULL;
	for (int32 i =0; i< fRows.CountItems();i++) {
		row = fRows.ItemAt(i);
		if (row != NULL) {
			line += row->Height() + 1;
			float rowHeight = row->Height();
			if (line > fVisibleRect.bottom)
				break;
			tintedLine = !tintedLine;
			if (line + rowHeight >= fVisibleRect.top) {
#if DOUBLE_BUFFERED_COLUMN_RESIZE
				BRect sourceRect(0, 0, column->Width(), rowHeight);
#endif
				BRect destRect(leftEdge, line, leftEdge + column->Width(),
					line + rowHeight);

				rgb_color highColor;
				rgb_color lowColor;
				if (row->fNextSelected != 0) {
					if (fEditMode) {
						highColor = fMasterView->Color(B_COLOR_EDIT_BACKGROUND);
						lowColor = fMasterView->Color(B_COLOR_EDIT_BACKGROUND);
					} else {
						highColor = fMasterView->Color(B_COLOR_SELECTION);
						lowColor = fMasterView->Color(B_COLOR_SELECTION);
					}
				} else {
					highColor = fMasterView->Color(B_COLOR_BACKGROUND);
					lowColor = fMasterView->Color(B_COLOR_BACKGROUND);
				}
				if (tintedLine)
					lowColor = tint_color(lowColor, kTintedLineTint);

#if DOUBLE_BUFFERED_COLUMN_RESIZE
				fDrawBuffer->Lock();

				fDrawBufferView->SetHighColor(highColor);
				fDrawBufferView->SetLowColor(lowColor);

				BFont font;
				GetFont(&font);
				fDrawBufferView->SetFont(&font);
				fDrawBufferView->FillRect(sourceRect, B_SOLID_LOW);

				BField* field = row->GetField(column->fFieldID);
				if (field) {
					BRect fieldRect(sourceRect);

	#if CONSTRAIN_CLIPPING_REGION
					BRegion clipRegion(fieldRect);
					fDrawBufferView->PushState();
					fDrawBufferView->ConstrainClippingRegion(&clipRegion);
	#endif
					fDrawBufferView->SetHighColor(fMasterView->Color(
						row->fNextSelected ? B_COLOR_SELECTION_TEXT
						: B_COLOR_TEXT));
					float baseline = floor(fieldRect.top + fh.ascent
						+ (fieldRect.Height() + 1 - (fh.ascent+fh.descent)) / 2);
					fDrawBufferView->MovePenTo(fieldRect.left + 8, baseline);
					column->DrawField(field, fieldRect, fDrawBufferView);
	#if CONSTRAIN_CLIPPING_REGION
					fDrawBufferView->PopState();
	#endif
				}

				if (fFocusRow == row && !fEditMode && fMasterView->IsFocus()
					&& Window()->IsActive()) {
					fDrawBufferView->SetHighColor(fMasterView->Color(
						B_COLOR_ROW_DIVIDER));
					fDrawBufferView->StrokeRect(BRect(-1, sourceRect.top,
						10000.0, sourceRect.bottom));
				}

				fDrawBufferView->Sync();
				fDrawBuffer->Unlock();
				SetDrawingMode(B_OP_COPY);
				DrawBitmap(fDrawBuffer, sourceRect, destRect);

#else

				SetHighColor(highColor);
				SetLowColor(lowColor);
				FillRect(destRect, B_SOLID_LOW);

				BField* field = row->GetField(column->fFieldID);
				if (field) {
	#if CONSTRAIN_CLIPPING_REGION
					BRegion clipRegion(destRect);
					PushState();
					ConstrainClippingRegion(&clipRegion);
	#endif
					SetHighColor(fMasterView->Color(row->fNextSelected
						? B_COLOR_SELECTION_TEXT : B_COLOR_TEXT));
					float baseline = floor(destRect.top + fh.ascent
						+ (destRect.Height() + 1 - (fh.ascent + fh.descent)) / 2);
					MovePenTo(destRect.left + 8, baseline);
					column->DrawField(field, destRect, this);
	#if CONSTRAIN_CLIPPING_REGION
					PopState();
	#endif
				}

				if (fFocusRow == row && !fEditMode && fMasterView->IsFocus()
					&& Window()->IsActive()) {
					SetHighColor(fMasterView->Color(B_COLOR_ROW_DIVIDER));
					StrokeRect(BRect(0, destRect.top, 10000.0, destRect.bottom));
				}
#endif
			}
		}
	}
}


void
OutlineView::Draw(BRect invalidBounds)
{
#if SMART_REDRAW
	BRegion invalidRegion;
	GetClippingRegion(&invalidRegion);
#endif

	font_height fh;
	GetFontHeight(&fh);

	float line = 0.0;
	bool tintedLine = true;
	int32 numColumns = fColumns->CountItems();
	BRow	*row	= NULL;
	for (int32 i =0; i< fRows.CountItems();i++) {
		row = fRows.ItemAt(i);
		if (line > invalidBounds.bottom)
			break;

		tintedLine = !tintedLine;
		float rowHeight = row->Height();

		if (line >= invalidBounds.top - rowHeight) {
			bool isFirstColumn = true;
			float fieldLeftEdge = kLeftMargin;

			// setup background color
			rgb_color lowColor;
			if (row->fNextSelected != 0) {
				if (Window()->IsActive()) {
					if (fEditMode)
						lowColor = fMasterView->Color(B_COLOR_EDIT_BACKGROUND);
					else
						lowColor = fMasterView->Color(B_COLOR_SELECTION);
				}
				else
					lowColor = fMasterView->Color(B_COLOR_NON_FOCUS_SELECTION);
			} else
				lowColor = fMasterView->Color(B_COLOR_BACKGROUND);
			if (tintedLine)
				lowColor = tint_color(lowColor, kTintedLineTint);

			for (int columnIndex = 0; columnIndex < numColumns; columnIndex++) {
				BColumn* column = (BColumn*) fColumns->ItemAt(columnIndex);
				if (!column->IsVisible())
					continue;

				if (!isFirstColumn && fieldLeftEdge > invalidBounds.right)
					break;

				if (fieldLeftEdge + column->Width() >= invalidBounds.left) {
					BRect fullRect(fieldLeftEdge, line,
						fieldLeftEdge + column->Width(), line + rowHeight);

					bool clippedFirstColumn = false;
						// This happens when a column is indented past the
						// beginning of the next column.

					SetHighColor(lowColor);

					BRect destRect(fullRect);


#if SMART_REDRAW
					if (!clippedFirstColumn
						&& invalidRegion.Intersects(fullRect)) {
#else
					if (!clippedFirstColumn) {
#endif
						FillRect(fullRect);	// Using color set above
					
						SetHighColor(fMasterView->HighColor());
							// The master view just holds the high color for us.
						SetLowColor(lowColor);

						BField* field = row->GetField(column->fFieldID);
						if (field) {
#if CONSTRAIN_CLIPPING_REGION
							BRegion clipRegion(destRect);
							PushState();
							ConstrainClippingRegion(&clipRegion);
#endif
							SetHighColor(fMasterView->Color(
								row->fNextSelected ? B_COLOR_SELECTION_TEXT
								: B_COLOR_TEXT));
							float baseline = floor(destRect.top + fh.ascent
								+ (destRect.Height() + 1
								- (fh.ascent+fh.descent)) / 2);
							MovePenTo(destRect.left + 8, baseline);
							column->DrawField(field, destRect, this);
#if CONSTRAIN_CLIPPING_REGION
							PopState();
#endif
						}
					}
				}

				isFirstColumn = false;
				fieldLeftEdge += column->Width() + 1;
			}

			if (fieldLeftEdge <= invalidBounds.right) {
				SetHighColor(lowColor);
				FillRect(BRect(fieldLeftEdge, line, invalidBounds.right,
					line + rowHeight));
			}
		}

		// indicate the keyboard focus row
		if (fFocusRow == row && !fEditMode && fMasterView->IsFocus()
			&& Window()->IsActive()) {
			SetHighColor(fMasterView->Color(B_COLOR_ROW_DIVIDER));
			StrokeRect(BRect(0, line, 10000.0, line + rowHeight));
		}

		line += rowHeight + 1;
	}

	if (line <= invalidBounds.bottom) {
		// fill background below last item
		SetHighColor(fMasterView->Color(B_COLOR_BACKGROUND));
		FillRect(BRect(invalidBounds.left, line, invalidBounds.right,
			invalidBounds.bottom));
	}

	// Draw the drop target line
	if (fDropHighlightY != -1) {
		InvertRect(BRect(0, fDropHighlightY - kDropHighlightLineHeight / 2,
			1000000, fDropHighlightY + kDropHighlightLineHeight / 2));
	}
}

//@ToDo remove _rowIndent and??? _top?
BRow*
OutlineView::FindRow(float ypos, int32* _rowIndent, float* _top)
{
	if (_rowIndent && _top) {
		float line = 0.0;
		BRow	*row	= NULL;
		for (int32 i =0; i< fRows.CountItems();i++) {
			row = fRows.ItemAt(i);
			if (line > ypos)
				break;

			float rowHeight = row->Height();
			if (ypos <= line + rowHeight) {
				*_top = line;
				*_rowIndent = 0;
				return row;
			}

			line += rowHeight + 1;
		}
	}

	return NULL;
}

void OutlineView::SetMouseTrackingEnabled(bool enabled)
{
	fTrackMouse = enabled;
	if (!enabled && fDropHighlightY != -1) {
		// Erase the old target line
		InvertRect(BRect(0, fDropHighlightY - kDropHighlightLineHeight / 2,
			1000000, fDropHighlightY + kDropHighlightLineHeight / 2));
		fDropHighlightY = -1;
	}
}


//
// Note that this interaction is not totally safe.  If items are added to
// the list in the background, the widget rect will be incorrect, possibly
// resulting in drawing glitches.  The code that adds items needs to be a little smarter
// about invalidating state.
//
void
OutlineView::MouseDown(BPoint position)
{
	if (!fEditMode)
		fMasterView->MakeFocus(true);

	// Check to see if the user is clicking on a widget to open a section
	// of the list.
	bool reset_click_count = false;
	int32 indent;
	float rowTop;
	BRow* row = FindRow(position.y, &indent, &rowTop);
	if (row != NULL) {

		// Update fCurrentField
		bool handle_field = false;
		BField* new_field = 0;
		BRow* new_row = 0;
		BColumn* new_column = 0;
		BRect new_rect;

		if (position.y >= 0) {
			if (position.x >= 0) {
				float x = 0;
				for (int32 c = 0; c < fMasterView->CountColumns(); c++) {
					new_column = fMasterView->ColumnAt(c);
					if (!new_column->IsVisible())
						continue;
					x += new_column->Width();
				}
			}
		}

		// Handle mouse down
		if (handle_field) {
			fMouseDown = true;
			fFieldRect = new_rect;
			fCurrentColumn = new_column;
			fCurrentRow = new_row;
			fCurrentField = new_field;
			fCurrentCode = B_INSIDE_VIEW;
			BMessage* message = Window()->CurrentMessage();
			int32 buttons = 1;
			message->FindInt32("buttons", &buttons);
			fCurrentColumn->MouseDown(fMasterView, fCurrentRow,
				fCurrentField, fFieldRect, position, buttons);
		}

		if (!fEditMode) {

			fTargetRow = row;
			fTargetRowTop = rowTop;
			FindVisibleRect(fFocusRow, &fFocusRowRect);

		}

		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS |
			B_NO_POINTER_HISTORY);

	} else if (fFocusRow != 0) {
		// User clicked in open space, unhighlight focus row.
		FindVisibleRect(fFocusRow, &fFocusRowRect);
		fFocusRow = 0;
		Invalidate(fFocusRowRect);
	}

	// We stash the click counts here because the 'clicks' field
	// is not in the CurrentMessage() when MouseUp is called... ;(
	if (reset_click_count)
		fClickCount = 1;
	else
		Window()->CurrentMessage()->FindInt32("clicks", &fClickCount);
	fClickPoint = position;

}


void
OutlineView::MouseMoved(BPoint position, uint32 /*transit*/,
	const BMessage* /*dragMessage*/)
{
	if (!fMouseDown) {
		// Update fCurrentField
		bool handle_field = false;
		BField* new_field = 0;
		BRow* new_row = 0;
		BColumn* new_column = 0;
		BRect new_rect(0,0,0,0);
		if (position.y >=0 ) {
			float top;
			int32 indent;
			BRow* row = FindRow(position.y, &indent, &top);
			if (row && position.x >=0 ) {
				float x=0;
				for (int32 c=0;c<fMasterView->CountColumns();c++) {
					new_column = fMasterView->ColumnAt(c);
					if (!new_column->IsVisible())
						continue;
					x += new_column->Width();
				}
			}
		}

		// Handle mouse moved
		if (handle_field) {
			if (new_field != fCurrentField) {
				if (fCurrentField) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 0,
						fCurrentCode = B_EXITED_VIEW);
				}
				fCurrentColumn = new_column;
				fCurrentRow = new_row;
				fCurrentField = new_field;
				fFieldRect = new_rect;
				if (fCurrentField) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 0,
						fCurrentCode = B_ENTERED_VIEW);
				}
			} else {
				if (fCurrentField) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 0,
						fCurrentCode = B_INSIDE_VIEW);
				}
			}
		} else {
			if (fCurrentField) {
				fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
					fCurrentField, fFieldRect, position, 0,
					fCurrentCode = B_EXITED_VIEW);
				fCurrentField = 0;
				fCurrentColumn = 0;
				fCurrentRow = 0;
			}
		}
	} else {
		if (fCurrentField) {
			if (fFieldRect.Contains(position)) {
				if (fCurrentCode == B_OUTSIDE_VIEW
					|| fCurrentCode == B_EXITED_VIEW) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 1,
						fCurrentCode = B_ENTERED_VIEW);
				} else {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 1,
						fCurrentCode = B_INSIDE_VIEW);
				}
			} else {
				if (fCurrentCode == B_INSIDE_VIEW
					|| fCurrentCode == B_ENTERED_VIEW) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 1,
						fCurrentCode = B_EXITED_VIEW);
				} else {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 1,
						fCurrentCode = B_OUTSIDE_VIEW);
				}
			}
		}
	}

	if (!fEditMode) {

		switch (fCurrentState) {
			
			case ROW_CLICKED:
				if (abs((int)(position.x - fClickPoint.x)) > kRowDragSensitivity
					|| abs((int)(position.y - fClickPoint.y))
						> kRowDragSensitivity) {
					fCurrentState = DRAGGING_ROWS;
					fMasterView->InitiateDrag(fClickPoint,
						fTargetRow->fNextSelected != 0);
				}
				break;

			case DRAGGING_ROWS:
#if 0
				// falls through...
#else
				if (fTrackMouse /*&& message*/) {
					if (fVisibleRect.Contains(position)) {
						float top;
						int32 indent;
						BRow* target = FindRow(position.y, &indent, &top);
						if (target)
							SetFocusRow(target, true);
					}
				}
				break;
#endif

			default: {

				if (fTrackMouse /*&& message*/) {
					// Draw a highlight line...
					if (fVisibleRect.Contains(position)) {
						float top;
						int32 indent;
						BRow* target = FindRow(position.y, &indent, &top);
						if (target == fRollOverRow)
							break;
						if (fRollOverRow) {
							BRect rect;
							FindRect(fRollOverRow, &rect);
							Invalidate(rect);
						}
						fRollOverRow = target;
#if 0
						SetFocusRow(fRollOverRow,false);
#else
						PushState();
						SetDrawingMode(B_OP_BLEND);
						SetHighColor(255, 255, 255, 255);
						BRect rect;
						FindRect(fRollOverRow, &rect);
						rect.bottom -= 1.0;
						FillRect(rect);
						PopState();
#endif
					} else {
						if (fRollOverRow) {
							BRect rect;
							FindRect(fRollOverRow, &rect);
							Invalidate(rect);
							fRollOverRow = NULL;
						}
					}
				}
			}
		}
	}
}


void
OutlineView::MouseUp(BPoint position)
{
	if (fCurrentField) {
		fCurrentColumn->MouseUp(fMasterView, fCurrentRow, fCurrentField);
		fMouseDown = false;
	}

	if (fEditMode)
		return;

	switch (fCurrentState) {
		case ROW_CLICKED:
			if (fClickCount > 1
				&& abs((int)fClickPoint.x - (int)position.x)
					< kDoubleClickMoveSensitivity
				&& abs((int)fClickPoint.y - (int)position.y)
					< kDoubleClickMoveSensitivity) {
				fMasterView->ItemInvoked();
			}
			fCurrentState = INACTIVE;
			break;

		case DRAGGING_ROWS:
			fCurrentState = INACTIVE;
			// Falls through

		default:
			if (fDropHighlightY != -1) {
				InvertRect(BRect(0,
					fDropHighlightY - kDropHighlightLineHeight / 2,
					1000000, fDropHighlightY + kDropHighlightLineHeight / 2));
					// Erase the old target line
				fDropHighlightY = -1;
			}
	}
}


void
OutlineView::MessageReceived(BMessage* message)
{
	if (message->WasDropped()) {
		fMasterView->MessageDropped(message,
			ConvertFromScreen(message->DropPoint()));
	} else {
		BView::MessageReceived(message);
	}
}


void
OutlineView::ChangeFocusRow(bool up, bool updateSelection,
	bool addToCurrentSelection)
{
	int32 indent;
	float top;
	float newRowPos = 0;
	float verticalScroll = 0;

	if (fFocusRow) {
		// A row currently has the focus, get information about it
		newRowPos = fFocusRowRect.top + (up ? -4 : fFocusRow->Height() + 4);
		if (newRowPos < fVisibleRect.top + 20)
			verticalScroll = newRowPos - 20;
		else if (newRowPos > fVisibleRect.bottom - 20)
			verticalScroll = newRowPos - fVisibleRect.Height() + 20;
	} else
		newRowPos = fVisibleRect.top + 2;
			// no row is currently focused, set this to the top of the window
			// so we will select the first visible item in the list.

	BRow* newRow = FindRow(newRowPos, &indent, &top);
	if (newRow) {
		if (fFocusRow) {
			fFocusRowRect.right = 10000;
			Invalidate(fFocusRowRect);
		}
		BRow* oldFocusRow = fFocusRow;
		fFocusRow = newRow;
		fFocusRowRect.top = top;
		fFocusRowRect.left = 0;
		fFocusRowRect.right = 10000;
		fFocusRowRect.bottom = fFocusRowRect.top + fFocusRow->Height();
		Invalidate(fFocusRowRect);

		if (updateSelection) {
			if (!addToCurrentSelection
				|| fSelectionMode == B_SINGLE_SELECTION_LIST) {
				DeselectAll();
			}

			// if the focus row isn't selected, add it to the selection
			if (fFocusRow->fNextSelected == 0) {
				fFocusRow->fNextSelected
					= fSelectionListDummyHead.fNextSelected;
				fFocusRow->fPrevSelected = &fSelectionListDummyHead;
				fFocusRow->fNextSelected->fPrevSelected = fFocusRow;
				fFocusRow->fPrevSelected->fNextSelected = fFocusRow;
			} else if (oldFocusRow != NULL
				&& fSelectionListDummyHead.fNextSelected == oldFocusRow
				&& (((IndexOf(oldFocusRow->fNextSelected)
						< IndexOf(oldFocusRow)) == up)
					|| fFocusRow == oldFocusRow->fNextSelected)) {
					// if the focus row is selected, if:
					// 1. the previous focus row is last in the selection
					// 2a. the next selected row is now the focus row
					// 2b. or the next selected row is beyond the focus row
					//	   in the move direction
					// then deselect the previous focus row
				fSelectionListDummyHead.fNextSelected
					= oldFocusRow->fNextSelected;
				if (fSelectionListDummyHead.fNextSelected != NULL) {
					fSelectionListDummyHead.fNextSelected->fPrevSelected
						= &fSelectionListDummyHead;
					oldFocusRow->fNextSelected = NULL;
				}
				oldFocusRow->fPrevSelected = NULL;
			}

			fLastSelectedItem = fFocusRow;
		}
	} else
		Invalidate(fFocusRowRect);

	if (verticalScroll != 0) {
		BScrollBar* vScrollBar = ScrollBar(B_VERTICAL);
		float min, max;
		vScrollBar->GetRange(&min, &max);
		if (verticalScroll < min)
			verticalScroll = min;
		else if (verticalScroll > max)
			verticalScroll = max;

		vScrollBar->SetValue(verticalScroll);
	}

	if (newRow && updateSelection)
		fMasterView->SelectionChanged();
}


void
OutlineView::MoveFocusToVisibleRect()
{
	fFocusRow = 0;
	ChangeFocusRow(true, true, false);
}


BRow*
OutlineView::CurrentSelection(BRow* lastSelected) const
{
	BRow* row;
	if (lastSelected == 0)
		row = fSelectionListDummyHead.fNextSelected;
	else
		row = lastSelected->fNextSelected;


	if (row == &fSelectionListDummyHead)
		row = 0;

	return row;
}


void
OutlineView::ToggleFocusRowSelection(bool selectRange)
{
	if (fFocusRow == 0)
		return;

	if (selectRange && fSelectionMode == B_MULTIPLE_SELECTION_LIST)
		SelectRange(fLastSelectedItem, fFocusRow);
	else {
		if (fFocusRow->fNextSelected != 0) {
			// Unselect row
			fFocusRow->fNextSelected->fPrevSelected = fFocusRow->fPrevSelected;
			fFocusRow->fPrevSelected->fNextSelected = fFocusRow->fNextSelected;
			fFocusRow->fPrevSelected = 0;
			fFocusRow->fNextSelected = 0;
		} else {
			// Select row
			if (fSelectionMode == B_SINGLE_SELECTION_LIST)
				DeselectAll();

			fFocusRow->fNextSelected = fSelectionListDummyHead.fNextSelected;
			fFocusRow->fPrevSelected = &fSelectionListDummyHead;
			fFocusRow->fNextSelected->fPrevSelected = fFocusRow;
			fFocusRow->fPrevSelected->fNextSelected = fFocusRow;
		}
	}

	fLastSelectedItem = fFocusRow;
	fMasterView->SelectionChanged();
	Invalidate(fFocusRowRect);
}


void
OutlineView::RemoveRow(BRow* row)
{
	if (row == NULL)
		return;
	fRows.RemoveItem(row);
	FixScrollBar(false);
	fItemsHeight -=  row->Height();

	BRect invalid;
	if (FindRect(row, &invalid)) {
		invalid.bottom = Bounds().bottom;
		if (invalid.IsValid())
			Invalidate(invalid);
	}
	
	int32 indent = 0;
	float top = 0.0;

	if (FindRow(fVisibleRect.top, &indent, &top) == NULL && ScrollBar(B_VERTICAL) != NULL) {
		// after removing this row, no rows are actually visible any more,
		// force a scroll to make them visible again
		if (fItemsHeight > fVisibleRect.Height())
			ScrollBy(0.0, fItemsHeight - fVisibleRect.Height() - Bounds().top);
		else
			ScrollBy(0.0, -Bounds().top);
	}	
	// Remove this from the selection if necessary
	if (row->fNextSelected != 0) {
		row->fNextSelected->fPrevSelected = row->fPrevSelected;
		row->fPrevSelected->fNextSelected = row->fNextSelected;
		row->fPrevSelected = 0;
		row->fNextSelected = 0;
		fMasterView->SelectionChanged();
	}

	fCurrentColumn = 0;
	fCurrentRow = 0;
	fCurrentField = 0;
}


BRowContainer*
OutlineView::RowList()
{
	return &fRows;
}


void
OutlineView::UpdateRow(BRow* row)
{
	if (row) {
		BRowContainer* list = &fRows;

		if(list) {
			int32 rowIndex = list->IndexOf(row);
			ASSERT(rowIndex >= 0);
			ASSERT(list->ItemAt(rowIndex) == row);

			bool rowMoved = false;
			if (rowIndex > 0 && CompareRows(list->ItemAt(rowIndex - 1), row) > 0)
				rowMoved = true;

			if (rowIndex < list->CountItems() - 1 && CompareRows(list->ItemAt(rowIndex + 1),
				row) < 0)
				rowMoved = true;

			if (rowMoved) {
				// Sort location of this row has changed.
				// Remove and re-add in the right spot
				//SortList(list, parentIsVisible && (parentRow == NULL || parentRow->fIsExpanded));
				SortList(list,true);
			} 

		}
	}
}


void
OutlineView::AddRow(BRow* row, int32 Index)
{
	if (!row)
		return;

	if (fMasterView->SortingEnabled() && !fSortColumns->IsEmpty()) {
		AddSorted(&fRows, row);
	} else {
		// Note, a -1 index implies add to end if sorting is not enabled
		if (Index < 0 || Index >= fRows.CountItems())
			fRows.AddItem(row);
		else
			fRows.AddItem(row, Index);
	}
	fItemsHeight += dynamic_cast<BRow*>(row)->Height() + 1;

	FixScrollBar(false);

	BRect newRowRect;
	bool newRowIsInOpenBranch = FindRect(row, &newRowRect);

	if (fFocusRow && fFocusRowRect.top > newRowRect.bottom) {
		// The focus row has moved.
		Invalidate(fFocusRowRect);
		FindRect(fFocusRow, &fFocusRowRect);
		Invalidate(fFocusRowRect);
	}
	//maybe this needed to remove too parentwise
	if (newRowIsInOpenBranch) {
		if (fCurrentState == INACTIVE) {
			if (newRowRect.bottom < fVisibleRect.top) {
				// The new row is totally above the current viewport, move
				// everything down and redraw the first line.
				BRect source(fVisibleRect);
				BRect dest(fVisibleRect);
				source.bottom -= row->Height() + 1;
				dest.top += row->Height() + 1;
				CopyBits(source, dest);
				Invalidate(BRect(fVisibleRect.left, fVisibleRect.top, fVisibleRect.right,
					fVisibleRect.top + newRowRect.Height()));
			} else if (newRowRect.top < fVisibleRect.bottom) {
				// New item is somewhere in the current region.  Scroll everything
				// beneath it down and invalidate just the new row rect.
				BRect source(fVisibleRect.left, newRowRect.top, fVisibleRect.right,
					fVisibleRect.bottom - newRowRect.Height());
				BRect dest(source);
				dest.OffsetBy(0, newRowRect.Height() + 1);
				CopyBits(source, dest);
				Invalidate(newRowRect);
			} // otherwise, this is below the currently visible region
		} else {
			// Adding the item may have caused the item that the user is currently
			// selected to move.  This would cause annoying drawing and interaction
			// bugs, as the position of that item is cached.  If this happens, resize
			// the scroll bar, then scroll back so the selected item is in view.
			BRect targetRect;
			if (FindRect(fTargetRow, &targetRect)) {
				float delta = targetRect.top - fTargetRowTop;
				if (delta != 0) {
					// This causes a jump because ScrollBy will copy a chunk of the view.
					// Since the actual contents of the view have been offset, we don't
					// want this, we just want to change the virtual origin of the window.
					// Constrain the clipping region so everything is clipped out so no
					// copy occurs.
					//
					//	xxx this currently doesn't work if the scroll bars aren't enabled.
					//  everything will still move anyway.  A minor annoyance.
					BRegion emptyRegion;
					ConstrainClippingRegion(&emptyRegion);
					PushState();
					ScrollBy(0, delta);
					PopState();
					ConstrainClippingRegion(NULL);

					fTargetRowTop += delta;
					fClickPoint.y += delta;
				}
			}
		}
	}

}

void
OutlineView::FixScrollBar(bool scrollToFit)
{
	BScrollBar* vScrollBar = ScrollBar(B_VERTICAL);
	if (vScrollBar) {
		if (fItemsHeight > fVisibleRect.Height()) {
			float maxScrollBarValue = fItemsHeight - fVisibleRect.Height();
			vScrollBar->SetProportion(fVisibleRect.Height() / fItemsHeight);

			// If the user is scrolled down too far when making the range smaller, the list
			// will jump suddenly, which is undesirable.  In this case, don't fix the scroll
			// bar here. In ScrollTo, it checks to see if this has occured, and will
			// fix the scroll bars sneakily if the user has scrolled up far enough.
			if (scrollToFit || vScrollBar->Value() <= maxScrollBarValue) {
				vScrollBar->SetRange(0.0, maxScrollBarValue);
				vScrollBar->SetSteps(20.0, fVisibleRect.Height());
			}
		} else if (vScrollBar->Value() == 0.0 || fItemsHeight == 0.0)
			vScrollBar->SetRange(0.0, 0.0);		// disable scroll bar.
	}
}


void
OutlineView::AddSorted(BRowContainer* list, BRow* row)
{
	if (list && row) {
		// Find general vicinity with binary search.
		int32 lower = 0;
		int32 upper = list->CountItems()-1;
		while( lower < upper ) {
			int32 middle = lower + (upper-lower+1)/2;
			int32 cmp = CompareRows(row, list->ItemAt(middle));
			if( cmp < 0 ) upper = middle-1;
			else if( cmp > 0 ) lower = middle+1;
			else lower = upper = middle;
		}

		// At this point, 'upper' and 'lower' at the last found item.
		// Arbitrarily use 'upper' and determine the final insertion
		// point -- either before or after this item.
		if( upper < 0 ) upper = 0;
		else if( upper < list->CountItems() ) {
			if( CompareRows(row, list->ItemAt(upper)) > 0 ) upper++;
		}

		if (upper >= list->CountItems())
			list->AddItem(row);				// Adding to end.
		else
			list->AddItem(row, upper);		// Insert
	}
}


int32
OutlineView::CompareRows(BRow* row1, BRow* row2)
{
	int32 itemCount (fSortColumns->CountItems());
	if (row1 && row2) {
		for (int32 index = 0; index < itemCount; index++) {
			BColumn* column = (BColumn*) fSortColumns->ItemAt(index);
			int comp = 0;
			BField* field1 = (BField*) row1->GetField(column->fFieldID);
			BField* field2 = (BField*) row2->GetField(column->fFieldID);
			if (field1 && field2)
				comp = column->CompareFields(field1, field2);

			if (!column->fSortAscending)
				comp = -comp;

			if (comp != 0)
				return comp;
		}
	}
	return 0;
}


void
OutlineView::FrameResized(float width, float height)
{
	fVisibleRect.right = fVisibleRect.left + width;
	fVisibleRect.bottom = fVisibleRect.top + height;
	FixScrollBar(true);
	_inherited::FrameResized(width, height);
}


void
OutlineView::ScrollTo(BPoint position)
{
	fVisibleRect.OffsetTo(position.x, position.y);

	// In FixScrollBar, we might not have been able to change the size of
	// the scroll bar because the user was scrolled down too far.  Take
	// this opportunity to sneak it in if we can.
	BScrollBar* vScrollBar = ScrollBar(B_VERTICAL);
	float maxScrollBarValue = fItemsHeight - fVisibleRect.Height();
	float min, max;
	vScrollBar->GetRange(&min, &max);
	if (max != maxScrollBarValue && position.y > maxScrollBarValue)
		FixScrollBar(true);

	_inherited::ScrollTo(position);
}


const BRect&
OutlineView::VisibleRect() const
{
	return fVisibleRect;
}


bool
OutlineView::FindVisibleRect(BRow* row, BRect* _rect)
{
	if (row && _rect) {
		float line = 0.0;
		BRow	*iterRow	= NULL;
		for (int32 i =0; i< fRows.CountItems();i++) {
			iterRow = fRows.ItemAt(i);
			if (iterRow == row) {
				_rect->Set(fVisibleRect.left, line, fVisibleRect.right,
					line + row->Height());
				return line <= fVisibleRect.bottom;
			}

			line += iterRow->Height() + 1;
		}
	}
	return false;
}


bool
OutlineView::FindRect(const BRow* row, BRect* _rect)
{
	float line = 0.0;
	BRow	*iterRow	= NULL;
	for (int32 i =0; i< fRows.CountItems();i++) {
		iterRow = fRows.ItemAt(i);
		if (iterRow == row) {
			_rect->Set(fVisibleRect.left, line, fVisibleRect.right,
				line + row->Height());
			return true;
		}

		line += iterRow->Height() + 1;
	}

	return false;
}


void
OutlineView::ScrollTo(const BRow* row)
{
	BRect rect;
	if (FindRect(row, &rect)) {
		BRect bounds = Bounds();
		if (rect.top < bounds.top)
			ScrollTo(BPoint(bounds.left, rect.top));
		else if (rect.bottom > bounds.bottom)
			ScrollBy(0, rect.bottom - bounds.bottom);
	}
}


void
OutlineView::DeselectAll()
{
	// Invalidate all selected rows
	float line = 0.0;
	BRow	*row	= NULL;
	for (int32 i =0; i< fRows.CountItems();i++) {
		row = fRows.ItemAt(i);
		if (line > fVisibleRect.bottom)
			break;

		if (line + row->Height() > fVisibleRect.top) {
			if (row->fNextSelected != 0)
				Invalidate(BRect(fVisibleRect.left, line, fVisibleRect.right,
					line + row->Height()));
		}

		line += row->Height() + 1;
	}

	// Set items not selected
	while (fSelectionListDummyHead.fNextSelected != &fSelectionListDummyHead) {
		BRow* row = fSelectionListDummyHead.fNextSelected;
		row->fNextSelected->fPrevSelected = row->fPrevSelected;
		row->fPrevSelected->fNextSelected = row->fNextSelected;
		row->fNextSelected = 0;
		row->fPrevSelected = 0;
	}
}


BRow*
OutlineView::FocusRow() const
{
	return fFocusRow;
}


void
OutlineView::SetFocusRow(BRow* row, bool Select)
{
	if (row) {
		if (Select)
			AddToSelection(row);

		if (fFocusRow == row)
			return;

		Invalidate(fFocusRowRect); // invalidate previous

		fTargetRow = fFocusRow = row;

		FindVisibleRect(fFocusRow, &fFocusRowRect);
		Invalidate(fFocusRowRect); // invalidate current

		fFocusRowRect.right = 10000;
		fMasterView->SelectionChanged();
	}
}


bool
OutlineView::SortList(BRowContainer* list, bool isVisible)
{
	if (list) {
		// Shellsort
		BRow** items
			= (BRow**) BObjectList<BRow>::Private(list).AsBList()->Items();
		int32 numItems = list->CountItems();
		int h;
		for (h = 1; h < numItems / 9; h = 3 * h + 1)
			;

		for (;h > 0; h /= 3) {
			for (int step = h; step < numItems; step++) {
				BRow* temp = items[step];
				int i;
				for (i = step - h; i >= 0; i -= h) {
					if (CompareRows(temp, items[i]) < 0)
						items[i + h] = items[i];
					else
						break;
				}

				items[i + h] = temp;
			}
		}

		if (isVisible) {
			Invalidate();

			InvalidateCachedPositions();
			int lockCount = Window()->CountLocks();
			for (int i = 0; i < lockCount; i++)
				Window()->Unlock();

			while (lockCount--)
				if (!Window()->Lock())
					return false;	// Window is gone...
		}
	}
	return true;
}


int32
OutlineView::DeepSortThreadEntry(void* _outlineView)
{
	((OutlineView*) _outlineView)->DeepSort();
	return 0;
}


void
OutlineView::DeepSort()
{
	bool haveLock = SortList(&fRows, true);
		if (!haveLock)
			return ;	// window is gone.
	Window()->Unlock();
}


void
OutlineView::StartSorting()
{
	// If this view is not yet attached to a window, don't start a sort thread!
	if (Window() == NULL)
		return;

	if (fSortThread != B_BAD_THREAD_ID) {
		thread_info tinfo;
		if (get_thread_info(fSortThread, &tinfo) == B_OK) {
			// Unlock window so this won't deadlock (sort thread is probably
			// waiting to lock window).

			int lockCount = Window()->CountLocks();
			for (int i = 0; i < lockCount; i++)
				Window()->Unlock();

			fSortCancelled = true;
			int32 status;
			wait_for_thread(fSortThread, &status);

			while (lockCount--)
				if (!Window()->Lock())
					return ;	// Window is gone...
		}
	}

	fSortCancelled = false;
	fSortThread = spawn_thread(DeepSortThreadEntry, "sort_thread", B_NORMAL_PRIORITY, this);
	resume_thread(fSortThread);
}


void
OutlineView::SelectRange(BRow* start, BRow* end)
{
	if (!start || !end)
		return;

	if (start == end)	// start is always selected when this is called
		return;
	BRow	*row	= NULL;
	for (int32 i =0; i< fRows.CountItems();i++) {
		row = fRows.ItemAt(i);
		if (row == end) {
			// reverse selection, swap to fix special case
			BRow* temp = start;
			start = end;
			end = temp;
			break;
		} else if (row == start)
			break;
	}

	for (int32 i =0; i< fRows.CountItems();i++) {
		row = fRows.ItemAt(i);
		if (row) {
			if (row->fNextSelected == 0) {
				row->fNextSelected = fSelectionListDummyHead.fNextSelected;
				row->fPrevSelected = &fSelectionListDummyHead;
				row->fNextSelected->fPrevSelected = row;
				row->fPrevSelected->fNextSelected = row;
			}
		} else
			break;

		if (row == end)
			break;
	}

	Invalidate();  // xxx make invalidation smaller
}


int32
OutlineView::IndexOf(BRow* row)
{
	if (row) 
		return fRows.IndexOf(row);
	return B_ERROR;
}


void
OutlineView::InvalidateCachedPositions()
{
	if (fFocusRow)
		FindRect(fFocusRow, &fFocusRowRect);
}


float
OutlineView::GetColumnPreferredWidth(BColumn* column)
{
	float preferred = 0.0;
	BRow	*row	= NULL;
	for (int32 i =0; i< fRows.CountItems();i++) {
		row = fRows.ItemAt(i);
		BField* field = row->GetField(column->fFieldID);
		if (field) {
			float width = column->GetPreferredWidth(field, this);
			preferred = max_c(preferred, width);
		}
	}

	BString name;
	column->GetColumnName(&name);
	preferred = max_c(preferred, StringWidth(name));

	// Constrain to preferred width. This makes the method do a little
	// more than asked, but it's for convenience.
	if (preferred < column->MinWidth())
		preferred = column->MinWidth();
	else if (preferred > column->MaxWidth())
		preferred = column->MaxWidth();

	return preferred;
}
