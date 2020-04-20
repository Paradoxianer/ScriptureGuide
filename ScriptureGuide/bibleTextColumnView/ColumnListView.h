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
/	File:			ColumnListView.h
/
/   Description:    Experimental multi-column list view.
/
/	Copyright 2000+, Be Incorporated, All Rights Reserved
/
*******************************************************************************/


#ifndef _COLUMN_LIST_VIEW_H
#define _COLUMN_LIST_VIEW_H

#include <BeBuild.h>
#include <View.h>
#include <List.h>
#include <Invoker.h>
#include <ListView.h>

#include "ColumnConsts.h"
#include "ObjectList.h"
#include "TitleView.h"

class BScrollBar;

class BRowContainer;
class BColumn;
class BColumnListView;
class BField;
class BRow;


// A single row/column intersection in the list.
class BField {
public:
								BField();
	virtual						~BField();
};

// A single line in the list.  Each line contains a BField object
// for each column in the list, associated by their "logical field"
// index.  Hierarchies are formed by adding other BRow objects as
// a parent of a row, using the AddRow() function in BColumnListView().
class BRow {
public:
								BRow();
								BRow(float height);
	virtual 					~BRow();

			int32				CountFields() const;
			BField*				GetField(int32 logicalFieldIndex);
	const	BField*				GetField(int32 logicalFieldIndex) const;
			void				SetField(BField* field,
									int32 logicalFieldIndex);

	virtual	float 				Height() const;
			bool 				IsExpanded() const;
			bool				IsSelected() const;

			void				Invalidate();

private:
	// Blows up into the debugger if the validation fails.
			void				ValidateFields() const;
			void				ValidateField(const BField* field,
									int32 logicalFieldIndex) const;
private:
			BList				fFields;
			float				fHeight;
			BRow*				fNextSelected;
			BRow*				fPrevSelected;
			BColumnListView*	fList;


	friend class BColumnListView;
	friend class OutlineView;
};

// Information about a single column in the list.  A column knows
// how to display the BField objects that occur at its location in
// each of the list's rows.  See ColumnTypes.h for particular
// subclasses of BField and BColumn that handle common data types.
class BColumn {
public:
								BColumn(float width, float minWidth,
									float maxWidth,
									alignment align = B_ALIGN_LEFT);
	virtual 					~BColumn();

			float				Width() const;
			void				SetWidth(float width);
			float				MinWidth() const;
			float				MaxWidth() const;

	virtual	void				DrawTitle(BRect rect, BView* targetView);
	virtual	void				DrawField(BField* field, BRect rect,
									BView* targetView);
	virtual	int					CompareFields(BField* field1, BField* field2);

	virtual void				MouseMoved(BColumnListView* parent, BRow* row,
									BField* field, BRect fieldRect,
									BPoint point, uint32 buttons, int32 code);
	virtual void				MouseDown(BColumnListView* parent, BRow* row,
									BField* field, BRect fieldRect,
									BPoint point, uint32 buttons);
	virtual	void				MouseUp(BColumnListView* parent, BRow* row,
									BField* field);

	virtual	void				GetColumnName(BString* into) const;
	virtual	float				GetPreferredWidth(BField* field,
									BView* parent) const;

			bool				IsVisible() const;
			void				SetVisible(bool);

			bool				WantsEvents() const;
			void				SetWantsEvents(bool);

			bool				ShowHeading() const;
			void				SetShowHeading(bool);

			alignment			Alignment() const;
			void				SetAlignment(alignment);

			int32				LogicalFieldNum() const;

	/*!
		\param field The BField derivative to validate.

			Implement this function on your BColumn derivatives to validate
			BField derivatives that your BColumn will be drawing/manipulating.

			This function will be called when BFields are added to the Column,
			use dynamic_cast<> to determine if it is of a kind that your
			BColumn know how ot handle. return false if it is not.

			\note The debugger will be called if you return false from here
			with information about what type of BField and BColumn and the
			logical field index where it occured.

			\note Do not call the inherited version of this, it just returns
			true;
	  */
	virtual	bool				AcceptsField(const BField* field) const;

private:
			float				fWidth;
			float 				fMinWidth;
			float				fMaxWidth;
			bool				fVisible;
			int32				fFieldID;
			BColumnListView*	fList;
			bool				fSortAscending;
			bool				fWantsEvents;
			bool				fShowHeading;
			alignment			fAlignment;

	friend class OutlineView;
	friend class BColumnListView;
	friend class TitleView;
};

// The column list view class.
class BColumnListView : public BView, public BInvoker {
public:
								BColumnListView(BRect rect,
									const char* name, uint32 resizingMode,
									uint32 flags, border_style = B_NO_BORDER,
									bool showHorizontalScrollbar = true);
								BColumnListView(const char* name,
									uint32 flags, border_style = B_NO_BORDER,
									bool showHorizontalScrollbar = true);
	virtual						~BColumnListView();

