/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_TEXT_DOCUMENT_H
#define BIBLE_TEXT_DOCUMENT_H

#include "Paragraph.h"
#include "BibleLayout.h"
#include "TextDocument.h"

#include <versekey.h>
#include <swmodule.h>

#include <map>

#include <SupportDefs.h>


class BibleTextDocument : public TextDocument {
public:
	/**ToDo implement LayoutMessenger to inform all others about layouting
	*/
						BibleTextDocument(char iKey, char *moduleName);

	const char*			GetKey();
	const char*			GetTestament() const;	
	const char*			GetBook() const;
	int					GetChapter() const;
	int					GetVerse() const;
	
	status_t			SetBook(const char *book);	
	status_t			SetChapter(int ichapter);
	status_t			SetVerse(int iverse);
	status_t			SetKey(const char iKey);
	
	status_t			NextBook();
	status_t			NextChapter();
	status_t			NextVerse();
	
	status_t			PrevBook();
	status_t			PrevChapter();
	status_t			PrevVerse();

	void				Select(int start, int end);
	status_t			SelectVerse(int vStart, int vEnd);
	
	const char*			VerseForSelection();
	
	status_t			SetModule(SGModule* mod);
 	SGModule*			CurrentModule(void);
	
	VerseKey&			KeyAt(int32 index);
	
	Paragraph&			ParagraphFor(SWKey key);	
	
	BibleVerseLayout&	ParagraphStyleFor(SWKey key);

protected:
	void				_UpdateBibleText();
	
private:
	SGModule			*module;
	
	map<VerseKey* ,Paragraph*>
						*verseParagraph
						
	CharacterStyle		*fVerseStyle;
	CharacterStyle		*fNumberStyle;
;
	
	BLanguage			language;

};


#endif // BIBLE_TEXT_DOCUMENT_H
