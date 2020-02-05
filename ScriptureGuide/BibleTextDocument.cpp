/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "BibleTextDocument.h"

BibleTextDocument::BibleTextDocument(char iKey, char *moduleName) 
	: TextDocument()
{
	module	= new SWModule(moduleName);
	module->addRenderFilter(new GBFPlain());
	const char *error = module->SetKey(iKey);
	printf("ERROR setting Key %s", error);
}


char* BibleTextDocument::GetKey()
{
	module->getKeyText();
}


const char* BibleTextDocument::GetTestament() const
{
}
	

const char* BibleTextDocument::GetBook() const
{
	((VerseKey*)module->getKey())->GetBook();
}


int BibleTextDocument::GetChapter() const
{
	((VerseKey*)module->getKey())->GetChapter();
}


int BibleTextDocument::GetVerse() const
{
	((VerseKey*)module->getKey())->GetVerse();
}


status_t BibleTextDocument::SetBook(const char *book)
{
	((VerseKey*)module->getKey())->SetBook(book);
}


status_t BibleTextDocument::SetChapter(int iChapter)
{
	((VerseKey*)module->getKey())->SetChapter(iChapter);
}


status_t BibleTextDocument::SetVerse(int iVerse)
{
	((VerseKey*)module->getKey())->SetVerse(iVerse);
}


status_t BibleTextDocument::SetKey(const char iKey)
{
	module->SetKey(new VerseKey(iKey));
}


status_t BibleTextDocument::NextBook()
{
	_UpdateBibleText();
}


status_t BibleTextDocument::NextChapter()
{
	module->increment();
	_UpdateBibleText();
}


status_t BibleTextDocument::NextVerse()
{
}


status_t BibleTextDocument::PrevBook()
{
	module->decrement();
	_UpdateBibleText();
}


status_t BibleTextDocument::PrevChapter()
{
	_UpdateBibleText();
}


status_t BibleTextDocument::PrevVerse()
{
}


void BibleTextDocument::Select(int start, int end)
{
}


status_t BibleTextDocument::SelectVerse(int vStart, int vEnd)
{
}


const char* BibleTextDocument::VerseForSelection()
{
}


status_t BibleTextDocument::SetModule(SGModule* mod)
{
	VerseKey key = ((VerseKey *)module->GetKey());
	module=mod;
	module->SetKey(key);
}


SGModule* BibleTextDocument::CurrentModule(void)
{
	return module;
}


VerseKey& BibleTextDocument::KeyAt(int32 index)
{
}


BibleVerseLayout& BibleTextDocument::ParagraphStyleFor(SWKey key)
{
}


void BibleTextDocument::_UpdateBibleText()
{
}

