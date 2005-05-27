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

#include "defs.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkclist.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "send_message.h"
#include "session.h"
#include "ssl.h"
#include "smtp.h"
#include "prefs_common.h"
#include "prefs_account.h"
#include "procheader.h"
#include "account.h"
#include "progressdialog.h"
#include "statusbar.h"
#include "inputdialog.h"
#include "alertpanel.h"
#include "manage_window.h"
#include "socket.h"
#include "utils.h"
#include "gtkutils.h"
#include "inc.h"
#include "log.h"

typedef struct _SendProgressDialog	SendProgressDialog;

struct _SendProgressDialog
{
	ProgressDialog *dialog;
	Session *session;
	gboolean cancelled;
};

static gint send_recv_message		(Session		*session,
					 const gchar		*msg,
					 gpointer		 data);
static gint send_send_data_progressive	(Session		*session,
					 guint			 cur_len,
					 guint			 total_len,
					 gpointer		 data);
static gint send_send_data_finished	(Session		*session,
					 guint			 len,
					 gpointer		 data);

static SendProgressDialog *send_progress_dialog_create(void);
static void send_progress_dialog_destroy(SendProgressDialog *dialog);

static void send_cancel_button_cb	(GtkWidget	*widget,
					 gpointer	 data);

static void send_put_error		(Session	*session);


gint send_message(const gchar *file, PrefsAccount *ac_prefs, GSList *to_list)
{
	FILE *fp;
	gint val;

	g_return_val_if_fail(file != NULL, -1);
	g_return_val_if_fail(ac_prefs != NULL, -1);
	g_return_val_if_fail(to_list != NULL, -1);

	if ((fp = fopen(file, "rb")) == NULL) {
		FILE_OP_ERROR(file, "fopen");
		return -1;
	}

	if (ac_prefs->use_mail_command && ac_prefs->mail_command &&
	    (*ac_prefs->mail_command)) {
		val = send_message_local(ac_prefs->mail_command, fp);
		fclose(fp);
		return val;
	} else {
		val = send_message_smtp(ac_prefs, to_list, fp);
		
		fclose(fp);
		return val;
	}
}

enum
{
	Q_SENDER     = 0,
	Q_SMTPSERVER = 1,
	Q_RECIPIENTS = 2,
	Q_ACCOUNT_ID = 3
};

gint send_message_local(const gchar *command, FILE *fp)
{
	gchar **argv;
	GPid pid;
	gint child_stdin;
	gchar buf[BUFFSIZE];
	gboolean err = FALSE;

	g_return_val_if_fail(command != NULL, -1);
	g_return_val_if_fail(fp != NULL, -1);

	log_message(_("Sending message using command: %s\n"), command);

	argv = strsplit_with_quote(command, " ", 0);

	if (g_spawn_async_with_pipes(NULL, argv, NULL, 0, NULL, NULL, &pid,
				     &child_stdin, NULL, NULL, NULL) == FALSE) {
		g_snprintf(buf, sizeof(buf),
			   _("Can't execute command: %s"), command);
		log_warning("%s\n", buf);
		alertpanel_error("%s", buf);
		g_strfreev(argv);
		return -1;
	}
	g_strfreev(argv);

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		strretchomp(buf);
		if (buf[0] == '.' && buf[1] == '\0') {
			if (fd_write_all(child_stdin, ".", 1) < 0) {
				err = TRUE;
				break;
			}
		}
		if (fd_write_all(child_stdin, buf, strlen(buf)) < 0 ||
		    fd_write_all(child_stdin, "\n", 1) < 0) {
			err = TRUE;
			break;
		}
	}

	fd_close(child_stdin);
	g_spawn_close_pid(pid);

	if (err) {
		g_snprintf(buf, sizeof(buf),
			   _("Error occurred while executing command: %s"),
			   command);
		log_warning("%s\n", buf);
		alertpanel_error("%s", buf);
		return -1;
	}

	return 0;
}

