/*
 * Sylpheed -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 1999-2007 Hiroyuki Yamamoto & the Claws Mail Team
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "defs.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <time.h>

#include "utils.h"
#include "folder.h"
#include "folderview.h"
#include "menu.h"
#include "account.h"
#include "alertpanel.h"
#include "foldersel.h"
#include "inputdialog.h"
#include "imap.h"
#include "inc.h"
#include "prefs_common.h"
#include "summaryview.h"

static void new_folder_cb(FolderView *folderview, guint action, GtkWidget *widget);
static void rename_folder_cb(FolderView *folderview, guint action, GtkWidget *widget);
static void move_folder_cb(FolderView *folderview, guint action, GtkWidget *widget);
static void delete_folder_cb(FolderView *folderview, guint action, GtkWidget *widget);
static void update_tree_cb(FolderView *folderview, guint action, GtkWidget *widget);
static void download_cb(FolderView *folderview, guint action, GtkWidget *widget);
static void sync_cb(FolderView *folderview, guint action, GtkWidget *widget);
static void subscribed_cb(FolderView *folderview, guint action, GtkWidget *widget);
static void subscribe_cb(FolderView *folderview, guint action, GtkWidget *widget);

static GtkItemFactoryEntry imap_popup_entries[] =
{
	{N_("/Create _new folder..."),	 NULL, new_folder_cb,    0, NULL},
	{"/---",			 NULL, NULL,             0, "<Separator>"},
	{N_("/_Rename folder..."),	 NULL, rename_folder_cb, 0, NULL},
	{N_("/M_ove folder..."),	 NULL, move_folder_cb,   0, NULL},
	{N_("/Cop_y folder..."),	 NULL, move_folder_cb,   1, NULL},
	{"/---",			 NULL, NULL,             0, "<Separator>"},
	{N_("/_Delete folder..."),	 NULL, delete_folder_cb, 0, NULL},
	{"/---",			 NULL, NULL,             0, "<Separator>"},
	{N_("/_Synchronise"),		 NULL, sync_cb,      	 0, NULL},
	{N_("/Down_load messages"),	 NULL, download_cb,      0, NULL},
	{"/---",			 NULL, NULL,             0, "<Separator>"},
	{N_("/S_ubscriptions"),		 NULL, NULL,		 0, "<Branch>"},
	{N_("/Subscriptions/Show only subscribed _folders"),    
					 NULL, subscribed_cb,  	 0, "<ToggleItem>"},
	{N_("/Subscriptions/---"),	 NULL, NULL,  		 0, "<Separator>"},
	{N_("/Subscriptions/_Subscribe..."),NULL, subscribe_cb,  	 1, NULL},
	{N_("/Subscriptions/_Unsubscribe..."),    
					 NULL, subscribe_cb, 	 0, NULL},
	{"/---",			 NULL, NULL,             0, "<Separator>"},
	{N_("/_Check for new messages"), NULL, update_tree_cb,   0, NULL},
	{N_("/C_heck for new folders"),	 NULL, update_tree_cb,   1, NULL},
	{N_("/R_ebuild folder tree"),	 NULL, update_tree_cb,   2, NULL},
	{"/---",			 NULL, NULL, 		 0, "<Separator>"},
};

static void set_sensitivity(GtkItemFactory *factory, FolderItem *item);

static FolderViewPopup imap_popup =
{
	"imap",
	"<IMAPFolder>",
	NULL,
	set_sensitivity
};

void imap_gtk_init(void)
{
	guint i, n_entries;

	n_entries = sizeof(imap_popup_entries) /
		sizeof(imap_popup_entries[0]);
	for (i = 0; i < n_entries; i++)
		imap_popup.entries = g_slist_append(imap_popup.entries, &imap_popup_entries[i]);

	folderview_register_popup(&imap_popup);
}

static void set_sensitivity(GtkItemFactory *factory, FolderItem *item)
{
	gboolean folder_is_normal = 
			item != NULL &&
			item->stype == F_NORMAL &&
			!folder_has_parent_of_type(item, F_OUTBOX) &&
			!folder_has_parent_of_type(item, F_DRAFT) &&
			!folder_has_parent_of_type(item, F_QUEUE) &&
			!folder_has_parent_of_type(item, F_TRASH);

#define SET_SENS(name, sens) \
	menu_set_sensitive(factory, name, sens)

	SET_SENS("/Create new folder...",   item->no_sub == FALSE);
	SET_SENS("/Rename folder...",       item->stype == F_NORMAL && folder_item_parent(item) != NULL);
	SET_SENS("/Move folder...", 	    folder_is_normal && folder_item_parent(item) != NULL);
	SET_SENS("/Delete folder...", 	    item->stype == F_NORMAL && folder_item_parent(item) != NULL);

	SET_SENS("/Synchronise",	    item ? (folder_item_parent(item) == NULL && folder_want_synchronise(item->folder))
						: FALSE);
	SET_SENS("/Check for new messages", folder_item_parent(item) == NULL);
	SET_SENS("/Check for new folders",  folder_item_parent(item) == NULL);
	SET_SENS("/Rebuild folder tree",    folder_item_parent(item) == NULL);
	
	SET_SENS("/Subscriptions/Unsubscribe...",    item->stype == F_NORMAL && folder_item_parent(item) != NULL);
	SET_SENS("/Subscriptions/Subscribe...",    TRUE);
	menu_set_active(factory, "/Subscriptions/Show only subscribed folders", item->folder->account->imap_subsonly);

#undef SET_SENS
}

static void new_folder_cb(FolderView *folderview, guint action,
		          GtkWidget *widget)
{
	GtkCTree *ctree = GTK_CTREE(folderview->ctree);
	FolderItem *item;
	FolderItem *new_item;
	gchar *new_folder;
	gchar *name;
	gchar *p;
	gchar separator = '/';
	
	if (!folderview->selected) return;

	item = gtk_ctree_node_get_row_data(ctree, folderview->selected);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->folder != NULL);
	g_return_if_fail(item->folder->account != NULL);

	new_folder = input_dialog
		(_("New folder"),
		 _("Input the name of new folder:\n"
		   "(if you want to create a folder to store subfolders\n"
		   "and no mails, append '/' at the end of the name)"),
		 _("NewFolder"));
	if (!new_folder) return;
	AUTORELEASE_STR(new_folder, {g_free(new_folder); return;});

	separator = imap_get_path_separator_for_item(item);

	p = strchr(new_folder, separator);
	if (p && *(p + 1) != '\0') {
		alertpanel_error(_("'%c' can't be included in folder name."),
				 separator);
		return;
	}
	p = strchr(new_folder, '/');
	if (p && *(p + 1) != '\0') {
		alertpanel_error(_("'%c' can't be included in folder name."),
				 '/');
		return;
	}

	name = trim_string(new_folder, 32);
	AUTORELEASE_STR(name, {g_free(name); return;});

	/* find whether the directory already exists */
	if (folder_find_child_item_by_name(item, new_folder)) {
		alertpanel_error(_("The folder '%s' already exists."), name);
		return;
	}

	new_item = folder_create_folder(item, new_folder);
	if (!new_item) {
		alertpanel_error(_("Can't create the folder '%s'."), name);
		return;
	}
	folder_write_list();
}

