/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef SG_DEBUG_H
#define SG_DEBUG_H

#include <stdio.h>
#include <stdlib.h>


// Diagnostic logging for the parallel-column engine, silent unless the
// SG_DEBUG environment variable is set:
//
//     SG_DEBUG=1 ./ScriptureGuide
//
// The lines these produce ([SG] for state, [SG-PERF] for timings) have
// repeatedly been what actually located a bug here -- the notes column's
// scrollbar range, the VerseAligner rebuild storm, a chain measuring
// itself at the wrong width -- so they are kept rather than deleted. But
// they are verbose enough (roughly 900 lines from a few minutes of
// ordinary use) that leaving them on by default fills the syslog of an
// app most users start from Tracker, where stderr is not a terminal
// anyone is watching.
//
// Deliberately a macro, not a function: the arguments frequently call
// something to produce the value being logged (paragraph counts, chain
// heights), and those calls should not happen at all when logging is off.
//
// Only this project's own diagnostics go through here. The vendored
// textview library's fprintf()s are genuine error reports (allocation
// failures), not diagnostics, and stay unconditional.
static inline bool
SGDebugEnabled()
{
	// Read once, on first use. Function-local statics are initialized
	// thread-safely, and the environment does not change under us.
	static const bool enabled = getenv("SG_DEBUG") != NULL;
	return enabled;
}


#define SG_LOG(...)										\
	do {												\
		if (SGDebugEnabled())							\
			fprintf(stderr, __VA_ARGS__);				\
	} while (0)


#endif // SG_DEBUG_H