gint send_message_smtp_full(PrefsAccount *ac_prefs, GSList *to_list, FILE *fp, gboolean keep_session)
{
	Session *session;
	SMTPSession *smtp_session;
	gushort port;
	SendProgressDialog *dialog;
	gchar buf[BUFFSIZE];
	gint ret = 0;
	gboolean was_inited = FALSE;

	g_return_val_if_fail(ac_prefs != NULL, -1);
	g_return_val_if_fail(ac_prefs->address != NULL, -1);
	g_return_val_if_fail(ac_prefs->smtp_server != NULL, -1);
	g_return_val_if_fail(to_list != NULL, -1);
	g_return_val_if_fail(fp != NULL, -1);

	if (!ac_prefs->session) {
		/* we can't reuse a previously initialised session */
		session = smtp_session_new();
		smtp_session = SMTP_SESSION(session);

		smtp_session->hostname =
			ac_prefs->set_domain ? g_strdup(ac_prefs->domain) : NULL;

		if (ac_prefs->use_smtp_auth) {
			smtp_session->forced_auth_type = ac_prefs->smtp_auth_type;
			if (ac_prefs->smtp_userid && strlen(ac_prefs->smtp_userid)) {
				smtp_session->user = g_strdup(ac_prefs->smtp_userid);
				if (ac_prefs->smtp_passwd)
					smtp_session->pass =
						g_strdup(ac_prefs->smtp_passwd);
				else if (ac_prefs->tmp_smtp_pass)
					smtp_session->pass =
						g_strdup(ac_prefs->tmp_smtp_pass);
				else {
					smtp_session->pass =
						input_dialog_query_password
							(ac_prefs->smtp_server,
							 smtp_session->user);
					if (!smtp_session->pass)
						smtp_session->pass = g_strdup("");
					ac_prefs->tmp_smtp_pass =
						g_strdup(smtp_session->pass);
				}
			} else {
				smtp_session->user = g_strdup(ac_prefs->userid);
				if (ac_prefs->passwd)
					smtp_session->pass = g_strdup(ac_prefs->passwd);
				else if (ac_prefs->tmp_pass)
					smtp_session->pass =
						g_strdup(ac_prefs->tmp_pass);
				else {
					smtp_session->pass =
						input_dialog_query_password
							(ac_prefs->smtp_server,
							 smtp_session->user);
					if (!smtp_session->pass)
						smtp_session->pass = g_strdup("");
					ac_prefs->tmp_pass =
						g_strdup(smtp_session->pass);
				}
			}
		} else {
			smtp_session->user = NULL;
			smtp_session->pass = NULL;
		}

	#if USE_OPENSSL
		port = ac_prefs->set_smtpport ? ac_prefs->smtpport :
			ac_prefs->ssl_smtp == SSL_TUNNEL ? SSMTP_PORT : SMTP_PORT;
		session->ssl_type = ac_prefs->ssl_smtp;
		if (ac_prefs->ssl_smtp != SSL_NONE)
			session->nonblocking = ac_prefs->use_nonblocking_ssl;
	#else
		port = ac_prefs->set_smtpport ? ac_prefs->smtpport : SMTP_PORT;
	#endif

		dialog = send_progress_dialog_create();
		dialog->session = session;
		smtp_session->dialog = dialog;

		progress_dialog_list_set(dialog->dialog, 0, NULL, 
					 ac_prefs->smtp_server, 
					 _("Connecting"));

		if (ac_prefs->pop_before_smtp
		    && (ac_prefs->protocol == A_APOP || ac_prefs->protocol == A_POP3)
		    && (time(NULL) - ac_prefs->last_pop_login_time) > (60 * ac_prefs->pop_before_smtp_timeout)) {
			g_snprintf(buf, sizeof(buf), _("Doing POP before SMTP..."));
			log_message(buf);
			progress_dialog_set_label(dialog->dialog, buf);
			progress_dialog_list_set_status(dialog->dialog, 0, _("POP before SMTP"));
			GTK_EVENTS_FLUSH();
			inc_pop_before_smtp(ac_prefs);
		}

		g_snprintf(buf, sizeof(buf), _("Connecting to SMTP server: %s ..."),
			   ac_prefs->smtp_server);
		progress_dialog_set_label(dialog->dialog, buf);
		log_message("%s\n", buf);

		session_set_recv_message_notify(session, send_recv_message, dialog);
		session_set_send_data_progressive_notify
			(session, send_send_data_progressive, dialog);
		session_set_send_data_notify(session, send_send_data_finished, dialog);

		ac_prefs->session = SMTP_SESSION(session);
	} else {
		/* everything is ready to start at MAIL FROM:, just
		 * reinit useful variables. 
		 */
		session = SESSION(ac_prefs->session);
		smtp_session = SMTP_SESSION(session);
		smtp_session->state = SMTP_HELO;
		dialog = (SendProgressDialog *)smtp_session->dialog;
		was_inited = TRUE;
	}

	/* This has to be initialised for every mail sent */
	smtp_session->from = g_strdup(ac_prefs->address);
	smtp_session->to_list = to_list;
	smtp_session->cur_to = to_list;
	smtp_session->send_data = get_outgoing_rfc2822_str(fp);
	smtp_session->send_data_len = strlen(smtp_session->send_data);

	/* connect if necessary */
	if (!was_inited && session_connect(session, ac_prefs->smtp_server, port) < 0) {
		session_destroy(session);
		send_progress_dialog_destroy(dialog);
		return -1;
	}

	debug_print("send_message_smtp(): begin event loop\n");

	if (was_inited) {
		/* as the server is quiet, start sending ourselves */
		smtp_from(smtp_session);
	}

	while (session_is_connected(session) && dialog->cancelled == FALSE
		&& SMTP_SESSION(session)->state != SMTP_MAIL_SENT_OK)
		gtk_main_iteration();

	if (SMTP_SESSION(session)->error_val == SM_AUTHFAIL) {
		if (ac_prefs->smtp_userid && ac_prefs->tmp_smtp_pass) {
			g_free(ac_prefs->tmp_smtp_pass);
			ac_prefs->tmp_smtp_pass = NULL;
		}
		ret = -1;
	} else if (SMTP_SESSION(session)->state == SMTP_MAIL_SENT_OK) {
		log_message("%s\n", _("Mail sent successfully."));
		ret = 0;
	} else if (session->state == SESSION_EOF &&
		   SMTP_SESSION(session)->state == SMTP_QUIT) {
		/* consider EOF right after QUIT successful */
		log_warning("%s\n", _("Connection closed by the remote host."));
		ret = 0;
	} else if (session->state == SESSION_ERROR ||
		   session->state == SESSION_EOF ||
		   session->state == SESSION_TIMEOUT ||
		   SMTP_SESSION(session)->state == SMTP_ERROR ||
		   SMTP_SESSION(session)->error_val != SM_OK)
		ret = -1;
	else if (dialog->cancelled == TRUE)
		ret = -1;

	if (ret == -1) {
		manage_window_focus_in(dialog->dialog->window, NULL, NULL);
		send_put_error(session);
		manage_window_focus_out(dialog->dialog->window, NULL, NULL);
	}

	/* if we should close the connection, let's do it.
	 * Close it in case of error, too, as it helps reinitializing things
	 * easier.
	 */
	if (!keep_session || ret != 0) {
		if (session_is_connected(session))
			smtp_quit(session);
		while (session_is_connected(session))
			gtk_main_iteration();
		session_destroy(session);
		ac_prefs->session = NULL;
		send_progress_dialog_destroy(dialog);
	} else {
		g_free(smtp_session->from);
		g_free(smtp_session->send_data);
		g_free(smtp_session->error_msg);
	}

	statusbar_pop_all();
	statusbar_verbosity_set(FALSE);
	return ret;
}

