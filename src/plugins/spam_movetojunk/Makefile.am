plugindir = $(pkglibdir)/plugins

if BUILD_SPAM_MOVETOJUNK_PLUGIN
plugin_LTLIBRARIES = spammovetojunk.la
endif

spammovetojunk_la_SOURCES = \
	spam_movetojunk.c

spammovetojunk_la_LDFLAGS = \
	-avoid-version -module

if CYGWIN
cygwin_export_lib = -L$(top_builddir)/src -lclaws-mail
else
cygwin_export_lib = 
endif

INCLUDES = \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src/common \
	-I$(top_srcdir)/src/common 

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	$(GTK_CFLAGS)
