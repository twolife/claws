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

#ifndef __SEND_MESSAGE_H__
#define __SEND_MESSAGE_H__

#include <glib.h>

#include "prefs_account.h"

#define SMTP_PORT	25
#if USE_OPENSSL
#define SSMTP_PORT	465
#endif

gint send_message		(const gchar	*file,
				 PrefsAccount	*ac_prefs,
				 GSList		*to_list);
gint send_message_local		(const gchar *command,
				 FILE *fp);
gint send_message_smtp		(PrefsAccount *ac_prefs,
				 GSList *to_list,
				 FILE *fp);
gint send_message_smtp_full	(PrefsAccount *ac_prefs, 
				 GSList *to_list, 
				 FILE *fp, 
				 gboolean keep_session);

#endif /* __SEND_H__ */
