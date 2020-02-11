/*
 * Copyright 2020, Paradoxianer <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_TEXT_FIELD_H
#define BIBLE_TEXT_FIELD_H


#include <SupportDefs.h>


class BibleTextField{
public:
								BibleTextField(const char* key);

			void				SetString(const char* string);
			const char*			String() const;
			void				SetClippedString(const char* string);
			bool				HasClippedString() const;
			const char*			ClippedString();
			void				SetWidth(float);
			float				Width();

private:
			VerseKey			key;
};


#endif // BIBLE_TEXT_FIELD_H
