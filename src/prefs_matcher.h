/*
 * Sylpheed -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 1999-2006 Hiroyuki Yamamoto and the Claws Mail team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __PREFS_MATCHER_H__
#define __PREFS_MATCHER_H__

#include "matcher.h"

typedef void PrefsMatcherSignal	(MatcherList *matchers);

void prefs_matcher_open		(MatcherList *matchers,
				 PrefsMatcherSignal *cb);
void prefs_matcher_test_info	(void);
void prefs_matcher_addressbook_select	(void);

#endif /* __PREFS_FILTER_H__ */
