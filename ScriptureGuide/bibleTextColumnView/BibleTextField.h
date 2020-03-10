/*
 * Copyright 2020, Paradoxianer <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_TEXT_FIELD_H
#define BIBLE_TEXT_FIELD_H

#include <ColumnListView.h>
#include <SupportDefs.h>

#include <versekey.h>

using namespace sword;


class BibleTextField : public BField
{
public:
								BibleTextField(VerseKey *key);

			void				SetKey(VerseKey *key);
			VerseKey*			Key(){return fKey;};
			void				SetWidth(float);
			float				Width();
			float				HeightForWidth();


private:
			VerseKey			*fKey;
};


#endif // BIBLE_TEXT_FIELD_H
