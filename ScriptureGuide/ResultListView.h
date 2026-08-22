/*
 * Copyright 2019 Paradoxianer <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef RESULT_LIST_VIEW_H
#define RESULT_LIST_VIEW_H

#include <OutlineListView.h>
#include <String.h>
#include <SupportDefs.h>

#define MAX_DRAG_HEIGHT		200.0
#define ALPHA				170
#define TEXT_OFFSET			5.0

static const float kRowDragSensitivity = 5.0;


class BibleItem : public BListItem
{
public:
					BibleItem(const char* key,const  char* text, const char* highlight = NULL);
	virtual			~BibleItem(void);

	const char*		GetKey(void) const { return fKey; }
	const char*		GetText(void) const { return fText; }
	const char*		GetHighlight(void) const {return fHighlight;}
	void			SetKey(const char* key);
	void			SetText(const char* text);
	void			SetHighlight(const char* highlight);
	
	
	virtual void	DrawItem(BView *owner, BRect frame,
						bool complete = false);
	virtual	void	Update(BView* owner, const BFont* font);


private:
	char*			fKey;
	char*			fText;
	char*			fHighlight;
	float			fBaselineOffset;	
};

class ResultListView : public BOutlineListView{
public:
	ResultListView(const char* name, list_view_type type
					= B_SINGLE_SELECTION_LIST, uint32 flags
					= B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);

	virtual			~ResultListView();
	
	virtual bool	InitiateDrag(BPoint point, int32 index, bool);

	// Which versification the keys in this list count in -- i.e. the
	// searched module's. Travels with every drag (see MakeDragMessage())
	// so whatever receives the drop can reposition the reference into
	// its own counting instead of assuming the two agree (#46). Without
	// it a result dragged out of a German-versified module and appended
	// to a KJV list would be written down as a verse it isn't.
	void			SetSourceVersification(const char* versification)
						{ fSourceVersification = versification; }

protected:
	void			MakeDragMessage(BMessage* message);


private:
	void			Init(void);
	uint32			fDragCommand;
	BString			fSourceVersification;

};


#endif // _H
