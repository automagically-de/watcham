#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <gtk/gtk.h>

#include "main.h"

static WAMConfig *get_config(void);
static WAMThread *get_thread_info(WAMConfig *config, gint i);
static gboolean update_thread_cb(gpointer data);

int main(int argc, char *argv[])
{
	WAMConfig *config;
	gchar *title;
	GtkWidget *window, *vbox, *ibox, *frame;
	gint i;

	gtk_init(&argc, &argv);

	config = get_config();
	if(config == NULL) {
		g_warning("watcham: failed to get configuration");
		return EXIT_FAILURE;
	}
	g_print("watcham: %d threads, varpath=%s\n",
		config->n_threads, config->varpath);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(G_OBJECT(window), "delete-event",
		G_CALLBACK(gtk_main_quit), NULL);
	vbox = gtk_vbox_new(TRUE, 2);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	for(i = 0; i < config->n_threads; i ++) {
		config->threads[i] = get_thread_info(config, i);
		if(config->threads[i] == NULL) {
			g_warning("watcham: failed to get thread info for #%d", i);
			continue;
		}
		title = g_strdup_printf("thread %d", i + 1);
		frame = gtk_frame_new(title);
		g_free(title);
		gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
		ibox = gtk_vbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(frame), ibox);
		config->threads[i]->progress = gtk_progress_bar_new();
		title = g_strdup_printf("%d / %d", 0, config->threads[i]->n_files);
		gtk_progress_bar_set_text(
			GTK_PROGRESS_BAR(config->threads[i]->progress), title);
		gtk_progress_bar_set_fraction(
			GTK_PROGRESS_BAR(config->threads[i]->progress), 0.0);
		gtk_box_pack_start(GTK_BOX(ibox), config->threads[i]->progress,
			FALSE, TRUE, 0);
		config->threads[i]->label = gtk_label_new("");
		gtk_box_pack_start(GTK_BOX(ibox), config->threads[i]->label,
			FALSE, TRUE, 0);
		g_free(title);
		g_timeout_add_seconds(1, update_thread_cb, config->threads[i]);
	}

	gtk_widget_show_all(window);
	gtk_main();

	return EXIT_SUCCESS;
}

static WAMConfig *get_config(void)
{
	WAMConfig *config;
	GIOChannel *io_mlist;
	GError *error = NULL;
	gchar *line, *part, *part2;
	gsize len;

	io_mlist = g_io_channel_new_file("/etc/apt/mirror.list", "r", &error);
	if(error != NULL) {
		g_warning("failed to open /etc/apt/mirror.list: %s", error->message);
		g_error_free(error);
		return NULL;
	}
	config = g_new0(WAMConfig, 1);

	while(g_io_channel_read_line(io_mlist, &line, &len, NULL, NULL) ==
		G_IO_STATUS_NORMAL) {

		g_strstrip(line);
		if((line[0] == '#') || (line[0] == '\0')) {
			g_free(line);
			continue;
		}
		if((strncmp(line, "set", 3) == 0) && isspace(line[3])) {
			part = line + 4;
			g_strchug(part);

			if((strncmp(part, "base_path", 9) == 0) && isspace(part[9])) {
				part2 = part + 10;
				g_strchug(part2);
				config->varpath = g_strdup_printf("%s/var", part2);
			} else if((strncmp(part, "nthreads", 8) == 0) &&
				isspace(part[8])) {
				part2 = part + 9;
				g_strchug(part2);
				config->n_threads = atoi(part2);
				config->threads = g_new0(WAMThread *, config->n_threads);
			} else if((strncmp(part, "var_path", 8) == 0) &&
				isspace(part[8])) {
				part2 = part + 9;
				g_strchug(part2);
				if(config->varpath)
					g_free(config->varpath);
				config->varpath = g_strdup(part2);
			}
		}

		g_free(line);
	}
	g_io_channel_shutdown(io_mlist, FALSE, NULL);
	g_io_channel_unref(io_mlist);

	return config;
}

static WAMThread *get_thread_info(WAMConfig *config, gint i)
{
	WAMThread *thread;
	gchar *fname, *line;
	gsize len;
	GSList *next;
	GIOChannel *io_urls;
	GError *error = NULL;

	/* urls */
	fname = g_strdup_printf("%s/archive-urls.%d", config->varpath, i);
	io_urls = g_io_channel_new_file(fname, "r", &error);
	if(error != NULL) {
		g_warning("failed to read %s: %s", fname, error->message);
		g_error_free(error);
		g_free(fname);
		return NULL;
	}
	g_free(fname);
	thread = g_new0(WAMThread, 1);
	thread->config = config;
	while(g_io_channel_read_line(io_urls, &line, &len, NULL, NULL) ==
		G_IO_STATUS_NORMAL) {
		g_strstrip(line);
		thread->files = g_slist_append(thread->files, g_strdup(line));
		g_free(line);
	}
	thread->n_files = g_slist_length(thread->files);
	g_io_channel_shutdown(io_urls, FALSE, NULL);
	g_io_channel_unref(io_urls);

	/* log */
	fname = g_strdup_printf("%s/archive-log.%d", config->varpath, i);
	error = NULL;
	thread->io_log = g_io_channel_new_file(fname, "r", &error);
	if(error != NULL) {
		g_warning("watcham: failed to open %s: %s", fname, error->message);
		g_error_free(error);
		g_free(fname);
		next = thread->files;
		while(next) {
			line = next->data;
			next = g_slist_remove(next, line);
			g_free(line);
		}
		g_free(thread);
		return NULL;
	}
	g_io_channel_set_flags(thread->io_log, G_IO_FLAG_NONBLOCK, NULL);

	return thread;
}

static gboolean update_thread_cb(gpointer data)
{
	WAMThread *thread = data;
	GError *error = NULL;
	GSList *item;
	gchar *line, *url, *basename, *text;
	gsize len;
	gint32 i;

	g_return_val_if_fail(thread != NULL, FALSE);

	if(g_io_channel_read_line(thread->io_log, &line, &len, NULL, &error) ==
		G_IO_STATUS_EOF) {
		return TRUE;
	}
	while(len > 0) {
		g_strstrip(line);
		if(strncmp(line, "--", 2) == 0) {
			url = strstr(line, "--  ");
			if(url != NULL) {
				url += 4;
				g_strchomp(url);
				i = thread->selected;
				item = thread->prev_file;
				if(item == NULL)
					item = thread->files;
				while(item && (strcmp(url, (gchar *)item->data) != 0)) {
					item = item->next;
					i ++;
				}
				if(item) {
					thread->prev_file = item;
					thread->selected = i;

					text = g_strdup_printf("%d / %d", i + 1, thread->n_files);
					gtk_progress_bar_set_text(
						GTK_PROGRESS_BAR(thread->progress), text);
					g_free(text);
					gtk_progress_bar_set_fraction(
						GTK_PROGRESS_BAR(thread->progress),
						(gdouble)(i + 1) / (gdouble)(thread->n_files));

					basename = g_path_get_basename((gchar *)item->data);
					gtk_label_set_text(GTK_LABEL(thread->label), basename);
					g_free(basename);
				}
			} /* is url line */
		} /* line starts with -- */
		g_free(line);
		while(gtk_events_pending())
			gtk_main_iteration();
		error = NULL;
		if(g_io_channel_read_line(thread->io_log, &line, &len, NULL, &error) ==
			G_IO_STATUS_EOF) {
			return TRUE;
		}
	}
	return TRUE;
}

