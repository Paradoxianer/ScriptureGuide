/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <ControlLook.h>
#include <Debug.h>
#include <MenuItem.h>
#include <Region.h>
#include <ScrollBar.h>
#include <String.h>
#include <Window.h>

#include <stdio.h>


#include "TitleView.h"
#include "ColumnListView.h";

TitleView::TitleView(BRect rect, OutlineView* horizontalSlave,
	BList* visibleColumns, BList* sortColumns, BColumnListView* listView,
	uint32 resizingMode)
	:
	BView(rect, "title_view", resizingMode, B_WILL_DRAW | B_FRAME_EVENTS),
	fOutlineView(horizontalSlave),
	fColumns(visibleColumns),
	fSortColumns(sortColumns),
//	fColumnsWidth(0),
	fVisibleRect(rect.OffsetToCopy(0, 0)),
	fCurrentState(INACTIVE),
	fColumnPop(NULL),
	fMasterView(listView),
	fEditMode(false),
	fColumnFlags(B_ALLOW_COLUMN_MOVE | B_ALLOW_COLUMN_RESIZE
		| B_ALLOW_COLUMN_POPUP | B_ALLOW_COLUMN_REMOVE)
{
	SetViewColor(B_TRANSPARENT_COLOR);

#if DOUBLE_BUFFERED_COLUMN_RESIZE
	// xxx this needs to be smart about the size of the backbuffer.
	BRect doubleBufferRect(0, 0, 600, 35);
	fDrawBuffer = new BBitmap(doubleBufferRect, B_RGB32, true);
	fDrawBufferView = new BView(doubleBufferRect, "double_buffer_view",
		B_FOLLOW_ALL_SIDES, 0);
	fDrawBuffer->Lock();
	fDrawBuffer->AddChild(fDrawBufferView);
	fDrawBuffer->Unlock();
#endif

	fUpSortArrow = new BBitmap(BRect(0, 0, 7, 7), B_CMAP8);
	fDownSortArrow = new BBitmap(BRect(0, 0, 7, 7), B_CMAP8);

	fUpSortArrow->SetBits((const void*) kUpSortArrow8x8, 64, 0, B_CMAP8);
	fDownSortArrow->SetBits((const void*) kDownSortArrow8x8, 64, 0, B_CMAP8);

	fResizeCursor = new BCursor(B_CURSOR_ID_RESIZE_EAST_WEST);
	fMinResizeCursor = new BCursor(B_CURSOR_ID_RESIZE_EAST);
	fMaxResizeCursor = new BCursor(B_CURSOR_ID_RESIZE_WEST);
	fColumnMoveCursor = new BCursor(B_CURSOR_ID_MOVE);

	FixScrollBar(true);
}


TitleView::~TitleView()
{
	delete fColumnPop;
	fColumnPop = NULL;

#if DOUBLE_BUFFERED_COLUMN_RESIZE
	delete fDrawBuffer;
#endif
	delete fUpSortArrow;
	delete fDownSortArrow;

	delete fResizeCursor;
	delete fMaxResizeCursor;
	delete fMinResizeCursor;
	delete fColumnMoveCursor;
}


void
TitleView::ColumnAdded(BColumn* column)
{
//	fColumnsWidth += column->Width();
	FixScrollBar(false);
	Invalidate();
}


void
TitleView::ColumnResized(BColumn* column, float oldWidth)
{
//	fColumnsWidth += column->Width() - oldWidth;
	FixScrollBar(false);
	Invalidate();
}


void
TitleView::SetColumnVisible(BColumn* column, bool visible)
{
	if (column->fVisible == visible)
		return;

	// If setting it visible, do this first so we can find its position
	// to invalidate.  If hiding it, do it last.
	if (visible)
		column->fVisible = visible;

	BRect titleInvalid;
	GetTitleRect(column, &titleInvalid);

	// Now really set the visibility
	column->fVisible = visible;

//	if (visible)
//		fColumnsWidth += column->Width();
//	else
//		fColumnsWidth -= column->Width();

	BRect outlineInvalid(fOutlineView->VisibleRect());
	outlineInvalid.left = titleInvalid.left;
	titleInvalid.right = outlineInvalid.right;

	Invalidate(titleInvalid);
	fOutlineView->Invalidate(outlineInvalid);

	FixScrollBar(false);
}