static void rename_folder_cb(FolderView *folderview, guint action,
			     GtkWidget *widget)
{
	FolderItem *item;
	gchar *new_folder;
	gchar *name;
	gchar *message;
	gchar *old_path;
	gchar *old_id;
	gchar *new_id;
	gchar *base;
	gchar separator = '/';

	item = folderview_get_selected_item(folderview);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->path != NULL);
	g_return_if_fail(item->folder != NULL);

	name = trim_string(item->name, 32);
	message = g_strdup_printf(_("Input new name for '%s':"), name);
	base = g_path_get_basename(item->path);
	new_folder = input_dialog(_("Rename folder"), message, base);
	g_free(base);
	g_free(message);
	g_free(name);
	if (!new_folder) return;
	AUTORELEASE_STR(new_folder, {g_free(new_folder); return;});

	separator = imap_get_path_separator_for_item(item);
	if (strchr(new_folder, separator) != NULL) {
		alertpanel_error(_("`%c' can't be included in folder name."),
				 separator);
		return;
	}
	if (strchr(new_folder, '/') != NULL) {
		alertpanel_error(_("`%c' can't be included in folder name."),
				 '/');
		return;
	}

	if (folder_find_child_item_by_name(folder_item_parent(item), new_folder)) {
		name = trim_string(new_folder, 32);
		alertpanel_error(_("The folder '%s' already exists."), name);
		g_free(name);
		return;
	}

	Xstrdup_a(old_path, item->path, {g_free(new_folder); return;});

	old_id = folder_item_get_identifier(item);
	
	if (folder_item_rename(item, new_folder) < 0) {
		alertpanel_error(_("The folder could not be renamed.\n"
				   "The new folder name is not allowed."));
		g_free(old_id);
		return;
	}

	new_id = folder_item_get_identifier(item);
	prefs_filtering_rename_path(old_id, new_id);
	account_rename_path(old_id, new_id);

	g_free(old_id);
	g_free(new_id);

	folder_item_prefs_save_config(item);
	folder_write_list();
}

