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

	void				Select(int start, int end);
	status_t			SelectVerse(int vStart, int vEnd);
	
	const char*			VerseForSelection();
	
	status_t			SetModule(SWModule* mod);
	status_t			SetModule(const char* modulName);
 	SWModule*			CurrentModule(void);
	
	VerseKey&			KeyAt(int32 index);	
	
	void				SetShowVerseNumbers(bool showVerseNumber);
	
	bool				GetShowVersenumbers(){return fShowVerseNumbers;};
	

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
