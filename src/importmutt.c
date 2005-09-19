/*
 * Sylpheed -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 2001 Match Grun
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

/*
 * Import Mutt address book data.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "defs.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtksignal.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktable.h>
#include <gtk/gtkbutton.h>

#include "addrbook.h"
#include "addressbook.h"
#include "addressitem.h"
#include "gtkutils.h"
#include "prefs_common.h"
#include "manage_window.h"
#include "mgutils.h"
#include "mutt.h"
#include "filesel.h"

#define IMPORTMUTT_GUESS_NAME "MUTT Import"

static struct _ImpMutt_Dlg {
	GtkWidget *window;
	GtkWidget *file_entry;
	GtkWidget *name_entry;
	GtkWidget *ok_btn;
	GtkWidget *cancel_btn;
	GtkWidget *statusbar;
	gint status_cid;
} impmutt_dlg;

static struct _AddressFileSelection _imp_mutt_file_selector_;
static AddressBookFile *_importedBook_;
static AddressIndex *_imp_addressIndex_;

/*
* Edit functions.
*/
void imp_mutt_status_show( gchar *msg ) {
	if( impmutt_dlg.statusbar != NULL ) {
		gtk_statusbar_pop( GTK_STATUSBAR(impmutt_dlg.statusbar), impmutt_dlg.status_cid );
		if( msg ) {
			gtk_statusbar_push( GTK_STATUSBAR(impmutt_dlg.statusbar), impmutt_dlg.status_cid, msg );
		}
	}
}

static gboolean imp_mutt_import_file( gchar *sName, gchar *sFile ) {
	gboolean retVal = FALSE;
	gchar *newFile;
	AddressBookFile *abf = NULL;
	MuttFile *mdf = NULL;

	if( _importedBook_ ) {
		addrbook_free_book( _importedBook_ );
	}

	abf = addrbook_create_book();
	addrbook_set_path( abf, _imp_addressIndex_->filePath );
	addrbook_set_name( abf, sName );
	newFile = addrbook_guess_next_file( abf );
	addrbook_set_file( abf, newFile );
	g_free( newFile );

	/* Import data from file */
	mdf = mutt_create();
	mutt_set_file( mdf, sFile );
	if( mutt_import_data( mdf, abf->addressCache ) == MGU_SUCCESS ) {
		addrbook_save_data( abf );
		_importedBook_ = abf;
		retVal = TRUE;
	}
	else {
		addrbook_free_book( abf );
	}

	return retVal;
}

static void imp_mutt_ok( GtkWidget *widget, gboolean *cancelled ) {
	gchar *sName;
	gchar *sFile;
	gchar *sMsg = NULL;
	gboolean errFlag = FALSE;

	sFile = gtk_editable_get_chars( GTK_EDITABLE(impmutt_dlg.file_entry), 0, -1 );
	g_strchug( sFile ); g_strchomp( sFile );
	gtk_entry_set_text( GTK_ENTRY(impmutt_dlg.file_entry), sFile );

	sName = gtk_editable_get_chars( GTK_EDITABLE(impmutt_dlg.name_entry), 0, -1 );
	g_strchug( sName ); g_strchomp( sName );
	gtk_entry_set_text( GTK_ENTRY(impmutt_dlg.name_entry), sName );

	if( *sFile == '\0'|| strlen( sFile ) < 1 ) {
		sMsg = _( "Please select a file." );
		errFlag = TRUE;
	}

	if( *sName == '\0'|| strlen( sName ) < 1 ) {
		if( ! errFlag ) sMsg = _( "Address book name must be supplied." );
		errFlag = TRUE;
	}

	if( errFlag ) {
		imp_mutt_status_show( sMsg );
	}
	else {
		/* Import the file */
		if( imp_mutt_import_file( sName, sFile ) ) {
			*cancelled = FALSE;
			gtk_main_quit();
		}
		else {
			imp_mutt_status_show( _( "Error importing MUTT file." ) );
		}
	}

	g_free( sFile );
	g_free( sName );

}

static void imp_mutt_cancel( GtkWidget *widget, gboolean *cancelled ) {
	*cancelled = TRUE;
	gtk_main_quit();
}

static void imp_mutt_file_select_create( AddressFileSelection *afs ) {
	gchar *file = filesel_select_file_open(_("Select MUTT File"), NULL);
	
	if (file == NULL)
		afs->cancelled = TRUE;
	else {
		afs->cancelled = FALSE;
		gtk_entry_set_text( GTK_ENTRY(impmutt_dlg.file_entry), file );
		g_free(file);
	}
}

static void imp_mutt_file_select( void ) {
	imp_mutt_file_select_create( & _imp_mutt_file_selector_ );
}

static gint imp_mutt_delete_event( GtkWidget *widget, GdkEventAny *event, gboolean *cancelled ) {
	*cancelled = TRUE;
	gtk_main_quit();
	return TRUE;
}

static gboolean imp_mutt_key_pressed( GtkWidget *widget, GdkEventKey *event, gboolean *cancelled ) {
	if (event && event->keyval == GDK_Escape) {
		*cancelled = TRUE;
		gtk_main_quit();
	}
	return FALSE;
}

