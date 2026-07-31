## Haiku Generic Makefile v2.6 ##

# Standalone regression test binary for the parallel Bible view engine.
# Run directly (no window, prints PASS/FAIL per check):
#   ./Tests

NAME = Tests
TARGET_DIR = .

TYPE = APP

APP_MIME_SIG = application/x-vnd.ScriptureGuide-Tests

SRCS = \
	 Tests.cpp  \
	 \
	 ../parallelbible/BibleTextDocument.cpp  \
	 ../parallelbible/ParallelBibleView.cpp  \
	 ../parallelbible/PersonalNotesModule.cpp  \
	 ../parallelbible/VerseAligner.cpp  \
	 ../SwordBackend.cpp  \
	 \
	 ../textview/Bullet.cpp  \
	 ../textview/BulletData.cpp  \
	 ../textview/CharacterStyle.cpp  \
	 ../textview/CharacterStyleData.cpp  \
	 ../textview/MarkupParser.cpp  \
	 ../textview/Paragraph.cpp  \
	 ../textview/ParagraphLayout.cpp  \
	 ../textview/ParagraphStyle.cpp  \
	 ../textview/ParagraphStyleData.cpp  \
	 ../textview/TextDocument.cpp  \
	 ../textview/TextDocumentLayout.cpp  \
	 ../textview/TextDocumentView.cpp  \
	 ../textview/TextEditor.cpp  \
	 ../textview/TextListener.cpp  \
	 ../textview/TextSelection.cpp  \
	 ../textview/TextSpan.cpp  \
	 ../textview/UndoableEditListener.cpp  \
	 \
	 ../textview_support/support.cpp

RDEFS =

RSRCS =

LIBS = be $(STDCPPLIBS) sword z shared localestub

LIBPATHS =

MACHINE=$(shell uname -m)
ifeq ($(MACHINE), BePC)
	SYSTEM_INCLUDE_PATHS =  /system/develop/headers/x86/sword \
							/system/develop/headers/private/interface \
							/boot/system/develop/headers/private/shared \
							/boot/system/develop/headers/private/interface
else
	SYSTEM_INCLUDE_PATHS =  /system/develop/headers/sword \
							/system/develop/headers/private/interface \
							/boot/system/develop/headers/private/shared \
							/boot/system/develop/headers/private/interface
endif

LOCAL_INCLUDE_PATHS = . .. ../parallelbible ../textview ../textview_support

OPTIMIZE := FULL

DEFINES = DEBUG=1

WARNINGS = ALL

SYMBOLS := TRUE

DEBUGGER := TRUE

COMPILER_FLAGS =

LINKER_FLAGS =

APP_VERSION :=

DRIVER_PATH =

## Include the Makefile-Engine
DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine
