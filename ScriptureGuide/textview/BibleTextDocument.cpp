/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Catalog.h>
#include <Locale.h>

#include <versekey.h>

#include "constants.h"
#include "BibleTextDocument.h"

using namespace sword;
using namespace std;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BibleTextDocument"


BibleTextDocument::BibleTextDocument(char iKey, char *moduleName) 
	: TextDocument()
{
	BLocale::Default()->GetLanguage(&language);
	fVerseStyle = new CharacterStyle();
	fNumberStyle = new CharacterStyle();
	fNumberStyle->SetForegroundColor(BLUE);
	fNumberStyle->SetBold(true);
	
	fModule	= NULL;
	//module->addRenderFilter(new GBFPlain());
	//const char *error = module->SetKey(iKey);
	//printf("ERROR setting Key %s", error);
}


const char* BibleTextDocument::GetKey() 
{
	return fModule->getKeyText();
}


const char* BibleTextDocument::GetTestament() const
{
}
	

const char* BibleTextDocument::GetBook() const
{
	((VerseKey*)fModule->getKey())->getBook();
}


int BibleTextDocument::GetChapter() const
{
	((VerseKey*)fModule->getKey())->getChapter();
}


int BibleTextDocument::GetVerse() const
{
	((VerseKey*)fModule->getKey())->getVerse();
}


status_t BibleTextDocument::SetBook(char book)
{
	((VerseKey*)fModule->getKey())->setBook(book);
}


status_t BibleTextDocument::SetChapter(int iChapter)
{
	((VerseKey*)fModule->getKey())->setChapter(iChapter);
}


status_t BibleTextDocument::SetVerse(int iVerse)
{
	((VerseKey*)fModule->getKey())->setVerse(iVerse);
}


status_t BibleTextDocument::SetKey(const char* iKey)
{
	fModule->setKey(new VerseKey(iKey));
}


status_t BibleTextDocument::NextBook()
{
	_UpdateBibleText();
}


status_t BibleTextDocument::NextChapter()
{
	fModule->increment();
	_UpdateBibleText();
}


status_t BibleTextDocument::NextVerse()
{
}


status_t BibleTextDocument::PrevBook()
{
	fModule->decrement();
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


status_t BibleTextDocument::SetModule(SWModule* mod)
{
	VerseKey key = ((VerseKey *)fModule->getKey());
	fModule=mod;
	fModule->setKey(key);
}


SWModule* BibleTextDocument::CurrentModule(void)
{
	return fModule;
}


VerseKey& BibleTextDocument::KeyAt(int32 index)
{
}


Paragraph& BibleTextDocument::ParagraphFor(SWKey key)
{
}


const ParagraphStyle& BibleTextDocument::ParagraphStyleFor(SWKey key)
{
	return ParagraphFor(key).Style();
}


void BibleTextDocument::_UpdateBibleText()
{
	ParagraphStyle bibleVerseStyle;
	bibleVerseStyle.SetJustify(true);
	Paragraph paragraph;
	
	BString oldtxt("1"), newtxt("2");
	BString currentbook("Matthäus");
	int32	highlightStart = 0;
	int32	highlightEnd = 0;
	VerseKey key = fModule->getKey();
	uint16 versecount = key.getVerseMax();
	if (fModule == NULL)
	{
		paragraph = Paragraph(bibleVerseStyle);
		paragraph.Append(TextSpan(
				B_TRANSLATE("No Modules installed\n\n \
				Please use ScriptureGuideManager to download the books you want."),
				*fVerseStyle));
		Append(paragraph);
		return;
	}
	if (strcmp(fModule->getType(), "Biblical Texts")==0) 
	{
		BString text(fModule->renderText());

		if (text.CountChars()<1)
		{
			// this condition will only happen if the module is only one particular
			// testament.
			paragraph = Paragraph(bibleVerseStyle);
			paragraph.Append(TextSpan(
							B_TRANSLATE("This module does not have this section."),
							*fVerseStyle));
			Append(paragraph);
			return;
		}
		for (uint16 currentverse = 1; currentverse <= versecount; currentverse++)
		{
			paragraph = Paragraph(bibleVerseStyle);

			// Get the verse for processing
			text.SetTo(fModule->renderText());
			
			if (text.CountChars() < 1)
				continue;
			
			// Remove <P> tags and 0xc2 0xb6 sequences to carriage returns. 
			// The crazy hex sequence is actually the UTF-8 encoding for the 
			// paragraph symbol. If we convert them to \n's, output looks funky
			text.RemoveAll("\x0a\x0a");
			text.RemoveAll("\xc2\xb6 ");
			text.RemoveAll("<P> ");
			
			if (fIsLineBreak)
				text += "\n";
			
			if (fShowVerseNumbers)
			{
				BString string;
				string << " " << currentverse << " ";
				paragraph.Append(TextSpan(string, *fNumberStyle));
			}
			paragraph.Append(TextSpan(text.String(),*fVerseStyle));
			Append(paragraph);
		}
	}
}