static void imp_mutt_create( gboolean *cancelled ) {
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *file_entry;
	GtkWidget *name_entry;
	GtkWidget *hbbox;
	GtkWidget *hsep;
	GtkWidget *ok_btn;
	GtkWidget *cancel_btn;
	GtkWidget *file_btn;
	GtkWidget *statusbar;
	GtkWidget *hsbox;
	gint top;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_size_request(window, 450, -1);
	gtk_container_set_border_width( GTK_CONTAINER(window), 0 );
	gtk_window_set_title( GTK_WINDOW(window), _("Import MUTT file into Address Book") );
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_modal(GTK_WINDOW(window), TRUE);	
	g_signal_connect(G_OBJECT(window), "delete_event",
			 G_CALLBACK(imp_mutt_delete_event), cancelled);
	g_signal_connect(G_OBJECT(window), "key_press_event",
			 G_CALLBACK(imp_mutt_key_pressed), cancelled);

	vbox = gtk_vbox_new(FALSE, 8);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_container_set_border_width( GTK_CONTAINER(vbox), 0 );

	table = gtk_table_new(2, 3, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);
	gtk_container_set_border_width( GTK_CONTAINER(table), 8 );
	gtk_table_set_row_spacings(GTK_TABLE(table), 8);
	gtk_table_set_col_spacings(GTK_TABLE(table), 8 );

	/* First row */
	top = 0;
	label = gtk_label_new(_("Name"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, top, (top + 1), GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	name_entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), name_entry, 1, 2, top, (top + 1), GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);

	/* Second row */
	top = 1;
	label = gtk_label_new(_("File"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, top, (top + 1), GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	file_entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), file_entry, 1, 2, top, (top + 1), GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);

	file_btn = gtk_button_new_with_label( _(" ... "));
	gtk_table_attach(GTK_TABLE(table), file_btn, 2, 3, top, (top + 1), GTK_FILL, 0, 3, 0);

	/* Status line */
	hsbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), hsbox, FALSE, FALSE, BORDER_WIDTH);
	statusbar = gtk_statusbar_new();
	gtk_box_pack_start(GTK_BOX(hsbox), statusbar, TRUE, TRUE, BORDER_WIDTH);

	/* Button panel */
	gtkut_stock_button_set_create(&hbbox, &ok_btn, GTK_STOCK_OK,
				      &cancel_btn, GTK_STOCK_CANCEL,
				      NULL, NULL);
	gtk_box_pack_end(GTK_BOX(vbox), hbbox, FALSE, FALSE, 0);
	gtk_container_set_border_width( GTK_CONTAINER(hbbox), 0 );
	gtk_widget_grab_default(ok_btn);

	hsep = gtk_hseparator_new();
	gtk_box_pack_end(GTK_BOX(vbox), hsep, FALSE, FALSE, 0);

	g_signal_connect(G_OBJECT(ok_btn), "clicked",
			 G_CALLBACK(imp_mutt_ok), cancelled);
	g_signal_connect(G_OBJECT(cancel_btn), "clicked",
			 G_CALLBACK(imp_mutt_cancel), cancelled);
	g_signal_connect(G_OBJECT(file_btn), "clicked",
			 G_CALLBACK(imp_mutt_file_select), NULL);

	gtk_widget_show_all(vbox);

	impmutt_dlg.window     = window;
	impmutt_dlg.file_entry = file_entry;
	impmutt_dlg.name_entry = name_entry;
	impmutt_dlg.ok_btn     = ok_btn;
	impmutt_dlg.cancel_btn = cancel_btn;
	impmutt_dlg.statusbar  = statusbar;
	impmutt_dlg.status_cid = gtk_statusbar_get_context_id( GTK_STATUSBAR(statusbar), "Import Mutt Dialog" );
}

AddressBookFile *addressbook_imp_mutt( AddressIndex *addrIndex ) {
	static gboolean cancelled;
	gchar *muttFile;

	_importedBook_ = NULL;
	_imp_addressIndex_ = addrIndex;

	if( ! impmutt_dlg.window )
		imp_mutt_create(&cancelled);
	gtk_widget_grab_focus(impmutt_dlg.ok_btn);
	gtk_widget_grab_focus(impmutt_dlg.file_entry);
	gtk_widget_show(impmutt_dlg.window);
	manage_window_set_transient(GTK_WINDOW(impmutt_dlg.window));

	imp_mutt_status_show( _( "Please select a file to import." ) );
	muttFile = mutt_find_file();
	gtk_entry_set_text( GTK_ENTRY(impmutt_dlg.name_entry), IMPORTMUTT_GUESS_NAME );
	gtk_entry_set_text( GTK_ENTRY(impmutt_dlg.file_entry), muttFile );
	g_free( muttFile );
	muttFile = NULL;

	gtk_main();
	gtk_widget_hide(impmutt_dlg.window);
	_imp_addressIndex_ = NULL;

	if (cancelled == TRUE) return NULL;
	return _importedBook_;
}

/*
* End of Source.
*/