	// Interaction
	virtual	bool				InitiateDrag(BPoint, bool wasSelected);
	virtual	void				MessageDropped(BMessage*, BPoint point);

	virtual	status_t			Invoke(BMessage* message = NULL);
	virtual	void				ItemInvoked();
	virtual	void				SetInvocationMessage(BMessage* message);
			BMessage* 			InvocationMessage() const;
			uint32 				InvocationCommand() const;
			BRow* 				FocusRow() const;
			void 				SetFocusRow(int32 index, bool select = false);
			void 				SetFocusRow(BRow* row, bool select = false);
			void 				SetMouseTrackingEnabled(bool);

	// Selection
			list_view_type		SelectionMode() const;
			void 				Deselect(BRow* row);
			void 				AddToSelection(BRow* row);
			void 				DeselectAll();
			BRow*				CurrentSelection(BRow* lastSelected = 0) const;
	virtual	void				SelectionChanged();
	virtual	void				SetSelectionMessage(BMessage* message);
			BMessage*			SelectionMessage();
			uint32				SelectionCommand() const;
			void				SetSelectionMode(list_view_type type);
				// list_view_type is defined in ListView.h.

	// Sorting
			void				SetSortingEnabled(bool);
			bool				SortingEnabled() const;
			void				SetSortColumn(BColumn* column, bool add,
									bool ascending);
			void				ClearSortColumns();

	// The status view is a little area in the lower left hand corner.
			TitleView*			GetTitleView(void){return fTitleView;};
			void				AddStatusView(BView* view);
			BView*				RemoveStatusView();

	// Column Manipulation
			void				AddColumn(BColumn* column,
									int32 logicalFieldIndex);
			void				MoveColumn(BColumn* column, int32 index);
			void				RemoveColumn(BColumn* column);
			int32				CountColumns() const;
			BColumn*			ColumnAt(int32 index) const;
			BColumn*			ColumnAt(BPoint point) const;
			void				SetColumnVisible(BColumn* column,
									bool isVisible);
			void				SetColumnVisible(int32, bool);
			bool				IsColumnVisible(int32) const;
			void				SetColumnFlags(column_flags flags);
			void				ResizeColumnToPreferred(int32 index);
			void				ResizeAllColumnsToPreferred();

	// Row manipulation
			const BRow*			RowAt(int32 index) const;
			BRow*				RowAt(int32 index);
			
			const BRow*			RowAt(BPoint) const;
			BRow*				RowAt(BPoint);
			bool				GetRowRect(const BRow* row, BRect* _rect) const;
			int32				IndexOf(BRow* row);
			int32				CountRows() const;
			void				AddRow(BRow* row);
			void				AddRow(BRow* row, int32 index);

			void				ScrollTo(const BRow* Row);
			void				ScrollTo(BPoint point);

	// Does not delete row or children at this time.
	// todo: Make delete row and children
			void				RemoveRow(BRow* row);
			void				UpdateRow(BRow* row);
			bool				SwapRows(int32 index1, int32 index2);
									
			void				Clear();

			void				InvalidateRow(BRow* row);

	// Appearance (DEPRECATED)
			void				GetFont(BFont* font) const
									{ BView::GetFont(font); }
	virtual	void				SetFont(const BFont* font,
									uint32 mask = B_FONT_ALL);
	virtual	void				SetHighColor(rgb_color);
			void				SetSelectionColor(rgb_color);
			void				SetBackgroundColor(rgb_color);
			void				SetEditColor(rgb_color);
			const rgb_color		SelectionColor() const;
			const rgb_color		BackgroundColor() const;
			const rgb_color		EditColor() const;

	// Appearance (NEW STYLE)
			void				SetColor(ColumnListViewColor colorIndex,
									rgb_color color);
			void				ResetColors();
			void				SetFont(ColumnListViewFont fontIndex,
									const BFont* font,
									uint32 mask = B_FONT_ALL);
			rgb_color			Color(ColumnListViewColor colorIndex) const;
			void				GetFont(ColumnListViewFont fontIndex,
									BFont* font) const;

			BPoint				SuggestTextPosition(const BRow* row,
									const BColumn* column = NULL) const;

			BRect				GetFieldRect(const BRow* row,
									const BColumn* column) const;

	virtual	void				MakeFocus(bool isfocus = true);
			void				SaveState(BMessage* archive);
			void				LoadState(BMessage* archive);

			BView*				ScrollView() const
									{ return (BView*)fOutlineView; }
			void				SetEditMode(bool state);
			void				Refresh();

	virtual BSize				MinSize();
	virtual BSize				PreferredSize();
	virtual BSize				MaxSize();


protected:
	virtual	void 				MessageReceived(BMessage* message);
	virtual	void 				KeyDown(const char* bytes, int32 numBytes);
	virtual	void 				AttachedToWindow();
	virtual	void 				WindowActivated(bool active);
	virtual	void 				Draw(BRect updateRect);