static void move_folder_cb(FolderView *folderview, guint action, GtkWidget *widget)
{
	FolderItem *from_folder = NULL, *to_folder = NULL;

	from_folder = folderview_get_selected_item(folderview);
	if (!from_folder || from_folder->folder->klass != imap_get_class())
		return;

	to_folder = foldersel_folder_sel(from_folder->folder, FOLDER_SEL_MOVE, NULL);
	if (!to_folder)
		return;
	
	folderview_move_folder(folderview, from_folder, to_folder, action);
}

static void delete_folder_cb(FolderView *folderview, guint action,
			     GtkWidget *widget)
{
	GtkCTree *ctree = GTK_CTREE(folderview->ctree);
	FolderItem *item;
	gchar *message, *name;
	AlertValue avalue;
	gchar *old_path;
	gchar *old_id;

	if (!folderview->selected) return;

	item = gtk_ctree_node_get_row_data(ctree, folderview->selected);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->path != NULL);
	g_return_if_fail(item->folder != NULL);

	name = trim_string(item->name, 32);
	AUTORELEASE_STR(name, {g_free(name); return;});
	message = g_strdup_printf
		(_("All folders and messages under '%s' will be permanently deleted. "
		   "Recovery will not be possible.\n\n"
		   "Do you really want to delete?"), name);
	avalue = alertpanel_full(_("Delete folder"), message,
		 		 GTK_STOCK_CANCEL, GTK_STOCK_DELETE, NULL, FALSE,
				 NULL, ALERT_WARNING, G_ALERTDEFAULT);
	g_free(message);
	if (avalue != G_ALERTALTERNATE) return;

	Xstrdup_a(old_path, item->path, return);
	old_id = folder_item_get_identifier(item);

	if (folderview->opened == folderview->selected ||
	    gtk_ctree_is_ancestor(ctree,
				  folderview->selected,
				  folderview->opened)) {
		summary_clear_all(folderview->summaryview);
		folderview->opened = NULL;
	}

	if (item->folder->klass->remove_folder(item->folder, item) < 0) {
		folder_item_scan(item);
		alertpanel_error(_("Can't remove the folder '%s'."), name);
		g_free(old_id);
		return;
	}

	folder_write_list();

	prefs_filtering_delete_path(old_id);
	g_free(old_id);

}

static void update_tree_cb(FolderView *folderview, guint action,
			   GtkWidget *widget)
{
	FolderItem *item;

	item = folderview_get_selected_item(folderview);
	g_return_if_fail(item != NULL);

	summary_show(folderview->summaryview, NULL);

	g_return_if_fail(item->folder != NULL);

	if (action == 0)
		folderview_check_new(item->folder);
	else if (action == 1)
		folderview_rescan_tree(item->folder, FALSE);
	else if (action == 2)
		folderview_rescan_tree(item->folder, TRUE);
}

static void sync_cb(FolderView *folderview, guint action,
			   GtkWidget *widget)
{
	FolderItem *item;

	item = folderview_get_selected_item(folderview);
	g_return_if_fail(item != NULL);
	folder_synchronise(item->folder);
}

