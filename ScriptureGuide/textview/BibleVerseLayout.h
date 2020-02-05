/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_VERSE_LAYOUT_H
#define BIBLE_VERSE_LAYOUT_H

#include <versekey.h>

#include <SupportDefs.h>

#include "ParagraphLayout.h"

class BibleVerseLayout : public ParagraphLayout {
public:
	
	BibleVerseLayout();
	
	float		Height();
	float		SetHeight(float height);	

private:
	float		fHeight;
};


#endif // BIBLE_VERSE_LAYOUT_H