void
TitleView::GetTitleRect(BColumn* findColumn, BRect* _rect)
{
	float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	int32 numColumns = fColumns->CountItems();
	for (int index = 0; index < numColumns; index++) {
		BColumn* column = (BColumn*) fColumns->ItemAt(index);
		if (!column->IsVisible())
			continue;

		if (column == findColumn) {
			_rect->Set(leftEdge, 0, leftEdge + column->Width(),
				fVisibleRect.bottom);
			return;
		}

		leftEdge += column->Width() + 1;
	}

	TRESPASS();
}


int32
TitleView::FindColumn(BPoint position, float* _leftEdge)
{
	float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	int32 numColumns = fColumns->CountItems();
	for (int index = 0; index < numColumns; index++) {
		BColumn* column = (BColumn*) fColumns->ItemAt(index);
		if (!column->IsVisible())
			continue;

		if (leftEdge > position.x)
			break;

		if (position.x >= leftEdge
			&& position.x <= leftEdge + column->Width()) {
			*_leftEdge = leftEdge;
			return index;
		}

		leftEdge += column->Width() + 1;
	}

	return 0;
}


void
TitleView::FixScrollBar(bool scrollToFit)
{
	BScrollBar* hScrollBar = ScrollBar(B_HORIZONTAL);
	if (hScrollBar == NULL)
		return;

	float virtualWidth = _VirtualWidth();

	if (virtualWidth > fVisibleRect.Width()) {
		hScrollBar->SetProportion(fVisibleRect.Width() / virtualWidth);

		// Perform the little trick if the user is scrolled over too far.
		// See OutlineView::FixScrollBar for a more in depth explanation
		float maxScrollBarValue = virtualWidth - fVisibleRect.Width();
		if (scrollToFit || hScrollBar->Value() <= maxScrollBarValue) {
			hScrollBar->SetRange(0.0, maxScrollBarValue);
			hScrollBar->SetSteps(50, fVisibleRect.Width());
		}
	} else if (hScrollBar->Value() == 0.0) {
		// disable scroll bar.
		hScrollBar->SetRange(0.0, 0.0);
	}
}


void
TitleView::DragSelectedColumn(BPoint position)
{
	float invalidLeft = fSelectedColumnRect.left;
	float invalidRight = fSelectedColumnRect.right;

	float leftEdge;
	int32 columnIndex = FindColumn(position, &leftEdge);
	fSelectedColumnRect.OffsetTo(leftEdge, 0);

	MoveColumn(fSelectedColumn, columnIndex);

	fSelectedColumn->fVisible = true;
	ComputeDragBoundries(fSelectedColumn, position);

	// Redraw the new column position
	GetTitleRect(fSelectedColumn, &fSelectedColumnRect);
	invalidLeft = MIN(fSelectedColumnRect.left, invalidLeft);
	invalidRight = MAX(fSelectedColumnRect.right, invalidRight);

	Invalidate(BRect(invalidLeft, 0, invalidRight, fVisibleRect.bottom));
	fOutlineView->Invalidate(BRect(invalidLeft, 0, invalidRight,
		fOutlineView->VisibleRect().bottom));

	DrawTitle(this, fSelectedColumnRect, fSelectedColumn, true);
}


void
TitleView::MoveColumn(BColumn* column, int32 index)
{
	fColumns->RemoveItem((void*) column);

	if (-1 == index) {
		// Re-add the column at the end of the list.
		fColumns->AddItem((void*) column);
	} else {
		fColumns->AddItem((void*) column, index);
	}
}


void
TitleView::SetColumnFlags(column_flags flags)
{
	fColumnFlags = flags;
}


float
TitleView::MarginWidth() const
{
	return MAX(kLeftMargin, fMasterView->LatchWidth()) + kRightMargin;
}


