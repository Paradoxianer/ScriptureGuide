/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_TEXT_COLUMN_VIEW_H
#define BIBLE_TEXT_COLUMN_VIEW_H

#include <Locale.h>
#include <SupportDefs.h>

#include <listkey.h>
#include <swmgr.h>
#include <swmodule.h>
#include <versekey.h>

#include "ColumnListView.h"

using namespace sword;


class BibleTextColumnView : public BColumnListView {
public:
						BibleTextColumnView(char *name, SWMgr *fManager, VerseKey *key);
	
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
	const char*			VerseForSelection();
	status_t			SelectVerse(int vStart, int vEnd);
	// --- Key navigation Methods

	void				Select(int start, int end);
	
	void				SetShowVerseNumber(bool showVerseNumber){fShowVerseNumber=showVerseNumber;}
	bool				ShowVerseNumber(){return fShowVerseNumber;};
	

private:
	void				_InsertRowForKeys();
	SWMgr				*fManager;
	VerseKey			*fVerseKey;
	bool				fShowVerseNumber;
	BLanguage			fLanguage;
};


#endif //BIBLE_TEXT_COLUMN_VIEW_H
