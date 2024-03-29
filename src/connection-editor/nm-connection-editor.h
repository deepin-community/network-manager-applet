// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Copyright 2007 Rodrigo Moya <rodrigo@gnome-db.org>
 * Copyright 2007 - 2014 Red Hat, Inc.
 */

#ifndef NM_CONNECTION_EDITOR_H
#define NM_CONNECTION_EDITOR_H

#include <glib-object.h>

#include <NetworkManager.h>

#include "utils.h"

#define NM_TYPE_CONNECTION_EDITOR    (nm_connection_editor_get_type ())
#define NM_IS_CONNECTION_EDITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_CONNECTION_EDITOR))
#define NM_CONNECTION_EDITOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_CONNECTION_EDITOR, NMConnectionEditor))

#define NM_CONNECTION_EDITOR_DONE       "done"
#define NM_CONNECTION_EDITOR_NEW_EDITOR "new-editor"

typedef struct GetSecretsInfo GetSecretsInfo;

typedef struct {
	GObject parent;
	gboolean disposed;

	GtkWindow *parent_window;
	NMClient *client;
	gulong permission_id;

	/* private data */
	NMConnection *connection;
	NMConnection *orig_connection;
	gboolean is_new_connection;

	GetSecretsInfo *secrets_call;
	GSList *pending_secrets_calls;

	GtkWidget *all_checkbutton;
	NMClientPermissionResult can_modify;

	GSList *initializing_pages;
	GSList *pages;
	GtkBuilder *builder;
	GtkWidget *window;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *export_button;
	GtkWidget *relabel_info;
	GtkWidget *relabel_dialog;
	GtkWidget *relabel_button;
	GtkListStore *relabel_list;

	gboolean busy;
	gboolean init_run;
	guint validate_id;

	char *last_validation_error;

	GHashTable *inter_page_hash;
	GSList *unsupported_properties;
} NMConnectionEditor;

typedef struct {
	GObjectClass parent_class;
} NMConnectionEditorClass;

typedef enum {
	/* Add item for inter-page changes here */
	INTER_PAGE_CHANGE_WIFI_MODE = 1,
	INTER_PAGE_CHANGE_MACSEC_MODE = 2,
	INTER_PAGE_CHANGE_802_1X_ENABLE = 3,
} InterPageChangeType;

GType               nm_connection_editor_get_type (void);
NMConnectionEditor *nm_connection_editor_new (GtkWindow *parent_window,
                                              NMConnection *connection,
                                              NMClient *client);
NMConnectionEditor *nm_connection_editor_get (NMConnection *connection);
NMConnectionEditor *nm_connection_editor_get_controller (NMConnection *port);

void                nm_connection_editor_present (NMConnectionEditor *editor);
void                nm_connection_editor_run (NMConnectionEditor *editor);
NMConnection *      nm_connection_editor_get_connection (NMConnectionEditor *editor);
GtkWindow *         nm_connection_editor_get_window (NMConnectionEditor *editor);
gboolean            nm_connection_editor_get_busy (NMConnectionEditor *editor);
void                nm_connection_editor_set_busy (NMConnectionEditor *editor, gboolean busy);

void                nm_connection_editor_error (GtkWindow *parent,
                                                const char *heading,
                                                const char *format,
                                                ...) _nm_printf(3,4);
void                nm_connection_editor_warning (GtkWindow *parent,
                                                  const char *heading,
                                                  const char *format,
                                                  ...) _nm_printf(3,4);

void               nm_connection_editor_inter_page_set_value (NMConnectionEditor *editor,
                                                              InterPageChangeType type,
                                                              gpointer value);
gboolean           nm_connection_editor_inter_page_get_value (NMConnectionEditor *editor,
                                                              InterPageChangeType type,
                                                              gpointer *value);
void               nm_connection_editor_inter_page_clear_data (NMConnectionEditor *editor);

void               nm_connection_editor_add_unsupported_property (NMConnectionEditor *editor,
                                                                  const char *name);
void               nm_connection_editor_check_unsupported_properties (NMConnectionEditor *editor,
                                                                      NMSetting *setting,
                                                                      const char *const *known_props);
#endif
