/*
 * Copyright 2017 Patrick O. Perry.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <stdio.h>
#include "private/graphbreak.h"
#include "utf8lite.h"


#define NEXT() \
	do { \
		scan->ptr = scan->iter.ptr; \
		if (utf8lite_text_iter_advance(&scan->iter)) { \
			scan->prop = graph_break(scan->iter.current); \
		} else { \
			scan->prop = -1; \
		} \
	} while (0)


void utf8lite_graphscan_make(struct utf8lite_graphscan *scan,
			     const struct utf8lite_text *text)
{
	utf8lite_text_iter_make(&scan->iter, text);
	utf8lite_graphscan_reset(scan);
}


void utf8lite_graphscan_reset(struct utf8lite_graphscan *scan)
{
	utf8lite_text_iter_reset(&scan->iter);
	scan->current.text.ptr = (uint8_t *)scan->iter.ptr;
	scan->current.text.attr = (scan->iter.text_attr
				   & ~UTF8LITE_TEXT_SIZE_MASK);
	NEXT();
}


void utf8lite_graphscan_skip(struct utf8lite_graphscan *scan)
{
	utf8lite_text_iter_skip(&scan->iter);
	scan->current.text.ptr = (uint8_t *)scan->iter.ptr;
	scan->current.text.attr = (scan->iter.text_attr
				   & ~UTF8LITE_TEXT_SIZE_MASK);
	scan->prop = -1;
}


int utf8lite_graphscan_advance(struct utf8lite_graphscan *scan)
{
	scan->current.text.ptr = (uint8_t *)scan->ptr;
	scan->current.text.attr = (scan->iter.text_attr
				   & ~UTF8LITE_TEXT_SIZE_MASK);
Start:
	// GB2: Break at the end of text
	if (scan->prop < 0) {
		goto Break;
	}

	switch ((enum graph_break_prop)scan->prop) {
	case GRAPH_BREAK_CR:
		NEXT();
		goto CR;

	case GRAPH_BREAK_CONTROL:
	case GRAPH_BREAK_LF:
		// Break after controls
		// GB4: (Newline | LF) +
		NEXT();
		goto Break;

	case GRAPH_BREAK_L:
		NEXT();
		goto L;

	case GRAPH_BREAK_LV:
	case GRAPH_BREAK_V:
		NEXT();
		goto V;

	case GRAPH_BREAK_LVT:
	case GRAPH_BREAK_T:
		NEXT();
		goto T;

	case GRAPH_BREAK_PREPEND:
		NEXT();
		goto Prepend;

	case GRAPH_BREAK_INCB_CONSONANT:
		NEXT();
		goto InCB_Consonant;

	case GRAPH_BREAK_EXTENDED_PICTOGRAPHIC:
		NEXT();
		goto Extended_Pictographic;

	case GRAPH_BREAK_REGIONAL_INDICATOR:
		NEXT();
		goto Regional_Indicator;

	case GRAPH_BREAK_EXTEND_OTHER:
	case GRAPH_BREAK_EXTEND_INCB_EXTEND:
	case GRAPH_BREAK_EXTEND_INCB_LINKER:
	case GRAPH_BREAK_SPACINGMARK:
	case GRAPH_BREAK_ZWJ:
	case GRAPH_BREAK_OTHER:
		NEXT();
		goto MaybeBreak;
	}

	assert(0 && "unhandled grapheme break property");

CR:
	// GB3: Do not break within CRLF
	// GB4: Otherwise break after controls
	if (scan->prop == GRAPH_BREAK_LF) {
		NEXT();
	}
	goto Break;

L:
	// GB6: Do not break Hangul syllable sequences.
	switch (scan->prop) {
	case GRAPH_BREAK_L:
		NEXT();
		goto L;

	case GRAPH_BREAK_V:
	case GRAPH_BREAK_LV:
		NEXT();
		goto V;

	case GRAPH_BREAK_LVT:
		NEXT();
		goto T;

	default:
		goto MaybeBreak;
	}

V:
	// GB7: Do not break Hangul syllable sequences.
	switch (scan->prop) {
	case GRAPH_BREAK_V:
		NEXT();
		goto V;

	case GRAPH_BREAK_T:
		NEXT();
		goto T;

	default:
		goto MaybeBreak;
	}

T:
	// GB8: Do not break Hangul syllable sequences.
	switch (scan->prop) {
	case GRAPH_BREAK_T:
		NEXT();
		goto T;

	default:
		goto MaybeBreak;
	}

Prepend:
	switch (scan->prop) {
	case GRAPH_BREAK_CONTROL:
	case GRAPH_BREAK_CR:
	case GRAPH_BREAK_LF:
		// GB5: break before controls
		goto Break;

	default:
		// GB9b: do not break after Prepend characters.
		goto Start;
	}

InCB_Consonant:
	// GB0c:  Do not break within certain combinations with Indic_Conjunct_Break (InCB)=Linker.
	while (
		scan->prop == GRAPH_BREAK_EXTEND_INCB_EXTEND ||
		scan->prop == GRAPH_BREAK_ZWJ) {
		NEXT();
	}
	if (scan-> prop == GRAPH_BREAK_EXTEND_INCB_LINKER) {
		NEXT();
		goto InCB_Consonant_Linker;
	}
	goto MaybeBreak;

InCB_Consonant_Linker:
	// GB0c:  Do not break within certain combinations with Indic_Conjunct_Break (InCB)=Linker.
	while (
		scan->prop == GRAPH_BREAK_EXTEND_INCB_EXTEND ||
		scan->prop == GRAPH_BREAK_EXTEND_INCB_LINKER ||
		scan->prop == GRAPH_BREAK_ZWJ) {
		NEXT();
	}
	if (scan->prop == GRAPH_BREAK_INCB_CONSONANT) {
		NEXT();
		goto InCB_Consonant;
	}
	goto MaybeBreak;

Extended_Pictographic:
	// GB9:  Do not break before extending characters
	while (
		scan->prop == GRAPH_BREAK_EXTEND_OTHER ||
		scan->prop == GRAPH_BREAK_EXTEND_INCB_EXTEND ||
		scan->prop == GRAPH_BREAK_EXTEND_INCB_LINKER) {
		NEXT();
	}

	// GB9: Do not break before ZWJ
	if (scan->prop == GRAPH_BREAK_ZWJ) {
		NEXT();
		// GB11: Do not break within emoji modifier sequences
		// or emoji zwj sequences.
		if (scan->prop == GRAPH_BREAK_EXTENDED_PICTOGRAPHIC) {
			NEXT();
			goto Extended_Pictographic;
		}
	}
	goto MaybeBreak;

Regional_Indicator:
	// Do not break within emoji flag sequences. That is, do not break
	// between regional indicator (RI) symbols if there is an odd number
	// of RI characters before the break point
	if (scan->prop == GRAPH_BREAK_REGIONAL_INDICATOR) {
		// GB12/13: [^RI] RI * RI
		NEXT();
	}
	goto MaybeBreak;

MaybeBreak:
	// GB9: Do not break before extending characters or ZWJ.
	// GB9a: Do not break before SpacingMark [extended grapheme clusters]
	// GB999: Otherwise, break everywhere
	switch (scan->prop) {
	case GRAPH_BREAK_EXTEND_OTHER:
	case GRAPH_BREAK_EXTEND_INCB_EXTEND:
	case GRAPH_BREAK_EXTEND_INCB_LINKER:
	case GRAPH_BREAK_SPACINGMARK:
	case GRAPH_BREAK_ZWJ:
		NEXT();
		goto MaybeBreak;

	default:
		goto Break;
	}

Break:
	scan->current.text.attr |= (size_t)(scan->ptr - scan->current.text.ptr);
	return (scan->ptr == scan->current.text.ptr) ? 0 : 1;
}

#define PREV() \
	do { \
		if (utf8lite_text_iter_retreat(&prev)) { \
			prop = graph_break(prev.current); \
		} else { \
			prop = -1; \
		} \
	} while (0)


static int regional_indicator_odd(const struct utf8lite_text_iter *prev)
{
	struct utf8lite_text_iter it = *prev;
	int odd = 1, prop;

	while (utf8lite_text_iter_retreat(&it)) {
		prop = graph_break(it.current);
		if (prop == GRAPH_BREAK_REGIONAL_INDICATOR) {
			odd = odd ? 0 : 1;
		} else {
			return odd;
		}
	}

	return odd;
}


static int follows_extended_pictographic(const struct utf8lite_text_iter *prev)
{
	struct utf8lite_text_iter it = *prev;
	int prop;

	while (utf8lite_text_iter_retreat(&it)) {
		prop = graph_break(it.current);
		switch (prop) {
		case GRAPH_BREAK_EXTENDED_PICTOGRAPHIC:
			return 1;
		case GRAPH_BREAK_EXTEND_OTHER:
		case GRAPH_BREAK_EXTEND_INCB_EXTEND:
		case GRAPH_BREAK_EXTEND_INCB_LINKER:
			break;
		default:
			return 0;
		}
	}

	return 0;
}

static int follows_incb_consonant(int prop, const struct utf8lite_text_iter *prev)
{
	struct utf8lite_text_iter it = *prev;
	int first = 1;

	while (first || utf8lite_text_iter_retreat(&it)) {
		if (!first) {
			prop = graph_break(it.current);
		}
		first = 0;
		switch (prop) {
		case GRAPH_BREAK_EXTEND_INCB_LINKER:
			goto Linker;
		case GRAPH_BREAK_ZWJ:
		case GRAPH_BREAK_EXTEND_INCB_EXTEND:
			break;
		default:
			return 0;
		}
	}

	return 0;

Linker:
	while (utf8lite_text_iter_retreat(&it)) {
		prop = graph_break(it.current);
		switch (prop) {
		case GRAPH_BREAK_INCB_CONSONANT:
			return 1;
		case GRAPH_BREAK_ZWJ:
		case GRAPH_BREAK_EXTEND_INCB_EXTEND:
		case GRAPH_BREAK_EXTEND_INCB_LINKER:
			break;
		default:
			return 0;
		}
	}

	return 0;
}

int utf8lite_graphscan_retreat(struct utf8lite_graphscan *scan)
{
	struct utf8lite_text_iter prev;
	int prop;

	// see if there is a previous character
	prev = scan->iter;
	if (!utf8lite_text_iter_retreat(&prev)) {
		// already at the start
		return 0;
	}

	// if so, start of current grapheme becomes end of previous
	scan->current.text.attr = (scan->iter.text_attr
				   & ~UTF8LITE_TEXT_SIZE_MASK);
	scan->ptr = scan->current.text.ptr;

	// position iter after the last character, prev before
	while (prev.ptr != scan->ptr) {
		scan->iter = prev;
		utf8lite_text_iter_retreat(&prev);
	}

	// update iterator property
	if (scan->iter.current < 0) {
		scan->prop = -1;
	} else {
		scan->prop = graph_break(scan->iter.current);
	}

	if (prev.current < 0) {
		prop = -1;
	} else {
		prop = graph_break(prev.current);
	}

Start:
	// at the start of the text
	if (prop < 0) {
		goto Break;
	}

	switch ((enum graph_break_prop)prop) {
	case GRAPH_BREAK_CONTROL:
	case GRAPH_BREAK_CR:
		// GB4: Break after controls
		PREV();
		goto Break;

	case GRAPH_BREAK_LF:
		PREV();
		goto LF;

	case GRAPH_BREAK_L:
	case GRAPH_BREAK_LV:
	case GRAPH_BREAK_LVT:
		PREV();
		goto L;

	case GRAPH_BREAK_V:
		PREV();
		goto V;

	case GRAPH_BREAK_T:
		PREV();
		goto T;

	case GRAPH_BREAK_EXTEND_OTHER:
	case GRAPH_BREAK_EXTEND_INCB_EXTEND:
	case GRAPH_BREAK_EXTEND_INCB_LINKER:
	case GRAPH_BREAK_SPACINGMARK:
	case GRAPH_BREAK_ZWJ:
		PREV();
		goto Extend;

	case GRAPH_BREAK_EXTENDED_PICTOGRAPHIC:
		PREV();
		goto Extended_Pictographic;

	case GRAPH_BREAK_INCB_CONSONANT:
		PREV();
		goto InCB_Consonant;

	case GRAPH_BREAK_REGIONAL_INDICATOR:
		PREV();
		goto Regional_Indicator;

	case GRAPH_BREAK_PREPEND:
	case GRAPH_BREAK_OTHER:
		PREV();
		goto MaybeBreak;
	}
	assert(0 && "unhandled graph break property");

LF:
	// GB3: Do not break between a CR and LF
	// GB4: Otherwise, break after controls.
	if (prop == GRAPH_BREAK_CR) {
		PREV();
	}
	goto Break;

L:
	// GB6: Do not break Hangul syllable sequences
	switch (prop) {
	case GRAPH_BREAK_L:
		PREV();
		goto L;

	default:
		goto MaybeBreak;
	}

V:
	// GB6, GB7: Do not break Hangul syllable sequences
	switch (prop) {
	case GRAPH_BREAK_V:
		PREV();
		goto V;

	case GRAPH_BREAK_L:
	case GRAPH_BREAK_LV:
		PREV();
		goto L;

	default:
		goto MaybeBreak;
	}

T:
	// GB6, GB7, GB8: Do not break Hangul syllable sequences
	switch (prop) {
	case GRAPH_BREAK_LV:
	case GRAPH_BREAK_LVT:
		PREV();
		goto L;

	case GRAPH_BREAK_V:
		PREV();
		goto V;

	case GRAPH_BREAK_T:
		PREV();
		goto T;

	default:
		goto MaybeBreak;
	}

Extend:
	switch (prop) {
	// GB4: Break after controls
	case GRAPH_BREAK_CONTROL:
	case GRAPH_BREAK_CR:
	case GRAPH_BREAK_LF:
		goto Break;

	// GB9: Do not break before extending characters or ZWJ.
	// GB9a: Do not break before SpacingMarks
	default:
		goto Start;
	}

InCB_Consonant:
	// GB0c:  Do not break within certain combinations with Indic_Conjunct_Break (InCB)=Linker.
	if (follows_incb_consonant(prop, &prev)) {
		while (prop != GRAPH_BREAK_INCB_CONSONANT) {
			PREV();
		}
		PREV();
		goto InCB_Consonant;
	}
	goto MaybeBreak;

Extended_Pictographic:
	// GB11: Do not break within emoji modifier sequences or
	// emoji zwj sequences.
	if (prop == GRAPH_BREAK_ZWJ && follows_extended_pictographic(&prev)) {
		PREV(); // ZWJ
		while (
			prop == GRAPH_BREAK_EXTEND_OTHER ||
			prop == GRAPH_BREAK_EXTEND_INCB_EXTEND ||
			prop == GRAPH_BREAK_EXTEND_INCB_LINKER) { // Extend*
			PREV();
		}

		PREV();
		goto Extended_Pictographic;
	}
	goto MaybeBreak;

Regional_Indicator:
	// GB12, GB13: Do not break within emoji flag sequences
	if (prop == GRAPH_BREAK_REGIONAL_INDICATOR) {
		if (regional_indicator_odd(&prev)) {
			PREV();
		}
	}
	goto MaybeBreak;

MaybeBreak:
	switch (prop) {
	// GB9b: Do not break after Prepend characters
	case GRAPH_BREAK_PREPEND:
		PREV();
		goto MaybeBreak;

	default:
		goto Break;
	}

Break:
	scan->current.text.ptr = (uint8_t *)prev.ptr;
	scan->current.text.attr |= (size_t)(scan->ptr - scan->current.text.ptr);
	return (scan->ptr == scan->current.text.ptr) ? 0 : 1;
}