void
TitleView::ResizeSelectedColumn(BPoint position, bool preferred)
{
	float minWidth = fSelectedColumn->MinWidth();
	float maxWidth = fSelectedColumn->MaxWidth();

	float oldWidth = fSelectedColumn->Width();
	float originalEdge = fSelectedColumnRect.left + oldWidth;
	if (preferred) {
		float width = fOutlineView->GetColumnPreferredWidth(fSelectedColumn);
		fSelectedColumn->SetWidth(width);
	} else if (position.x > fSelectedColumnRect.left + maxWidth)
		fSelectedColumn->SetWidth(maxWidth);
	else if (position.x < fSelectedColumnRect.left + minWidth)
		fSelectedColumn->SetWidth(minWidth);
	else
		fSelectedColumn->SetWidth(position.x - fSelectedColumnRect.left - 1);

	float dX = fSelectedColumnRect.left + fSelectedColumn->Width()
		 - originalEdge;
	if (dX != 0) {
		float columnHeight = fVisibleRect.Height();
		BRect originalRect(originalEdge, 0, 1000000.0, columnHeight);
		BRect movedRect(originalRect);
		movedRect.OffsetBy(dX, 0);

		// Update the size of the title column
		BRect sourceRect(0, 0, fSelectedColumn->Width(), columnHeight);
		BRect destRect(sourceRect);
		destRect.OffsetBy(fSelectedColumnRect.left, 0);

#if DOUBLE_BUFFERED_COLUMN_RESIZE
		fDrawBuffer->Lock();
		DrawTitle(fDrawBufferView, sourceRect, fSelectedColumn, false);
		fDrawBufferView->Sync();
		fDrawBuffer->Unlock();

		CopyBits(originalRect, movedRect);
		DrawBitmap(fDrawBuffer, sourceRect, destRect);
#else
		CopyBits(originalRect, movedRect);
		DrawTitle(this, destRect, fSelectedColumn, false);
#endif

		// Update the body view
		BRect slaveSize = fOutlineView->VisibleRect();
		BRect slaveSource(originalRect);
		slaveSource.bottom = slaveSize.bottom;
		BRect slaveDest(movedRect);
		slaveDest.bottom = slaveSize.bottom;
		fOutlineView->CopyBits(slaveSource, slaveDest);
		fOutlineView->RedrawColumn(fSelectedColumn, fSelectedColumnRect.left,
			fResizingFirstColumn);

//		fColumnsWidth += dX;

		// Update the cursor
		if (fSelectedColumn->Width() == minWidth)
			SetViewCursor(fMinResizeCursor, true);
		else if (fSelectedColumn->Width() == maxWidth)
			SetViewCursor(fMaxResizeCursor, true);
		else
			SetViewCursor(fResizeCursor, true);

		ColumnResized(fSelectedColumn, oldWidth);
	}
}


void
TitleView::ComputeDragBoundries(BColumn* findColumn, BPoint)
{
	float previousColumnLeftEdge = -1000000.0;
	float nextColumnRightEdge = 1000000.0;

	bool foundColumn = false;
	float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	int32 numColumns = fColumns->CountItems();
	for (int index = 0; index < numColumns; index++) {
		BColumn* column = (BColumn*) fColumns->ItemAt(index);
		if (!column->IsVisible())
			continue;

		if (column == findColumn) {
			foundColumn = true;
			continue;
		}

		if (foundColumn) {
			nextColumnRightEdge = leftEdge + column->Width();
			break;
		} else
			previousColumnLeftEdge = leftEdge;

		leftEdge += column->Width() + 1;
	}

	float rightEdge = leftEdge + findColumn->Width();

	fLeftDragBoundry = MIN(previousColumnLeftEdge + findColumn->Width(),
		leftEdge);
	fRightDragBoundry = MAX(nextColumnRightEdge, rightEdge);
}


