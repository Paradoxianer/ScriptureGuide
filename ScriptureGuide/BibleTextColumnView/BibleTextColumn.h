/*
 * Copyright 2020, Paradoxon <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_TEXT_COLUMN_H
#define BIBLE_TEXT_COLUMN_H

#include <ColumnListView.h>
#include <ColumnListTypes.h>
#include <Language.h>

#include <versekey.h>
#include <swkey.h>
#include <swmodule.h>

#include <map>

#include <SupportDefs.h>

using namespace sword;
using namespace std;


class BibleTextColumn : public BTitledColumn {
public:

						BibleTextColumn(const char *moduleName, 
										float width, float minWidth,
										float maxWidth, alignment align);
										
	//+++ BColumn Methods
										
	virtual	void		DrawField(BField* field, BRect rect, BView* parent);
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

	// +++ Key navigations Methods
	const char*			GetKey();
	const char*			GetTestament() const;	
	const char*			GetBook() const;
	int					GetChapter() const;
	int					GetVerse() const;
	
	status_t			SetBook(char* book);	
	status_t			SetChapter(int ichapter);
	status_t			SetVerse(int iverse);
	status_t			SetKey(const char *iKey);
	
	status_t			NextBook();
	status_t			NextChapter();
	status_t			NextVerse();
	
	status_t			PrevBook();
	status_t			PrevChapter();
	status_t			PrevVerse();
	
	VerseKey&			KeyAt(int32 index);	
	// --- Key navigation Methods

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

protected:
	void				_UpdateBibleText();
	
private:
	SWMgr				*fManager;
	SWModule			*fModule;

	BLanguage			language;
	
	bool				fIsLineBreak;
	bool				fShowVerseNumbers;

};


#endif // BIBLE_TEXT_COLUMN_H
