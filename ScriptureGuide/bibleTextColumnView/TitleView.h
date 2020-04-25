/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef TITLE_VIEW_H
#define TITLE_VIEW_H

#include <Bitmap.h>
#include <Cursor.h>
#include <List.h>
#include <Message.h>
#include <Point.h>
#include <PopUpMenu.h>
#include <Rect.h>
#include <SupportDefs.h>
#include <View.h>

#include "ColumnConsts.h"

class OutlineView;
class BColumnListView;
class BColumn;

class TitleView : public BView {
	typedef BView _inherited;
public:
								TitleView(BRect frame, OutlineView* outlineView,
									BList* visibleColumns, BList* sortColumns,
									BColumnListView* masterView,
									uint32 resizingMode);
	virtual						~TitleView();

			void				ColumnAdded(BColumn* column);
			void				ColumnResized(BColumn* column, float oldWidth);
			void				SetColumnVisible(BColumn* column, bool visible);

	virtual	void				Draw(BRect updateRect);
	virtual	void				ScrollTo(BPoint where);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				FrameResized(float width, float height);

			void				MoveColumn(BColumn* column, int32 index);
			void				SetColumnFlags(column_flags flags);

			void				SetEditMode(bool state)
									{ fEditMode = state; }

			float				MarginWidth() const;
			void		 		SetOutlineView(OutlineView* outlineView);
			OutlineView* 		GetOutlineView(){return fOutlineView;}
			void		 		SetMasterView(BColumnListView* masterView);
			BColumnListView*	MasterView(){return fMasterView;}
			
			void				SetColumns(BList *columns){fColumns=columns;}
			void				SetSortColumns(BList *sColumns){fSortColumns=sColumns;}
			
	virtual	BPopUpMenu*			CreateContextMenu();

protected:
			BPopUpMenu*			fColumnPop;
			BList*				fColumns;
		

private:
			void				GetTitleRect(BColumn* column, BRect* _rect);
			int32				FindColumn(BPoint where, float* _leftEdge);
			void				FixScrollBar(bool scrollToFit);
			void				DragSelectedColumn(BPoint where);
			void				ResizeSelectedColumn(BPoint where,
									bool preferred = false);
			void				ComputeDragBoundries(BColumn* column,
									BPoint where);
			void				DrawTitle(BView* view, BRect frame,
									BColumn* column, bool depressed);

			float				_VirtualWidth() const;
			
	
			OutlineView*		fOutlineView;
			BList*				fSortColumns;
//			float				fColumnsWidth;
			BRect				fVisibleRect;

#if DOUBLE_BUFFERED_COLUMN_RESIZE
			BBitmap*			fDrawBuffer;
			BView*				fDrawBufferView;
#endif

			enum {
				INACTIVE,
				RESIZING_COLUMN,
				PRESSING_COLUMN,
				DRAG_COLUMN_INSIDE_TITLE,
				DRAG_COLUMN_OUTSIDE_TITLE
			}					fCurrentState;

			BColumnListView*	fMasterView;
			bool				fEditMode;
			int32				fColumnFlags;

	// State information for resizing/dragging
			BColumn*			fSelectedColumn;
			BRect				fSelectedColumnRect;
			bool				fResizingFirstColumn;
			BPoint				fClickPoint; // offset within cell
			float				fLeftDragBoundry;
			float				fRightDragBoundry;
			BPoint				fCurrentDragPosition;


			BBitmap*			fUpSortArrow;
			BBitmap*			fDownSortArrow;

			BCursor*			fResizeCursor;
			BCursor*			fMinResizeCursor;
			BCursor*			fMaxResizeCursor;
			BCursor*			fColumnMoveCursor;
};


#endif // _H