void
TitleView::DrawTitle(BView* view, BRect rect, BColumn* column, bool depressed)
{
	BRect drawRect;
	drawRect = rect;

	font_height fh;
	GetFontHeight(&fh);

	float baseline = floor(drawRect.top + fh.ascent
		+ (drawRect.Height() + 1 - (fh.ascent + fh.descent)) / 2);

	BRect bgRect = rect;

	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	view->SetHighColor(tint_color(base, B_DARKEN_2_TINT));
	view->StrokeLine(bgRect.LeftBottom(), bgRect.RightBottom());

	bgRect.bottom--;
	bgRect.right--;

	if (depressed)
		base = tint_color(base, B_DARKEN_1_TINT);

	be_control_look->DrawButtonBackground(view, bgRect, rect, base, 0,
		BControlLook::B_TOP_BORDER | BControlLook::B_BOTTOM_BORDER);

	view->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	view->StrokeLine(rect.RightTop(), rect.RightBottom());

	// If no column given, nothing else to draw.
	if (column == NULL)
		return;

	view->SetHighColor(fMasterView->Color(B_COLOR_HEADER_TEXT));

	BFont font;
	GetFont(&font);
	view->SetFont(&font);

	int sortIndex = fSortColumns->IndexOf(column);
	if (sortIndex >= 0) {
		// Draw sort notation.
		BPoint upperLeft(drawRect.right - kSortIndicatorWidth, baseline);

		if (fSortColumns->CountItems() > 1) {
			char str[256];
			sprintf(str, "%d", sortIndex + 1);
			const float w = view->StringWidth(str);
			upperLeft.x -= w;

			view->SetDrawingMode(B_OP_COPY);
			view->MovePenTo(BPoint(upperLeft.x + kSortIndicatorWidth,
				baseline));
			view->DrawString(str);
		}

		float bmh = fDownSortArrow->Bounds().Height()+1;

		view->SetDrawingMode(B_OP_OVER);

		if (column->fSortAscending) {
			BPoint leftTop(upperLeft.x, drawRect.top + (drawRect.IntegerHeight()
				- fDownSortArrow->Bounds().IntegerHeight()) / 2);
			view->DrawBitmapAsync(fDownSortArrow, leftTop);
		} else {
			BPoint leftTop(upperLeft.x, drawRect.top + (drawRect.IntegerHeight()
				- fUpSortArrow->Bounds().IntegerHeight()) / 2);
			view->DrawBitmapAsync(fUpSortArrow, leftTop);
		}

		upperLeft.y = baseline - bmh + floor((fh.ascent + fh.descent - bmh) / 2);
		if (upperLeft.y < drawRect.top)
			upperLeft.y = drawRect.top;

		// Adjust title stuff for sort indicator
		drawRect.right = upperLeft.x - 2;
	}

	if (drawRect.right > drawRect.left) {
#if CONSTRAIN_CLIPPING_REGION
		BRegion clipRegion(drawRect);
		view->PushState();
		view->ConstrainClippingRegion(&clipRegion);
#endif
		view->MovePenTo(BPoint(drawRect.left + 8, baseline));
		view->SetDrawingMode(B_OP_OVER);
		view->SetHighColor(fMasterView->Color(B_COLOR_HEADER_TEXT));
		column->DrawTitle(drawRect, view);

#if CONSTRAIN_CLIPPING_REGION
		view->PopState();
#endif
	}
}


float
TitleView::_VirtualWidth() const
{
	float width = MarginWidth();

	int32 count = fColumns->CountItems();
	for (int32 i = 0; i < count; i++) {
		BColumn* column = reinterpret_cast<BColumn*>(fColumns->ItemAt(i));
		if (column->IsVisible())
			width += column->Width();
	}

	return width;
}


void
TitleView::Draw(BRect invalidRect)
{
	float columnLeftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	for (int32 columnIndex = 0; columnIndex < fColumns->CountItems();
		columnIndex++) {

		BColumn* column = (BColumn*) fColumns->ItemAt(columnIndex);
		if (!column->IsVisible())
			continue;

		if (columnLeftEdge > invalidRect.right)
			break;

		if (columnLeftEdge + column->Width() >= invalidRect.left) {
			BRect titleRect(columnLeftEdge, 0,
				columnLeftEdge + column->Width(), fVisibleRect.Height());
			DrawTitle(this, titleRect, column,
				(fCurrentState == DRAG_COLUMN_INSIDE_TITLE
				&& fSelectedColumn == column));
		}

		columnLeftEdge += column->Width() + 1;
	}


	// bevels for right title margin
	if (columnLeftEdge <= invalidRect.right) {
		BRect titleRect(columnLeftEdge, 0, Bounds().right + 2,
			fVisibleRect.Height());
		DrawTitle(this, titleRect, NULL, false);
	}

	// bevels for left title margin
	if (invalidRect.left < MAX(kLeftMargin, fMasterView->LatchWidth())) {
		BRect titleRect(0, 0, MAX(kLeftMargin, fMasterView->LatchWidth()) - 1,
			fVisibleRect.Height());
		DrawTitle(this, titleRect, NULL, false);
	}

#if DRAG_TITLE_OUTLINE
	// (internal) column drag indicator
	if (fCurrentState == DRAG_COLUMN_INSIDE_TITLE) {
		BRect dragRect(fSelectedColumnRect);
		dragRect.OffsetTo(fCurrentDragPosition.x - fClickPoint.x, 0);
		if (dragRect.Intersects(invalidRect)) {
			SetHighColor(0, 0, 255);
			StrokeRect(dragRect);
		}
	}
#endif
}


