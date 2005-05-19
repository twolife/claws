/*
 * Sylpheed -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 1999-2003 Hiroyuki Yamamoto
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkhbbox.h>
#include <gtk/gtkstock.h>

#include "inputdialog.h"
#include "manage_window.h"
#include "gtkutils.h"
#include "utils.h"

#define INPUT_DIALOG_WIDTH	420

typedef enum
{
	INPUT_DIALOG_NORMAL,
	INPUT_DIALOG_INVISIBLE,
	INPUT_DIALOG_COMBO
} InputDialogType;

static gboolean ack;
static gboolean fin;

static InputDialogType type;

static GtkWidget *dialog;
static GtkWidget *msg_title;
static GtkWidget *msg_label;
static GtkWidget *entry;
static GtkWidget *combo;
static GtkWidget *ok_button;

static void input_dialog_create	(void);
static gchar *input_dialog_open	(const gchar	*title,
				 const gchar	*message,
				 const gchar	*default_string);
static void input_dialog_set	(const gchar	*title,
				 const gchar	*message,
				 const gchar	*default_string);

static void ok_clicked		(GtkWidget	*widget,
				 gpointer	 data);
static void cancel_clicked	(GtkWidget	*widget,
				 gpointer	 data);
static gint delete_event	(GtkWidget	*widget,
				 GdkEventAny	*event,
				 gpointer	 data);
static gboolean key_pressed	(GtkWidget	*widget,
				 GdkEventKey	*event,
				 gpointer	 data);
static void entry_activated	(GtkEditable	*editable);
static void combo_activated	(GtkEditable	*editable);


gchar *input_dialog(const gchar *title, const gchar *message,
		    const gchar *default_string)
{
	if (dialog && GTK_WIDGET_VISIBLE(dialog)) return NULL;

	if (!dialog)
		input_dialog_create();

	type = INPUT_DIALOG_NORMAL;
	gtk_widget_hide(combo);
	gtk_widget_show(entry);
	gtk_entry_set_visibility(GTK_ENTRY(entry), TRUE);

	return input_dialog_open(title, message, default_string);
}

gchar *input_dialog_with_invisible(const gchar *title, const gchar *message,
				   const gchar *default_string)
{
	if (dialog && GTK_WIDGET_VISIBLE(dialog)) return NULL;

	if (!dialog)
		input_dialog_create();

	type = INPUT_DIALOG_INVISIBLE;
	gtk_widget_hide(combo);
	gtk_widget_show(entry);
	gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

	return input_dialog_open(title, message, default_string);
}

gchar *input_dialog_combo(const gchar *title, const gchar *message,
			  const gchar *default_string, GList *list,
			  gboolean case_sensitive)
{
	if (dialog && GTK_WIDGET_VISIBLE(dialog)) return NULL;

	if (!dialog)
		input_dialog_create();

	type = INPUT_DIALOG_COMBO;
	gtk_widget_hide(entry);
	gtk_widget_show(combo);

	if (!list) {
		GList empty_list;

		empty_list.data = (gpointer)"";
		empty_list.next = NULL;
		empty_list.prev = NULL;
		gtk_combo_set_popdown_strings(GTK_COMBO(combo), &empty_list);
	} else
		gtk_combo_set_popdown_strings(GTK_COMBO(combo), list);

	gtk_combo_set_case_sensitive(GTK_COMBO(combo), case_sensitive);

	return input_dialog_open(title, message, default_string);
}

gchar *input_dialog_query_password(const gchar *server, const gchar *user)
{
	gchar *message;
	gchar *pass;

	message = g_strdup_printf(_("Input password for %s on %s:"),
				  user, server);
	pass = input_dialog_with_invisible(_("Input password"), message, NULL);
	g_free(message);

	return pass;
}

static void input_dialog_create(void)
{
	static PangoFontDescription *font_desc;
	GtkWidget *w_hbox;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *confirm_area;
	GtkWidget *icon;

	dialog = gtk_dialog_new();

	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
	gtk_window_set_title(GTK_WINDOW(dialog), "");
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

	g_signal_connect(G_OBJECT(dialog), "delete_event",
			 G_CALLBACK(delete_event), NULL);
	g_signal_connect(G_OBJECT(dialog), "key_press_event",
			 G_CALLBACK(key_pressed), NULL);
	MANAGE_WINDOW_SIGNALS_CONNECT(dialog);

	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 14);
	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox,
			    FALSE, FALSE, 0);

	/* for title label */
	w_hbox = gtk_hbox_new(FALSE, 0);
	
	icon = gtk_image_new_from_stock(GTK_STOCK_DIALOG_QUESTION,
        				GTK_ICON_SIZE_DIALOG); 
	gtk_misc_set_alignment (GTK_MISC (icon), 0.5, 0.0);
	gtk_box_pack_start (GTK_BOX (hbox), icon, FALSE, FALSE, 0);
	
	vbox = gtk_vbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
	gtk_widget_show (vbox);
	
	msg_title = gtk_label_new("");
	gtk_misc_set_alignment(GTK_MISC(msg_title), 0, 0.5);
	gtk_label_set_justify(GTK_LABEL(msg_title), GTK_JUSTIFY_LEFT);
	gtk_label_set_use_markup (GTK_LABEL (msg_title), TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), msg_title, FALSE, FALSE, 0);
	gtk_label_set_line_wrap(GTK_LABEL(msg_title), TRUE);
	if (!font_desc) {
		gint size;

		size = pango_font_description_get_size
			(msg_title->style->font_desc);
		font_desc = pango_font_description_new();
		pango_font_description_set_weight
			(font_desc, PANGO_WEIGHT_BOLD);
		pango_font_description_set_size
			(font_desc, size * PANGO_SCALE_LARGE);
	}
	if (font_desc)
		gtk_widget_modify_font(msg_title, font_desc);
	
	msg_label = gtk_label_new("");
	gtk_misc_set_alignment(GTK_MISC(msg_label), 0, 0.5);
	gtk_label_set_justify(GTK_LABEL(msg_label), GTK_JUSTIFY_LEFT);
	gtk_box_pack_start(GTK_BOX(vbox), msg_label, FALSE, FALSE, 0);
	gtk_widget_show(msg_label);
		
	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(entry), "activate",
			 G_CALLBACK(entry_activated), NULL);

	combo = gtk_combo_new();
	gtk_box_pack_start(GTK_BOX(vbox), combo, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(GTK_COMBO(combo)->entry), "activate",
			 G_CALLBACK(combo_activated), NULL);

	hbox = gtk_hbox_new(TRUE, 0);

	gtkut_stock_button_set_create(&confirm_area,
				      &ok_button, GTK_STOCK_OK,
				      &cancel_button, GTK_STOCK_CANCEL,
				      NULL, NULL);

	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(dialog)->action_area),
			 confirm_area, FALSE, FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(confirm_area), 5);

	gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);
	
	gtk_widget_grab_default(ok_button);

	g_signal_connect(G_OBJECT(ok_button), "clicked",
			 G_CALLBACK(ok_clicked), NULL);
	g_signal_connect(G_OBJECT(cancel_button), "clicked",
			 G_CALLBACK(cancel_clicked), NULL);
}