	virtual	void				LayoutInvalidated(bool descendants = false);
	virtual	void				DoLayout();

private:
			void				_Init();
			void				_UpdateColors();
			void				_GetChildViewRects(const BRect& bounds,
									BRect& titleRect, BRect& outlineRect,
									BRect& vScrollBarRect,
									BRect& hScrollBarRect);

			rgb_color 			fColorList[B_COLOR_TOTAL];
			bool				fCustomColors;
			TitleView*			fTitleView;
			OutlineView*		fOutlineView;
			BList 				fColumns;
			BScrollBar*			fHorizontalScrollBar;
			BScrollBar* 		fVerticalScrollBar;
			BList				fSortColumns;
			BView*				fStatusView;
			BMessage*			fSelectionMessage;
			bool				fSortingEnabled;
			border_style		fBorderStyle;
			bool				fShowingHorizontalScrollBar;
};

class BRowContainer : public BObjectList<BRow>
{
};


class OutlineView : public BView {
	typedef BView _inherited;
public:
								OutlineView(BRect, BList* visibleColumns,
									BList* sortColumns,
									BColumnListView* listView);
	virtual						~OutlineView();

	virtual void				Draw(BRect);
	const 	BRect&				VisibleRect() const;

			void				RedrawColumn(BColumn* column, float leftEdge,
									bool isFirstColumn);
			void 				StartSorting();
			float				GetColumnPreferredWidth(BColumn* column);

			void				AddRow(BRow*, int32 index);
			BRow*				CurrentSelection(BRow* lastSelected) const;
			void 				ToggleFocusRowSelection(bool selectRange);
			void 				ChangeFocusRow(bool up, bool updateSelection,
									bool addToCurrentSelection);
			void 				MoveFocusToVisibleRect();
			void 				RemoveRow(BRow*);
			BRowContainer*		RowList();
			void				UpdateRow(BRow*);
			int32				IndexOf(BRow* row);
			void				Deselect(BRow*);
			void				AddToSelection(BRow*);
			void				DeselectAll();
			BRow*				FocusRow() const;
			void				SetFocusRow(BRow* row, bool select);
			BRow*				FindRow(float ypos, float* _top);
			bool				FindRect(const BRow* row, BRect* _rect);
			void				ScrollTo(const BRow* row);

			void				Clear();
			void				SetSelectionMode(list_view_type type);
			list_view_type		SelectionMode() const;
			void				SetMouseTrackingEnabled(bool);
			void				FixScrollBar(bool scrollToFit);
			void				SetEditMode(bool state)
									{ fEditMode = state; }

	virtual void				FrameResized(float width, float height);
	virtual void				ScrollTo(BPoint where);
	virtual void				MouseDown(BPoint where);
	virtual void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual void				MouseUp(BPoint where);
	virtual void				MessageReceived(BMessage* message);

private:
			bool				SortList(BRowContainer* list, bool isVisible);
	static	int32				DeepSortThreadEntry(void* outlineView);
			void				DeepSort();
			void				SelectRange(BRow* start, BRow* end);
			int32				CompareRows(BRow* row1, BRow* row2);
			void				AddSorted(BRowContainer* list, BRow* row);
			void				DeleteRows(BRowContainer* list, bool owner);
			void				InvalidateCachedPositions();
			bool				FindVisibleRect(BRow* row, BRect* _rect);

			BList*				fColumns;
			BList*				fSortColumns;
			float				fItemsHeight;
			BRowContainer		fRows;
			BRect				fVisibleRect;

#if DOUBLE_BUFFERED_COLUMN_RESIZE
			BBitmap*			fDrawBuffer;
			BView*				fDrawBufferView;
#endif

			BRow*				fFocusRow;
			BRect				fFocusRowRect;
			BRow*				fRollOverRow;

			BRow				fSelectionListDummyHead;
			BRow*				fLastSelectedItem;
			BRow*				fFirstSelectedItem;

			thread_id			fSortThread;
			int32				fNumSorted;
			bool				fSortCancelled;

			enum CurrentState {
				INACTIVE,
				ROW_CLICKED,
				DRAGGING_ROWS
			};

			CurrentState		fCurrentState;


			BColumnListView*	fMasterView;
			list_view_type		fSelectionMode;
			bool				fTrackMouse;
			BField*				fCurrentField;
			BRow*				fCurrentRow;
			BColumn*			fCurrentColumn;
			bool				fMouseDown;
			BRect				fFieldRect;
			int32				fCurrentCode;
			bool				fEditMode;

	// State information for mouse/keyboard interaction
			BPoint				fClickPoint;
			bool				fDragging;
			int32				fClickCount;
			BRow*				fTargetRow;
			float				fTargetRowTop;
			float				fDropHighlightY;

};


#endif // _COLUMN_LIST_VIEW_H