void
TitleView::ScrollTo(BPoint position)
{
	fOutlineView->ScrollBy(position.x - fVisibleRect.left, 0);
	fVisibleRect.OffsetTo(position.x, position.y);

	// Perform the little trick if the user is scrolled over too far.
	// See OutlineView::ScrollTo for a more in depth explanation
	float maxScrollBarValue = _VirtualWidth() - fVisibleRect.Width();
	BScrollBar* hScrollBar = ScrollBar(B_HORIZONTAL);
	float min, max;
	hScrollBar->GetRange(&min, &max);
	if (max != maxScrollBarValue && position.x > maxScrollBarValue)
		FixScrollBar(true);

	_inherited::ScrollTo(position);
}


void
TitleView::MessageReceived(BMessage* message)
{
	if (message->what == kToggleColumn) {
		int32 num;
		if (message->FindInt32("be:field_num", &num) == B_OK) {
			for (int index = 0; index < fColumns->CountItems(); index++) {
				BColumn* column = (BColumn*) fColumns->ItemAt(index);
				if (column == NULL)
					continue;

				if (column->LogicalFieldNum() == num)
					column->SetVisible(!column->IsVisible());
			}
		}
		return;
	}

	BView::MessageReceived(message);
}


void
TitleView::MouseDown(BPoint position)
{
	if (fEditMode)
		return;

	int32 buttons = 1;
	Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	if (buttons == B_SECONDARY_MOUSE_BUTTON
		&& (fColumnFlags & B_ALLOW_COLUMN_POPUP)) {
		// Right mouse button -- bring up menu to show/hide columns.
		if (fColumnPop == NULL)
			fColumnPop = new BPopUpMenu("Columns", false, false);

		fColumnPop->RemoveItems(0, fColumnPop->CountItems(), true);
		BMessenger me(this);
		for (int index = 0; index < fColumns->CountItems(); index++) {
			BColumn* column = (BColumn*) fColumns->ItemAt(index);
			if (column == NULL)
				continue;

			BString name;
			column->GetColumnName(&name);
			BMessage* message = new BMessage(kToggleColumn);
			message->AddInt32("be:field_num", column->LogicalFieldNum());
			BMenuItem* item = new BMenuItem(name.String(), message);
			item->SetMarked(column->IsVisible());
			item->SetTarget(me);
			fColumnPop->AddItem(item);
		}

		BPoint screenPosition = ConvertToScreen(position);
		BRect sticky(screenPosition, screenPosition);
		sticky.InsetBy(-5, -5);
		fColumnPop->Go(ConvertToScreen(position), true, false, sticky, true);

		return;
	}

	fResizingFirstColumn = true;
	float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	for (int index = 0; index < fColumns->CountItems(); index++) {
		BColumn* column = (BColumn*)fColumns->ItemAt(index);
		if (column == NULL || !column->IsVisible())
			continue;

		if (leftEdge > position.x + kColumnResizeAreaWidth / 2)
			break;

		// check for resizing a column
		float rightEdge = leftEdge + column->Width();

		if (column->ShowHeading()) {
			if (position.x > rightEdge - kColumnResizeAreaWidth / 2
				&& position.x < rightEdge + kColumnResizeAreaWidth / 2
				&& column->MaxWidth() > column->MinWidth()
				&& (fColumnFlags & B_ALLOW_COLUMN_RESIZE) != 0) {

				int32 clicks = 0;
				fSelectedColumn = column;
				fSelectedColumnRect.Set(leftEdge, 0, rightEdge,
					fVisibleRect.Height());
				Window()->CurrentMessage()->FindInt32("clicks", &clicks);
				if (clicks == 2 || buttons == B_TERTIARY_MOUSE_BUTTON) {
					ResizeSelectedColumn(position, true);
					fCurrentState = INACTIVE;
					break;
				}
				fCurrentState = RESIZING_COLUMN;
				fClickPoint = BPoint(position.x - rightEdge - 1,
					position.y - fSelectedColumnRect.top);
				SetMouseEventMask(B_POINTER_EVENTS,
					B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
				break;
			}

			fResizingFirstColumn = false;

			// check for clicking on a column
			if (position.x > leftEdge && position.x < rightEdge) {
				fCurrentState = PRESSING_COLUMN;
				fSelectedColumn = column;
				fSelectedColumnRect.Set(leftEdge, 0, rightEdge,
					fVisibleRect.Height());
				DrawTitle(this, fSelectedColumnRect, fSelectedColumn, true);
				fClickPoint = BPoint(position.x - fSelectedColumnRect.left,
					position.y - fSelectedColumnRect.top);
				SetMouseEventMask(B_POINTER_EVENTS,
					B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
				break;
			}
		}
		leftEdge = rightEdge + 1;
	}
}


void
TitleView::MouseMoved(BPoint position, uint32 transit,
	const BMessage* dragMessage)
{
	if (fEditMode)
		return;

	// Handle column manipulation
	switch (fCurrentState) {
		case RESIZING_COLUMN:
			ResizeSelectedColumn(position - BPoint(fClickPoint.x, 0));
			break;

		case PRESSING_COLUMN: {
			if (abs((int32)(position.x - (fClickPoint.x
					+ fSelectedColumnRect.left))) > kColumnResizeAreaWidth
				|| abs((int32)(position.y - (fClickPoint.y
					+ fSelectedColumnRect.top))) > kColumnResizeAreaWidth) {
				// User has moved the mouse more than the tolerable amount,
				// initiate a drag.
				if (transit == B_INSIDE_VIEW || transit == B_ENTERED_VIEW) {
					if(fColumnFlags & B_ALLOW_COLUMN_MOVE) {
						fCurrentState = DRAG_COLUMN_INSIDE_TITLE;
						ComputeDragBoundries(fSelectedColumn, position);
						SetViewCursor(fColumnMoveCursor, true);
#if DRAG_TITLE_OUTLINE
						BRect invalidRect(fSelectedColumnRect);
						invalidRect.OffsetTo(position.x - fClickPoint.x, 0);
						fCurrentDragPosition = position;
						Invalidate(invalidRect);
#endif
					}
				} else {
					if(fColumnFlags & B_ALLOW_COLUMN_REMOVE) {
						// Dragged outside view
						fCurrentState = DRAG_COLUMN_OUTSIDE_TITLE;
						fSelectedColumn->SetVisible(false);
						BRect dragRect(fSelectedColumnRect);

						// There is a race condition where the mouse may have
						// moved by the time we get to handle this message.
						// If the user drags a column very quickly, this
						// results in the annoying bug where the cursor is
						// outside of the rectangle that is being dragged
						// around.  Call GetMouse with the checkQueue flag set
						// to false so we can get the most recent position of
						// the mouse.  This minimizes this problem (although
						// it is currently not possible to completely eliminate
						// it).
						uint32 buttons;
						GetMouse(&position, &buttons, false);
						dragRect.OffsetTo(position.x - fClickPoint.x,
							position.y - dragRect.Height() / 2);
						BeginRectTracking(dragRect, B_TRACK_WHOLE_RECT);
					}
				}
			}

			break;
		}

		case DRAG_COLUMN_INSIDE_TITLE: {
			if (transit == B_EXITED_VIEW
				&& (fColumnFlags & B_ALLOW_COLUMN_REMOVE)) {
				// Dragged outside view
				fCurrentState = DRAG_COLUMN_OUTSIDE_TITLE;
				fSelectedColumn->SetVisible(false);
				BRect dragRect(fSelectedColumnRect);

				// See explanation above.
				uint32 buttons;
				GetMouse(&position, &buttons, false);

				dragRect.OffsetTo(position.x - fClickPoint.x,
					position.y - fClickPoint.y);
				BeginRectTracking(dragRect, B_TRACK_WHOLE_RECT);
			} else if (position.x < fLeftDragBoundry
				|| position.x > fRightDragBoundry) {
				DragSelectedColumn(position - BPoint(fClickPoint.x, 0));
			}

#if DRAG_TITLE_OUTLINE
			// Set up the invalid rect to include the rect for the previous
			// position of the drag rect, as well as the new one.
			BRect invalidRect(fSelectedColumnRect);
			invalidRect.OffsetTo(fCurrentDragPosition.x - fClickPoint.x, 0);
			if (position.x < fCurrentDragPosition.x)
				invalidRect.left -= fCurrentDragPosition.x - position.x;
			else
				invalidRect.right += position.x - fCurrentDragPosition.x;

			fCurrentDragPosition = position;
			Invalidate(invalidRect);
#endif
			break;
		}

		case DRAG_COLUMN_OUTSIDE_TITLE:
			if (transit == B_ENTERED_VIEW) {
				// Drag back into view
				EndRectTracking();
				fCurrentState = DRAG_COLUMN_INSIDE_TITLE;
				fSelectedColumn->SetVisible(true);
				DragSelectedColumn(position - BPoint(fClickPoint.x, 0));
			}

			break;

		case INACTIVE:
			// Check for cursor changes if we are over the resize area for
			// a column.
			BColumn* resizeColumn = 0;
			float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
			for (int index = 0; index < fColumns->CountItems(); index++) {
				BColumn* column = (BColumn*) fColumns->ItemAt(index);
				if (!column->IsVisible())
					continue;

				if (leftEdge > position.x + kColumnResizeAreaWidth / 2)
					break;

				float rightEdge = leftEdge + column->Width();
				if (position.x > rightEdge - kColumnResizeAreaWidth / 2
					&& position.x < rightEdge + kColumnResizeAreaWidth / 2
					&& column->MaxWidth() > column->MinWidth()) {
					resizeColumn = column;
					break;
				}

				leftEdge = rightEdge + 1;
			}

			// Update the cursor
			if (resizeColumn) {
				if (resizeColumn->Width() == resizeColumn->MinWidth())
					SetViewCursor(fMinResizeCursor, true);
				else if (resizeColumn->Width() == resizeColumn->MaxWidth())
					SetViewCursor(fMaxResizeCursor, true);
				else
					SetViewCursor(fResizeCursor, true);
			} else
				SetViewCursor(B_CURSOR_SYSTEM_DEFAULT, true);
			break;
	}
}


void
TitleView::MouseUp(BPoint position)
{
	if (fEditMode)
		return;

	switch (fCurrentState) {
		case RESIZING_COLUMN:
			ResizeSelectedColumn(position - BPoint(fClickPoint.x, 0));
			fCurrentState = INACTIVE;
			FixScrollBar(false);
			break;

		case PRESSING_COLUMN: {
			if (fMasterView->SortingEnabled()) {
				if (fSortColumns->HasItem(fSelectedColumn)) {
					if ((modifiers() & B_CONTROL_KEY) == 0
						&& fSortColumns->CountItems() > 1) {
						fSortColumns->MakeEmpty();
						fSortColumns->AddItem(fSelectedColumn);
					}

					fSelectedColumn->fSortAscending
						= !fSelectedColumn->fSortAscending;
				} else {
					if ((modifiers() & B_CONTROL_KEY) == 0)
						fSortColumns->MakeEmpty();

					fSortColumns->AddItem(fSelectedColumn);
					fSelectedColumn->fSortAscending = true;
				}

				fOutlineView->StartSorting();
			}

			fCurrentState = INACTIVE;
			Invalidate();
			break;
		}

		case DRAG_COLUMN_INSIDE_TITLE:
			fCurrentState = INACTIVE;

#if DRAG_TITLE_OUTLINE
			Invalidate();	// xxx Can make this smaller
#else
			Invalidate(fSelectedColumnRect);
#endif
			SetViewCursor(B_CURSOR_SYSTEM_DEFAULT, true);
			break;

		case DRAG_COLUMN_OUTSIDE_TITLE:
			fCurrentState = INACTIVE;
			EndRectTracking();
			SetViewCursor(B_CURSOR_SYSTEM_DEFAULT, true);
			break;

		default:
			;
	}
}


void
TitleView::FrameResized(float width, float height)
{
	fVisibleRect.right = fVisibleRect.left + width;
	fVisibleRect.bottom = fVisibleRect.top + height;
	FixScrollBar(true);
}