gint send_message_smtp(PrefsAccount *ac_prefs, GSList *to_list, FILE *fp)
{
	return send_message_smtp_full(ac_prefs, to_list, fp, FALSE);
}

static gint send_recv_message(Session *session, const gchar *msg, gpointer data)
{
	gchar buf[BUFFSIZE];
	SMTPSession *smtp_session = SMTP_SESSION(session);
	SendProgressDialog *dialog = (SendProgressDialog *)data;
	gchar *state_str = NULL;

	g_return_val_if_fail(dialog != NULL, -1);

	switch (smtp_session->state) {
	case SMTP_READY:
	case SMTP_CONNECTED:
		return 0;
	case SMTP_HELO:
		g_snprintf(buf, sizeof(buf), _("Sending HELO..."));
		state_str = _("Authenticating");
		statusbar_print_all(_("Sending message..."));
		break;
	case SMTP_EHLO:
		g_snprintf(buf, sizeof(buf), _("Sending EHLO..."));
		state_str = _("Authenticating");
		statusbar_print_all(_("Sending message..."));
		break;
	case SMTP_AUTH:
		g_snprintf(buf, sizeof(buf), _("Authenticating..."));
		state_str = _("Authenticating");
		break;
	case SMTP_FROM:
		g_snprintf(buf, sizeof(buf), _("Sending MAIL FROM..."));
		state_str = _("Sending");
		break;
	case SMTP_RCPT:
		g_snprintf(buf, sizeof(buf), _("Sending RCPT TO..."));
		state_str = _("Sending");
		break;
	case SMTP_DATA:
	case SMTP_EOM:
		g_snprintf(buf, sizeof(buf), _("Sending DATA..."));
		state_str = _("Sending");
		break;
	case SMTP_QUIT:
		g_snprintf(buf, sizeof(buf), _("Quitting..."));
		state_str = _("Quitting");
		break;
	case SMTP_ERROR:
		g_warning("send: error: %s\n", msg);
		return 0;
	default:
		return 0;
	}

	progress_dialog_set_label(dialog->dialog, buf);
	progress_dialog_list_set_status(dialog->dialog, 0, state_str);

	return 0;
}

