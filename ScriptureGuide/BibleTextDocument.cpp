/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "BibleTextDocument.h"

BibleTextDocument::BibleTextDocument(char iKey, char *moduleName) 
	: TextDocument()
{
}


char* BibleTextDocument::GetKey()

	const char* BibleTextDocument::GetTestament() const;	
	const char* BibleTextDocument::GetBook() const;
	int BibleTextDocument::GetChapter() const;
	int BibleTextDocument::GetVerse() const;
	
	status_t BibleTextDocument::SetBook(const char *book);	
	status_t BibleTextDocument::SetChapter(int ichapter);
	status_t BibleTextDocument::SetVerse(int iverse);
	status_t BibleTextDocument::SetKey();
	
	status_t BibleTextDocument::NextBook();
	status_t BibleTextDocument::NextChapter();
	status_t BibleTextDocument::NextVerse();
	
	status_t BibleTextDocument::PrevBook();
	status_t BibleTextDocument::PrevChapter();
	status_t BibleTextDocument::PrevVerse();

	void BibleTextDocument::Select(int start, int end);
	status_t BibleTextDocument::SelectVerse(int vStart, int vEnd);
	
	const char* BibleTextDocument::VerseForSelection();
	
	status_t BibleTextDocument::SetModule(SGModule* mod);
 	SGModule* BibleTextDocument::CurrentModule(void);
	
	VerseKey& BibleTextDocument::KeyAt(int32 index);
	
	BibleVerseLayout& BibleTextDocument::ParagraphStyleFor(SWKey key);


	void BibleTextDocument::_UpdateBibleText();
