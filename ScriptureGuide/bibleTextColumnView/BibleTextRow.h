/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef BIBLE_TEXT_ROW_H
#define BIBLE_TEXT_ROW_H

#include <ColumnListView.h>

#include <SupportDefs.h>


class BibleTextRow : public BRow {
public:
				BibleTextRow();
				BibleTextRow(float height);

	float 		Height() const;
	

};


#endif // _H