static gint send_send_data_progressive(Session *session, guint cur_len,
				       guint total_len, gpointer data)
{
	gchar buf[BUFFSIZE];
	SendProgressDialog *dialog = (SendProgressDialog *)data;

	g_return_val_if_fail(dialog != NULL, -1);

	if (SMTP_SESSION(session)->state != SMTP_SEND_DATA &&
	    SMTP_SESSION(session)->state != SMTP_EOM)
		return 0;

	g_snprintf(buf, sizeof(buf), _("Sending message (%d / %d bytes)"),
		   cur_len, total_len);
	progress_dialog_set_label(dialog->dialog, buf);
	progress_dialog_set_fraction
		(dialog->dialog, (gfloat)cur_len / (gfloat)total_len);

	return 0;
}

static gint send_send_data_finished(Session *session, guint len, gpointer data)
{
	SendProgressDialog *dialog = (SendProgressDialog *)data;

	g_return_val_if_fail(dialog != NULL, -1);

	send_send_data_progressive(session, len, len, dialog);
	return 0;
}

static SendProgressDialog *send_progress_dialog_create(void)
{
	SendProgressDialog *dialog;
	ProgressDialog *progress;

	dialog = g_new0(SendProgressDialog, 1);

	progress = progress_dialog_create();
	gtk_window_set_title(GTK_WINDOW(progress->window),
			     _("Sending message"));
	g_signal_connect(G_OBJECT(progress->cancel_btn), "clicked",
			 G_CALLBACK(send_cancel_button_cb), dialog);
	g_signal_connect(G_OBJECT(progress->window), "delete_event",
			 G_CALLBACK(gtk_true), NULL);
	gtk_window_set_modal(GTK_WINDOW(progress->window), TRUE);
	manage_window_set_transient(GTK_WINDOW(progress->window));

	progress_dialog_get_fraction(progress);

	if (prefs_common.send_dialog_mode == SEND_DIALOG_ALWAYS) {
		gtk_widget_show_now(progress->window);
	}
	
	dialog->dialog = progress;

	return dialog;
}

static void send_progress_dialog_destroy(SendProgressDialog *dialog)
{
	g_return_if_fail(dialog != NULL);
	if (prefs_common.send_dialog_mode == SEND_DIALOG_ALWAYS) {
		progress_dialog_destroy(dialog->dialog);
	}
	g_free(dialog);
}

static void send_cancel_button_cb(GtkWidget *widget, gpointer data)
{
	SendProgressDialog *dialog = (SendProgressDialog *)data;

	dialog->cancelled = TRUE;
}

static void send_put_error(Session *session)
{
	gchar *msg;
	gchar *log_msg = NULL;
	gchar *err_msg = NULL;

	msg = SMTP_SESSION(session)->error_msg;

	switch (SMTP_SESSION(session)->error_val) {
	case SM_ERROR:
	case SM_UNRECOVERABLE:
		log_msg = _("Error occurred while sending the message.");
		if (msg)
			err_msg = g_strdup_printf
				(_("Error occurred while sending the message:\n%s"),
				 msg);
		else
			err_msg = g_strdup(log_msg);
		break;
	case SM_AUTHFAIL:
		log_msg = _("Authentication failed.");
		if (msg)
			err_msg = g_strdup_printf
				(_("Authentication failed:\n%s"), msg);
		else
			err_msg = g_strdup(log_msg);
		break;
	default:
		switch (session->state) {
		case SESSION_ERROR:
			log_msg =
				_("Error occurred while sending the message.");
			err_msg = g_strdup(log_msg);
			break;
		case SESSION_EOF:
			log_msg = _("Connection closed by the remote host.");
			err_msg = g_strdup(log_msg);
			break;
		case SESSION_TIMEOUT:
			log_msg = _("Session timed out.");
			err_msg = g_strdup(log_msg);
			break;
		default:
			break;
		}
		break;
	}

	if (log_msg)
		log_warning("%s\n", log_msg);
	if (err_msg) {
		alertpanel_error_log("%s", err_msg);
		g_free(err_msg);
	}
}

