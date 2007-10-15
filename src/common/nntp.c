/*
 * Sylpheed -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 1999-2007 Hiroyuki Yamamoto and the Claws Mail team
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib.h>
#ifdef ENABLE_NLS
#include <glib/gi18n.h>
#else
#define _(a) (a)
#define N_(a) (a)
#endif
#include <stdio.h>
#include <string.h>

#include "nntp.h"
#include "socket.h"
#include "utils.h"
#include "log.h"
#if USE_OPENSSL
#  include "ssl.h"
#endif

static gint verbose = 1;

static void nntp_session_destroy(Session	*session);

static gint nntp_ok		(SockInfo	*sock,
				 gchar		*argbuf);

static gint nntp_gen_send	(SockInfo	*sock,
				 const gchar	*format,
				 ...);
static gint nntp_gen_recv	(SockInfo	*sock,
				 gchar		*buf,
				 gint		 size);
static gint nntp_gen_command	(NNTPSession	*session,
				 gchar		*argbuf,
				 const gchar	*format,
				 ...);

#if USE_OPENSSL
Session *nntp_session_new(const gchar *server, gushort port, gchar *buf,
			  const gchar *userid, const gchar *passwd,
			  SSLType ssl_type)
#else
Session *nntp_session_new(const gchar *server, gushort port, gchar *buf,
			  const gchar *userid, const gchar *passwd)
#endif
{
	NNTPSession *session;
	SockInfo *sock;

	if ((sock = sock_connect(server, port)) == NULL) {
		log_warning(LOG_PROTOCOL, _("Can't connect to NNTP server: %s:%d\n"),
			    server, port);
		return NULL;
	}

#if USE_OPENSSL
	if (ssl_type == SSL_TUNNEL && !ssl_init_socket(sock)) {
		log_error(LOG_PROTOCOL, _("SSL handshake failed\n"));
		sock_close(sock);
		return NULL;
	}
#endif

	if (nntp_ok(sock, buf) != NN_SUCCESS) {
		sock_close(sock);
		return NULL;
	}

	session = g_new0(NNTPSession, 1);

	session_init(SESSION(session));

	SESSION(session)->type			= SESSION_NEWS;
	SESSION(session)->server		= g_strdup(server);
	SESSION(session)->sock			= sock;
	SESSION(session)->last_access_time	= time(NULL);
	SESSION(session)->data			= NULL;

	SESSION(session)->destroy		= nntp_session_destroy;

	session->group = NULL;
	
	nntp_mode(session, FALSE);
	
	if (userid && passwd) {
		gint ok;

		session->userid = g_strdup(userid);
		session->passwd = g_strdup(passwd);

		ok = nntp_gen_send(sock, "AUTHINFO USER %s", session->userid);
		if (ok != NN_SUCCESS) {
			session_destroy(SESSION(session));
			return NULL;
		}
		ok = nntp_ok(sock, NULL);
		if (ok == NN_AUTHCONT) {
			ok = nntp_gen_send(sock, "AUTHINFO PASS %s",
					   session->passwd);
			if (ok != NN_SUCCESS) {
				session_destroy(SESSION(session));
				return NULL;
			}
			ok = nntp_ok(sock, NULL);
			if (ok != NN_SUCCESS)
				session->auth_failed = TRUE;
		}
		if (ok == NN_SOCKET) {
			session_destroy(SESSION(session));
			return NULL;
		}
	}

	session_set_access_time(SESSION(session));
	
	return SESSION(session);
}

void nntp_forceauth(NNTPSession *session, gchar *buf, const gchar *userid, const gchar *passwd)

{
	if (!session) return;
		
	nntp_gen_command(session, buf , "AUTHINFO USER %s", userid);


}

static void nntp_session_destroy(Session *session)
{
	NNTPSession *nntp_session = NNTP_SESSION(session);

	g_return_if_fail(session != NULL);

	g_free(nntp_session->group);
	g_free(nntp_session->userid);
	g_free(nntp_session->passwd);
}

gint nntp_group(NNTPSession *session, const gchar *group,
		gint *num, gint *first, gint *last)
{
	gint ok;
	gint resp;
	gchar buf[NNTPBUFSIZE];

	ok = nntp_gen_command(session, buf, "GROUP %s", group);

	if (ok != NN_SUCCESS && ok != NN_SOCKET && ok != NN_AUTHREQ) {
		ok = nntp_mode(session, FALSE);
		if (ok == NN_SUCCESS)
			ok = nntp_gen_command(session, buf, "GROUP %s", group);
	}

	if (ok != NN_SUCCESS)
		return ok;

	if (sscanf(buf, "%d %d %d %d", &resp, num, first, last)
	    != 4) {
		log_warning(LOG_PROTOCOL, _("protocol error: %s\n"), buf);
		return NN_PROTOCOL;
	}

	return NN_SUCCESS;
}

gint nntp_get_article(NNTPSession *session, const gchar *cmd, gint num,
		      gchar **msgid)
{
	gint ok;
	gchar buf[NNTPBUFSIZE];

	if (num > 0)
		ok = nntp_gen_command(session, buf, "%s %d", cmd, num);
	else
		ok = nntp_gen_command(session, buf, cmd);

	if (ok != NN_SUCCESS)
		return ok;

	extract_parenthesis(buf, '<', '>');
	if (buf[0] == '\0') {
		log_warning(LOG_PROTOCOL, _("protocol error\n"));
		*msgid = g_strdup("0");
	} else
		*msgid = g_strdup(buf);

	return NN_SUCCESS;
}

gint nntp_xover(NNTPSession *session, gint first, gint last)
{
	gint ok;
	gchar buf[NNTPBUFSIZE];

	ok = nntp_gen_command(session, buf, "XOVER %d-%d", first, last);
	if (ok != NN_SUCCESS)
		return ok;

	return NN_SUCCESS;
}

gint nntp_xhdr(NNTPSession *session, const gchar *header, gint first, gint last)
{
	gint ok;
	gchar buf[NNTPBUFSIZE];

	ok = nntp_gen_command(session, buf, "XHDR %s %d-%d",
			      header, first, last);
	if (ok != NN_SUCCESS)
		return ok;

	return NN_SUCCESS;
}

gint nntp_list(NNTPSession *session)
{
	return nntp_gen_command(session, NULL, "LIST");
}

gint nntp_post(NNTPSession *session, FILE *fp)
{
	gint ok;
	gchar buf[NNTPBUFSIZE];
	gchar *msg;

	ok = nntp_gen_command(session, buf, "POST");
	if (ok != NN_SUCCESS)
		return ok;

	msg = get_outgoing_rfc2822_str(fp);
	if (sock_write_all(SESSION(session)->sock, msg, strlen(msg)) < 0) {
		log_warning(LOG_PROTOCOL, _("Error occurred while posting\n"));
		g_free(msg);
		return NN_SOCKET;
	}
	g_free(msg);

	sock_write_all(SESSION(session)->sock, ".\r\n", 3);
	if ((ok = nntp_ok(SESSION(session)->sock, buf)) != NN_SUCCESS)
		return ok;

	session_set_access_time(SESSION(session));

	return NN_SUCCESS;
}

gint nntp_mode(NNTPSession *session, gboolean stream)
{
	gint ok;

	ok = nntp_gen_command(session, NULL, "MODE %s",
			      stream ? "STREAM" : "READER");

	return ok;
}

static gint nntp_ok(SockInfo *sock, gchar *argbuf)
{
	gint ok;
	gchar buf[NNTPBUFSIZE];

	if ((ok = nntp_gen_recv(sock, buf, sizeof(buf))) == NN_SUCCESS) {
		if (strlen(buf) < 3)
			return NN_ERROR;

		if ((buf[0] == '1' || buf[0] == '2' || buf[0] == '3') &&
		    (buf[3] == ' ' || buf[3] == '\0')) {
			if (argbuf)
				strcpy(argbuf, buf);

			if (!strncmp(buf, "381", 3))
				return NN_AUTHCONT;

			return NN_SUCCESS;
		} else if (!strncmp(buf, "480", 3))
			return NN_AUTHREQ;
		else
			return NN_ERROR;
	}

	return ok;
}

static gint nntp_gen_send(SockInfo *sock, const gchar *format, ...)
{
	gchar buf[NNTPBUFSIZE];
	va_list args;

	va_start(args, format);
	g_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if (verbose) {
		if (!g_ascii_strncasecmp(buf, "AUTHINFO PASS", 13))
			log_print(LOG_PROTOCOL, "NNTP> AUTHINFO PASS ********\n");
		else
			log_print(LOG_PROTOCOL, "NNTP> %s\n", buf);
	}

	strcat(buf, "\r\n");
	if (sock_write_all(sock, buf, strlen(buf)) < 0) {
		log_warning(LOG_PROTOCOL, _("Error occurred while sending command\n"));
		return NN_SOCKET;
	}

	return NN_SUCCESS;
}

static gint nntp_gen_recv(SockInfo *sock, gchar *buf, gint size)
{
	if (sock_gets(sock, buf, size) == -1)
		return NN_SOCKET;

	strretchomp(buf);

	if (verbose)
		log_print(LOG_PROTOCOL, "NNTP< %s\n", buf);

	return NN_SUCCESS;
}

static gint nntp_gen_command(NNTPSession *session, gchar *argbuf,
			     const gchar *format, ...)
{
	gchar buf[NNTPBUFSIZE];
	va_list args;
	gint ok;
	SockInfo *sock;

	va_start(args, format);
	g_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	sock = SESSION(session)->sock;
	ok = nntp_gen_send(sock, "%s", buf);
	if (ok != NN_SUCCESS)
		return ok;
	ok = nntp_ok(sock, argbuf);
	if (ok == NN_AUTHREQ) {
		if (!session->userid || !session->passwd) {
			session->auth_failed = TRUE;
			return ok;
		}

		ok = nntp_gen_send(sock, "AUTHINFO USER %s", session->userid);
		if (ok != NN_SUCCESS)
			return ok;
		ok = nntp_ok(sock, NULL);
		if (ok == NN_AUTHCONT) {
			ok = nntp_gen_send(sock, "AUTHINFO PASS %s",
					   session->passwd);
			if (ok != NN_SUCCESS)
				return ok;
			ok = nntp_ok(sock, NULL);
		}
		if (ok != NN_SUCCESS) {
			session->auth_failed = TRUE;
			return ok;
		}

		ok = nntp_gen_send(sock, "%s", buf);
		if (ok != NN_SUCCESS)
			return ok;
		ok = nntp_ok(sock, argbuf);

	} else if (ok == NN_AUTHCONT) {
                ok = nntp_gen_send(sock, "AUTHINFO PASS %s",
				   session->passwd);
		if (ok != NN_SUCCESS)  {
			session->auth_failed = TRUE;
			return ok;
		}
                ok = nntp_ok(sock, NULL);
       }
       
	session_set_access_time(SESSION(session));
	
	return ok;
}
