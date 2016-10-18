/*
 * Claws Mail -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 1999-2015 the Claws Mail Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include <glib.h>
#include <glib/gi18n.h>


#include "common/claws.h"
#include "common/version.h"
#include "plugin.h"
#include "common/utils.h"
#include "hooks.h"
#include "procmsg.h"
#include "folder.h"


#define PLUGIN_NAME (_("Spam MoveToJunk"))

int spammovetojunk_fakelearn(MsgInfo *msginfo, GSList *msglist, gboolean spam)
{
	if (msginfo == NULL && msglist == NULL) {
		return -1;
	}
	return 0;
}

FolderItem *spammovetojunk_get_spam_folder(MsgInfo *msginfo)
{
	FolderItem *item = NULL;

	if (msginfo == NULL || msginfo->folder == NULL)
		return NULL;

	if (msginfo->folder->folder &&
	    msginfo->folder->folder->junk)
		item = msginfo->folder->folder->junk;

	if (item == NULL && 
	    msginfo->folder->folder &&
	    msginfo->folder->folder->trash)
		item = msginfo->folder->folder->trash;
		
	if (item == NULL)
		item = folder_get_default_trash();
		
	debug_print("spam dir: %s\n", folder_item_get_path(item));

	return item;
}


gint plugin_init(gchar **error)
{
	if (!check_plugin_version(MAKE_NUMERIC_VERSION(3,9,2,0),
				VERSION_NUMERIC, PLUGIN_NAME, error))
		return -1;

	procmsg_register_spam_learner(spammovetojunk_fakelearn);
	procmsg_spam_set_folder("Junk", spammovetojunk_get_spam_folder);

	debug_print("SpamMoveToJunk plugin loaded\n");

	return 0;
}

gboolean plugin_done(void)
{
	procmsg_unregister_spam_learner(spammovetojunk_fakelearn);
	procmsg_spam_set_folder(NULL, NULL);

	debug_print("SpamMoveToJunk plugin unloaded\n");
	return TRUE;
}

const gchar *plugin_name(void)
{
	return PLUGIN_NAME;
}

const gchar *plugin_desc(void)
{
	return _("This plugin move a message you mark as spam in your Junk folder."
	         "\n\n"
	         "It is only useful when your antispam system is running "
	         "on the IMAP server, watching every move in this folder.");
}

const gchar *plugin_type(void)
{
	return "Common";
}

const gchar *plugin_licence(void)
{
	return "GPL3+";
}

const gchar *plugin_version(void)
{
	return VERSION;
}

struct PluginFeature *plugin_provides(void)
{
	static struct PluginFeature features[] = 
		{ {PLUGIN_UTILITY, N_("Spam Move")},
		  {PLUGIN_NOTHING, NULL}};
	return features;
}
