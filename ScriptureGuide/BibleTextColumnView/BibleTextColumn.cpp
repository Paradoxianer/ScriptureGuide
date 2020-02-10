/*
 * Copyright 2020, Paradoxon <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Catalog.h>
#include <Debug.h>
#include <Locale.h>

#include <gbfplain.h>
#include <gbfstrongs.h>
#include <localemgr.h>
#include <markupfiltmgr.h>
#include <swmgr.h>
#include <versekey.h>

#include "constants.h"
#include "BibleTextColumn.h"

using namespace sword;
using namespace std;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BibleTextDocument"

#define CONFIGPATH MODULES_PATH


BibleTextDocument::BibleTextDocument(const char *moduleName, const char* iKey) 
	: TextDocument(),
	fIsLineBreak(false),
	fShowVerseNumbers(true)
{
	PRINT(("key %s module %s\n", iKey, moduleName));
	BLocale::Default()->GetLanguage(&language);
	fVerseStyle = new CharacterStyle();
	fNumberStyle = new CharacterStyle();
	fNumberStyle->SetForegroundColor(BLUE);
	fNumberStyle->SetBold(true);
	fManager = new SWMgr(CONFIGPATH, true, new MarkupFilterMgr(FMT_GBF, ENC_UTF8));
	SetModule(moduleName);
	VerseKey key = new VerseKey();
	key.setLocale(language.Code());
	key.setText(iKey);
	const char error = fModule->SetKey(key);
	printf("ERROR setting Key %c\n", error);
}


const char* BibleTextDocument::GetKey() 
{
	return fModule->getKeyText();
}


const char* BibleTextDocument::GetTestament() const
{
	//**ToDo implement
	return NULL;
}
	

const char* BibleTextDocument::GetBook() const
{
	return ((VerseKey*)fModule->getKey())->getBookName();
}


int BibleTextDocument::GetChapter() const
{
	return ((VerseKey*)fModule->getKey())->getChapter();
}


int BibleTextDocument::GetVerse() const
{
	return ((VerseKey*)fModule->getKey())->getVerse();
}


status_t BibleTextDocument::SetBook(char* book)
{
	((VerseKey*)fModule->getKey())->setBookName(book);
	char error = fModule->popError();
	printf("Error %c\n",error);
	//ToDo return correct Error
	_UpdateBibleText();
	return B_OK;
	
}


status_t BibleTextDocument::SetChapter(int iChapter)
{
	((VerseKey*)fModule->getKey())->setChapter(iChapter);
	char error = fModule->popError();
	printf("Error %c\n",error);
	//ToDo return correct Error
	_UpdateBibleText();
	return B_OK;
}


status_t BibleTextDocument::SetVerse(int iVerse)
{
	((VerseKey*)fModule->getKey())->setVerse(iVerse);
	char error = fModule->popError();
	printf("Error %c\n",error);
	//ToDo return correct Error
	_UpdateBibleText();
	return B_OK;
}


status_t BibleTextDocument::SetKey(const char* iKey)
{
	VerseKey key = new VerseKey();
	key.setLocale(language.Code());
	key.setText(iKey);
	char error = fModule->setKey(key);
	printf("Error SetKey %c\n",error);
	//ToDo return correct Error
	_UpdateBibleText();
	return B_OK;
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
	_UpdateBibleText();
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
	_UpdateBibleText();
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
	_UpdateBibleText();
}


status_t BibleTextDocument::SetModule(const char* modulName)
{
	
	VerseKey	key;
	bool		setKey=false;
	if (fModule)
	{
		key = ((VerseKey *)fModule->getKey());
		setKey=true;
	}
	fModule	= fManager->getModule(modulName);
	if (fModule)
	{
		fModule->addRenderFilter(new GBFPlain());
		if (setKey)
			fModule->setKey(key);
	}	
	_UpdateBibleText();
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
	Remove(0,Length());
	ParagraphStyle bibleVerseStyle;
	bibleVerseStyle.SetJustify(true);
	Paragraph paragraph;
	VerseKey key = fModule->getKey();
	VerseKey oldKey = fModule->getKey();

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
		key.setVerse(1);
		fModule->setKey(key);
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
			key.setVerse(currentverse);
			fModule->setKey(key);
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
	fModule->setKey(oldKey);
	//trigger update...
	Insert(Length(),"");
}


void  BibleTextDocument::SetShowVerseNumbers(bool showVerseNumbers)
{
	if (fShowVerseNumbers != showVerseNumbers)
	{
		fShowVerseNumbers=showVerseNumbers;
		//trigger update...
		Insert(Length(),"");
	}
};
