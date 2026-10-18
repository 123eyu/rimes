#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kOutputPath = NULL;
static GtkWidget *g_entry = NULL;

static void WriteState(const char *kind, const char *text) {
    // GTK can emit an empty preedit after changed/commit. Keep the committed
    // value readable while retaining the separate composition event trace.
    gchar *events_path = g_strdup_printf("%s.events", kOutputPath);
    FILE *events = fopen(events_path, "a");
    g_free(events_path);
    if (events != NULL) {
        fprintf(events, "%s\t%s\n", kind, text ? text : "");
        fclose(events);
    }
    if (strcmp(kind, "preedit") == 0) {
        return;
    }
    FILE *file = fopen(kOutputPath, "w");
    if (file == NULL) {
        return;
    }
    fprintf(file, "%s\t%s\n", kind, text ? text : "");
    fclose(file);
}

static void OnChanged(GtkEditable *editable, gpointer user_data) {
    (void)user_data;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(editable));
    WriteState("commit", text);
}

static void OnPreedit(GtkEntry *entry, gchar *preedit, gpointer user_data) {
    (void)entry;
    (void)user_data;
    WriteState("preedit", preedit ? preedit : "");
}

static gboolean QuitLater(gpointer user_data) {
    (void)user_data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: rimes-gtk-host <output-file> [timeout-ms]\n");
        return EXIT_FAILURE;
    }
    kOutputPath = argv[1];
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "RIMES GTK host");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 80);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    g_entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(window), g_entry);
    g_signal_connect(g_entry, "changed", G_CALLBACK(OnChanged), NULL);
    g_signal_connect(g_entry, "preedit-changed", G_CALLBACK(OnPreedit), NULL);

    gtk_widget_show_all(window);
    gtk_widget_grab_focus(g_entry);
    WriteState("ready", "");

    if (argc >= 3) {
        const int timeout_ms = atoi(argv[2]);
        if (timeout_ms > 0) {
            g_timeout_add((guint)timeout_ms, QuitLater, NULL);
        }
    }
    gtk_main();
    return EXIT_SUCCESS;
}
