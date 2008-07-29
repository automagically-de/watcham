#ifndef _MAIN_H
#define _MAIN_H

#include <gtk/gtk.h>

typedef struct _WAMConfig WAMConfig;

typedef struct {
	WAMConfig *config;
	GIOChannel *io_log;
	GSList *files;
	GSList *prev_file;
	gint32 selected;
	guint32 n_files;
	GtkWidget *progress;
	GtkWidget *label;
} WAMThread;

struct _WAMConfig {
	gchar *varpath;
	guint32 n_threads;
	WAMThread **threads;
};

#endif