void imap_gtk_synchronise(FolderItem *item)
{
	MainWindow *mainwin = mainwindow_get_mainwindow();
	FolderView *folderview = mainwin->folderview;
	
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->folder != NULL);

	main_window_cursor_wait(mainwin);
	inc_lock();
	main_window_lock(mainwin);
	gtk_widget_set_sensitive(folderview->ctree, FALSE);
	main_window_progress_on(mainwin);
	GTK_EVENTS_FLUSH();
	if (item->no_select == FALSE && folder_item_fetch_all_msg(item) < 0) {
		gchar *name;

		name = trim_string(item->name, 32);
		alertpanel_error(_("Error occurred while downloading messages in '%s'."), name);
		g_free(name);
	}
	folder_set_ui_func(item->folder, NULL, NULL);
	main_window_progress_off(mainwin);
	gtk_widget_set_sensitive(folderview->ctree, TRUE);
	main_window_unlock(mainwin);
	inc_unlock();
	main_window_cursor_normal(mainwin);

}

static void chk_update_val(GtkWidget *widget, gpointer data)
{
        gboolean *val = (gboolean *)data;
	*val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static gboolean imap_gtk_subscribe_func(GNode *node, gpointer data)
{
	FolderItem *item = node->data;
	gboolean action = GPOINTER_TO_INT(data);
	
	if (item->path)
		imap_subscribe(item->folder, item, action);

	return FALSE;
}

static void subscribe_cb(FolderView *folderview, guint action,
			   GtkWidget *widget)
{
	GtkCTree *ctree = GTK_CTREE(folderview->ctree);
	FolderItem *item;
	gchar *message, *name;
	AlertValue avalue;
	GtkWidget *rec_chk;
	gboolean recurse = FALSE;

	if (!folderview->selected) return;

	item = gtk_ctree_node_get_row_data(ctree, folderview->selected);
	g_return_if_fail(item != NULL);
	g_return_if_fail(item->folder != NULL);

	name = trim_string(item->name, 32);
	AUTORELEASE_STR(name, {g_free(name); return;});
	
	if (action && item->folder->account->imap_subsonly) {
		alertpanel_notice(_("This folder is already subscribed to. To "
				  "subscribe to other folders, you must first "
				  "disable \"Show subscribed folders only\".\n"
				  "\n"
				  "Alternatively, you can use \"Create new folder\" "
				  "to subscribe to a known folder."));
		return;
	}
	message = g_strdup_printf
		(_("Do you want to %s the '%s' folder?"),
		   action?_("subscribe"):_("unsubscribe"), name);
	
	rec_chk = gtk_check_button_new_with_label(_("Apply to subfolders"));
	
	g_signal_connect(G_OBJECT(rec_chk), "toggled", 
			G_CALLBACK(chk_update_val), &recurse);

	avalue = alertpanel_full(_("Subscriptions"), message,
		 		 GTK_STOCK_CANCEL, action?_("Subscribe"):_("Unsubscribe"), NULL, FALSE,
				 rec_chk, ALERT_QUESTION, G_ALERTDEFAULT);
	g_free(message);
	if (avalue != G_ALERTALTERNATE) return;
	
	if (recurse) {
		g_node_traverse(item->node, G_PRE_ORDER,
			G_TRAVERSE_ALL, -1, imap_gtk_subscribe_func, GINT_TO_POINTER(action));
	} else {
		imap_subscribe(item->folder, item, action);
	}

	if (!action && item->folder->account->imap_subsonly)
		folderview_rescan_tree(item->folder, FALSE);
}

static void subscribed_cb(FolderView *folderview, guint action,
			   GtkWidget *widget)
{
	GtkCTree *ctree = GTK_CTREE(folderview->ctree);
	FolderItem *item = gtk_ctree_node_get_row_data(ctree, folderview->selected);
	
	if (!item || !item->folder || !item->folder->account)
		return;
	if (item->folder->account->imap_subsonly == GTK_CHECK_MENU_ITEM(widget)->active)
		return;

	item->folder->account->imap_subsonly = GTK_CHECK_MENU_ITEM(widget)->active;
	folderview_rescan_tree(item->folder, FALSE);
}

static void download_cb(FolderView *folderview, guint action,
			GtkWidget *widget)
{
	GtkCTree *ctree = GTK_CTREE(folderview->ctree);
	FolderItem *item;

	if (!folderview->selected) return;

	item = gtk_ctree_node_get_row_data(ctree, folderview->selected);
	imap_gtk_synchronise(item);
}