static gchar *input_dialog_open(const gchar *title, const gchar *message,
				const gchar *default_string)
{
	gchar *str;

	if (dialog && GTK_WIDGET_VISIBLE(dialog)) return NULL;

	if (!dialog)
		input_dialog_create();

	input_dialog_set(title, message, default_string);
	gtk_widget_show(dialog);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	manage_window_set_transient(GTK_WINDOW(dialog));

	ack = fin = FALSE;

	while (fin == FALSE)
		gtk_main_iteration();

	manage_window_focus_out(dialog, NULL, NULL);
	gtk_widget_hide(dialog);

	if (ack) {
		GtkEditable *editable;

		if (type == INPUT_DIALOG_COMBO)
			editable = GTK_EDITABLE(GTK_COMBO(combo)->entry);
		else
			editable = GTK_EDITABLE(entry);

		str = gtk_editable_get_chars(editable, 0, -1);
		if (str && *str == '\0') {
			g_free(str);
			str = NULL;
		}
	} else
		str = NULL;

	GTK_EVENTS_FLUSH();

	debug_print("return string = %s\n", str ? str : "(none)");
	return str;
}

static void input_dialog_set(const gchar *title, const gchar *message,
			     const gchar *default_string)
{
	GtkWidget *entry_;

	if (type == INPUT_DIALOG_COMBO)
		entry_ = GTK_COMBO(combo)->entry;
	else
		entry_ = entry;

	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_label_set_text(GTK_LABEL(msg_title), title);
	gtk_label_set_text(GTK_LABEL(msg_label), message);
	if (default_string && *default_string) {
		gtk_entry_set_text(GTK_ENTRY(entry_), default_string);
		gtk_editable_set_position(GTK_EDITABLE(entry_), 0);
		gtk_editable_select_region(GTK_EDITABLE(entry_), 0, -1);
	} else
		gtk_entry_set_text(GTK_ENTRY(entry_), "");

	gtk_widget_grab_focus(ok_button);
	gtk_widget_grab_focus(entry_);
}

static void ok_clicked(GtkWidget *widget, gpointer data)
{
	ack = TRUE;
	fin = TRUE;
}

static void cancel_clicked(GtkWidget *widget, gpointer data)
{
	ack = FALSE;
	fin = TRUE;
}

static gint delete_event(GtkWidget *widget, GdkEventAny *event, gpointer data)
{
	ack = FALSE;
	fin = TRUE;

	return TRUE;
}

static gboolean key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if (event && event->keyval == GDK_Escape) {
		ack = FALSE;
		fin = TRUE;
	}

	return FALSE;
}

static void entry_activated(GtkEditable *editable)
{
	ack = TRUE;
	fin = TRUE;
}

static void combo_activated(GtkEditable *editable)
{
	ack = TRUE;
	fin = TRUE;
}
