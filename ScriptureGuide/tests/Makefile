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
	 ../parallelbible/BookmarkFile.cpp  \
	 ../parallelbible/HighlightStore.cpp  \
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

# Deliberately NOT TRUE, and not about debugging at all: the engine
# derives the object directory name from it --
#   OBJ_DIR := objects.$(CPU)-$(CC)$(CC_VER)-$(if $(DEBUGGER),debug,release)
# -- and the application next door builds with DEBUGGER := TRUE. With
# both using the same name, and the application's source directories on
# this makefile's own VPATH, make resolves "objects.../Foo.o" to the
# APPLICATION's copy: building the tests wrote its objects into ../, and
# the tests then linked whatever happened to be lying around here.
#
# Measured, not assumed: touching ../textview/ParagraphLayout.cpp and
# running make here recompiled it into ../objects.../ParagraphLayout.o
# while the copy in this directory kept its hours-old timestamp. The
# practical damage was worse than a stale binary -- a test once passed
# while measuring a version of the code whose behaviour had been
# disabled, which is a test proving nothing at all.
#
# Must be EMPTY, not FALSE: $(if $(DEBUGGER),debug,release) tests for a
# non-empty string, so "FALSE" would still select "debug" and change
# nothing.
#
# The flags DEBUGGER would have set are in COMPILER_FLAGS above instead,
# so nothing is lost but the name collision.
DEBUGGER :=

# -g -O0 explicitly rather than via DEBUGGER below -- see there for why
# that switch has to stay off.
COMPILER_FLAGS = -g -O0

LINKER_FLAGS =

APP_VERSION :=

DRIVER_PATH =

## Include the Makefile-Engine
DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine
