/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include <gbfplain.h>
#include <versekey.h>

#include "BibleTextRow.h"
#include "BibleTextColumn.h"
#include "BibleTextColumnView.h"
#include "BibleTextField.h"
#include "BibleTitleView.h"



BibleTextColumnView::BibleTextColumnView(char *name, SWMgr *fManager, VerseKey *key)
	: BColumnListView(name, 0,B_NO_BORDER, false),
	fManager(fManager),
	fVerseKey(key)
{
	BLocale::Default()->GetLanguage(&fLanguage);	
	ModMap::iterator it;
	SWModule* currentmodule = NULL;
	BibleTextColumn *tmpColumn = NULL;
	BibleTitleView  *btView = new BibleTitleView();
	SetTitleView(btView);
	int32 i =0;
	for (it = fManager->Modules.begin(); it != fManager->Modules.end(); it++)
	{
		currentmodule = (*it).second;
		i++;
		currentmodule->addRenderFilter(new GBFPlain());
		tmpColumn = new BibleTextColumn(fManager, currentmodule, 100, 350, 8192);
		AddColumn(tmpColumn,i);
		if (i > 1)
			SetColumnVisible(tmpColumn,false);

	}
	SetSortingEnabled(false);
	_InsertRowForKeys();
}

void BibleTextColumnView::_InsertRowForKeys()
{
	printf("BibleTextColumnView::_InsertRowForKeys(%s)\n",fVerseKey->getText());
	//first delete all Rows
	BRow *r = RowAt(0);
	while (r != NULL)
	{
		RemoveRow(r);
		//@ToDo check if we need to delete all Fields???
		delete r;
		r = RowAt(0);
	}

	BibleTextRow	*row			= NULL;
	VerseKey		*key			= NULL;
	int32			countColumns	= CountColumns();
	int32			versecount		= fVerseKey->getVerseMax();
	for (uint16 currentverse = 1; currentverse <= versecount; currentverse++)
	{
		key = new VerseKey(fVerseKey);
		key->setVerse(currentverse);
		row = new BibleTextRow();
		for (uint32 column = 0; column < countColumns; column++)
		{
			row->SetField(new BibleTextField(key),column);
		}
		AddRow(row);
	}
}

const char* BibleTextColumnView::GetKey() 
{
	//return fModule->getKeyText();
	return NULL;
}


const char* BibleTextColumnView::GetTestament() const
{
	//**ToDo implement
	return NULL;
}
	

const char* BibleTextColumnView::GetBook() const
{
	//return ((VerseKey*)fModule->getKey())->getBookName();
	return NULL;
}


int BibleTextColumnView::GetChapter() const
{
	//return ((VerseKey*)fModule->getKey())->getChapter();
	return 0;
}


int BibleTextColumnView::GetVerse() const
{
	//return ((VerseKey*)fModule->getKey())->getVerse();
	return 0;
}


status_t BibleTextColumnView::SetBook(char* book)
{
	fVerseKey->setLocale(fLanguage.Code());
	fVerseKey->setBookName(book);
	
	_InsertRowForKeys();
	Invalidate();
	//ToDo return correct Error;
	return B_OK;
	
}


status_t BibleTextColumnView::SetChapter(int iChapter)
{
	/*((VerseKey*)fModule->getKey())->setChapter(iChapter);
	char error = fModule->popError();
	printf("Error %c\n",error);
	//ToDo return correct Error
	;*/
	fVerseKey->setChapter(iChapter);
	_InsertRowForKeys();
	Invalidate();
	return B_OK;
}


status_t BibleTextColumnView::SetVerse(int iVerse)
{
	AddToSelection(RowAt(iVerse));
	Invalidate();
	return B_OK;
}


status_t BibleTextColumnView::SetKey(const char* iKey)
{

	/*VerseKey key = new VerseKey();
	key.setLocale(language.Code());
	key.setText(iKey);
	char error = fModule->setKey(key);
	printf("Error SetKey %c\n",error);
	//ToDo return correct Error*/
	return B_OK;
}


status_t BibleTextColumnView::NextBook()
{
	return B_OK;
}


status_t BibleTextColumnView::NextChapter()
{
	return B_OK;
}


status_t BibleTextColumnView::NextVerse()
{
	fVerseKey++;
	return B_OK;
}


status_t BibleTextColumnView::PrevBook()
{
	return B_OK;
}


status_t BibleTextColumnView::PrevChapter()
{
	return B_OK;
}


status_t BibleTextColumnView::PrevVerse()
{
	fVerseKey--;
	return B_OK;
}


void BibleTextColumnView::Select(int start, int end)
{
}


status_t BibleTextColumnView::SelectVerse(int vStart, int vEnd)
{
	return B_OK;
}


const char* BibleTextColumnView::VerseForSelection()
{
	return NULL;
}
