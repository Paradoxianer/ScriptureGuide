/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "BibleTextColumnViewView.h"

BibleTextColumnView::BibleTextColumnView()
{
}

const char* BibleTextColumnView::GetKey() 
{
	return fModule->getKeyText();
}


const char* BibleTextColumnView::GetTestament() const
{
	//**ToDo implement
	return NULL;
}
	

const char* BibleTextColumnView::GetBook() const
{
	return ((VerseKey*)fModule->getKey())->getBookName();
}


int BibleTextColumnView::GetChapter() const
{
	return ((VerseKey*)fModule->getKey())->getChapter();
}


int BibleTextColumnView::GetVerse() const
{
	return ((VerseKey*)fModule->getKey())->getVerse();
}


status_t BibleTextColumnView::SetBook(char* book)
{
	((VerseKey*)fModule->getKey())->setBookName(book);
	char error = fModule->popError();
	printf("Error %c\n",error);
	//ToDo return correct Error
	;
	return B_OK;
	
}


status_t BibleTextColumnView::SetChapter(int iChapter)
{
	((VerseKey*)fModule->getKey())->setChapter(iChapter);
	char error = fModule->popError();
	printf("Error %c\n",error);
	//ToDo return correct Error
	;
	return B_OK;
}


status_t BibleTextColumnView::SetVerse(int iVerse)
{
	((VerseKey*)fModule->getKey())->setVerse(iVerse);
	char error = fModule->popError();
	printf("Error %c\n",error);
	//ToDo return correct Error
	;
	return B_OK;
}


status_t BibleTextColumnView::SetKey(const char* iKey)
{
	VerseKey key = new VerseKey();
	key.setLocale(language.Code());
	key.setText(iKey);
	char error = fModule->setKey(key);
	printf("Error SetKey %c\n",error);
	//ToDo return correct Error
	return B_OK;
}


status_t BibleTextColumnView::NextBook()
{
}


status_t BibleTextColumnView::NextChapter()
{
	
}


status_t BibleTextColumnView::NextVerse()
{
	(*fListKey)++;
}


status_t BibleTextColumnView::PrevBook()
{
}


status_t BibleTextColumnView::PrevChapter()
{
	
}


status_t BibleTextColumnView::PrevVerse()
{
	(*fListKey)--;
}


void BibleTextColumnView::Select(int start, int end)
{
}


status_t BibleTextColumnView::SelectVerse(int vStart, int vEnd)
{
}


const char* BibleTextColumn::VerseForSelection()
{
}
