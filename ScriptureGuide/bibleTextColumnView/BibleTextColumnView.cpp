/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <gbfplain.h>
#include <versekey.h>

#include "BibleTextColumn.h"
#include "BibleTextColumnView.h"
#include "BibleTextField.h"



BibleTextColumnView::BibleTextColumnView(char *name, SWMgr *fManager, VerseKey *key)
	: BColumnListView(name, 0,B_NO_BORDER, false),
	fManager(fManager),
	fVerseKey(key)
{
	ModMap::iterator it;
	SWModule* currentmodule = NULL;
	BibleTextColumn *tmpColumn = NULL;
	int32 i =0;
	for (it = fManager->Modules.begin(); it != fManager->Modules.end(); it++)
	{
		currentmodule = (*it).second;
		i++;
		currentmodule->addRenderFilter(new GBFPlain());
		tmpColumn = new BibleTextColumn(fManager, currentmodule, 100, 350, 2000);
		AddColumn(tmpColumn,i);
		if (i > 1)
			SetColumnVisible(tmpColumn,false);

	}
	SetSortingEnabled(false);
	_InsertRowForKeys();
}

void BibleTextColumnView::_InsertRowForKeys()
{
	BRow		*row			= NULL;
	VerseKey	*key			= NULL;
	int32		countColumns	= CountColumns();
	int32		versecount		= fVerseKey->getVerseMax();
	for (uint16 currentverse = 1; currentverse <= versecount; currentverse++)
	{
		key = new VerseKey(fVerseKey);
		key->setVerse(currentverse);
		row = new BRow(100.0);
		for (uint32 column = 0; column < countColumns; column++)
		{
			row->SetField( new BibleTextField(key),column);
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
	/*((VerseKey*)fModule->getKey())->setBookName(book);
	char error = fModule->popError();
	printf("Error %c\n",error);*/
	//ToDo return correct Error
	;
	return B_OK;
	
}


status_t BibleTextColumnView::SetChapter(int iChapter)
{
	/*((VerseKey*)fModule->getKey())->setChapter(iChapter);
	char error = fModule->popError();
	printf("Error %c\n",error);
	//ToDo return correct Error
	;*/
	return B_OK;
}


status_t BibleTextColumnView::SetVerse(int iVerse)
{
	/*((VerseKey*)fModule->getKey())->setVerse(iVerse);
	char error = fModule->popError();
	printf("Error %c\n",error);
	//ToDo return correct Error
	;*/
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
