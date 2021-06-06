/*
 * Copyright 2020, Paradoxon <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_TEXT_COLUMN_H
#define BIBLE_TEXT_COLUMN_H

#include <Bitmap.h>

#include <Language.h>
#include <TextView.h>

#include <versekey.h>
#include <swkey.h>
#include <swmgr.h>
#include <swmodule.h>

#include <map>

#include <SupportDefs.h>

#include "ColumnListView.h"
#include "ColumnListView.h"
#include "ColumnTypes.h"

#include "TextDocument.h"
#include "TextDocumentLayout.h"
#include "TextEditor.h"

using namespace sword;
using namespace std;


class BibleTextColumn : public BTitledColumn {
public:
						BibleTextColumn(SWMgr *manager, SWModule* mod, 
										float width, float minWidth,
										float maxWidth, alignment align = B_ALIGN_LEFT);
						BibleTextColumn(SWMgr *manager, const char *moduleName, 
										float width, float minWidth,
										float maxWidth, alignment align = B_ALIGN_LEFT);
						~BibleTextColumn();
										
	//+++ BColumn Methods
										
	virtual	void		DrawField(BField* _field, BRect rect, BView* parent);
	virtual	int			CompareFields(BField* field1, BField* field2);
	virtual	float		GetPreferredWidth(BField* field, BView* parent) const;
	virtual	bool		AcceptsField(const BField* field) const;
		
	virtual void		MouseMoved(BColumnListView* parent, BRow* row,
							BField* field, BRect fieldRect,
							BPoint point, uint32 buttons, int32 code);
	virtual void		MouseDown(BColumnListView* parent, BRow* row,
							BField* field, BRect fieldRect,
							BPoint point, uint32 buttons);
	virtual	void		MouseUp(BColumnListView* parent, BRow* row,
							BField* field);
	
	//---- BColumn Methods

	
	// +++ Text related Methods
	void				Select(int start, int end);
	status_t			SelectVerse(int vStart, int vEnd);	
	
	const char*			VerseForSelection();
	// --- Text related Methods

	// +++ Module related Methods
	status_t			SetModule(SWModule* mod);
	status_t			SetModule(const char* modulName);
 	SWModule*			CurrentModule(void);
	// --- Module related Methods
	
	// +++ Config Settings Methods
	void				SetShowVerseNumbers(bool showVerseNumber);
	bool				GetShowVersenumbers()
							{return fShowVerseNumbers;};
	// --- Config Settings Method
	
	// +++ Layout related Interface
	
	virtual	BSize		MinSize();
	virtual	BSize		MaxSize();
	virtual	BSize		PreferredSize();

	virtual	bool		HasHeightForWidth();
	virtual	void		GetHeightForWidth(float width, float* min,
							float* max, float* preferred);
	// --- Layout related Interface
	
	// +++ TextDocumentView interface
	void				SetTextDocument(
							const TextDocumentRef& document);

	void				SetEditingEnabled(bool enabled);
	void				SetTextEditor(
							const TextEditorRef& editor);

	void				SetInsets(float inset);
	void				SetInsets(float horizontal, float vertical);
	void				SetInsets(float left, float top, float right,
							float bottom);

	void				SetSelectionEnabled(bool enabled);
	void				SetCaret(BPoint where, bool extendSelection);

	void				SelectAll();
	bool				HasSelection() const;
	void				GetSelection(int32& start, int32& end) const;
	// --- TextDocumentView interface
	
private:
	float				_TextLayoutWidth(float viewWidth) const;

	void				_ShowCaret(bool show);
	void				_BlinkCaret();
	void				_DrawCaret(int32 textOffset);
	void				_DrawSelection();
	void				_GetSelectionShape(BShape& shape,
							int32 start, int32 end);

	

protected:
	void				Init();
	void 				DrawBibeltext(const char* string, BView* parent, BRect rect);
	
private:
	SWMgr				*fManager;
	SWModule			*fModule;

	BLanguage			language;
	
	bool				fIsLineBreak;
	bool				fShowVerseNumbers;
	BTextView			*verseRenderer;
	BBitmap				*verseBitmap;
	
	
	TextDocumentRef		fTextDocument;
	TextDocumentLayout	fTextDocumentLayout;
	TextEditorRef		fTextEditor;

	float				fInsetLeft;
	float				fInsetTop;
	float				fInsetRight;
	float				fInsetBottom;

	BRect				fCaretBounds;
	BMessageRunner*		fCaretBlinker;
	int32				fCaretBlinkToken;
	bool				fSelectionEnabled;
	bool				fShowCaret;
	bool				fMouseDown;

};


#endif // BIBLE_TEXT_COLUMN_H
