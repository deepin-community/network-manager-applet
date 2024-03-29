// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Copyright 2012 - 2014 Red Hat, Inc.
 */

#include "nm-default.h"

#include <stdlib.h>

#include <NetworkManager.h>

#include "page-controller.h"
#include "nm-connection-editor.h"

G_DEFINE_TYPE (CEPageController, ce_page_controller, CE_TYPE_PAGE)

#define CE_PAGE_CONTROLLER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_CONTROLLER, CEPageControllerPrivate))

typedef struct {
	const char *uuid;

	GtkWindow *toplevel;

	GtkEntry *interface_name;

	GtkTreeView *connections;
	GtkTreeModel *connections_model;
	GtkButton *add, *edit, *delete;

	GHashTable *new_ports;  /* track whether some port(s) were added */

} CEPageControllerPrivate;

static void finish_setup (CEPageController *self, gpointer user_data);

enum {
	COL_CONNECTION,
	COL_NAME
};

static int
name_sort_func (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
	NMConnection *conn_a, *conn_b;
	int ret;

	/* We fetch COL_CONNECTION rather than COL_NAME to avoid a strdup/free. */
	gtk_tree_model_get (model, a, COL_CONNECTION, &conn_a, -1);
	gtk_tree_model_get (model, b, COL_CONNECTION, &conn_b, -1);
	ret = strcmp (nm_connection_get_id (conn_a), nm_connection_get_id (conn_b));
	g_object_unref (conn_a);
	g_object_unref (conn_b);

	return ret;
}

static void
constructed (GObject *object)
{
	CEPageController *self = CE_PAGE_CONTROLLER (object);

	g_signal_connect (self, CE_PAGE_INITIALIZED, G_CALLBACK (finish_setup), NULL);

	G_OBJECT_CLASS (ce_page_controller_parent_class)->constructed (object);
}

static void
dispose (GObject *object)
{
	CEPageController *self = CE_PAGE_CONTROLLER (object);
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	GtkTreeIter iter;

	g_signal_handlers_disconnect_matched (CE_PAGE (self)->client, G_SIGNAL_MATCH_DATA,
	                                      0, 0, NULL, NULL, self);

	if (gtk_tree_model_get_iter_first (priv->connections_model, &iter)) {
		do {
			NMRemoteConnection *connection = NULL;

			gtk_tree_model_get (priv->connections_model, &iter,
			                    COL_CONNECTION, &connection,
			                    -1);
			g_signal_handlers_disconnect_matched (connection, G_SIGNAL_MATCH_DATA,
			                                      0, 0, NULL, NULL, self);
			g_object_unref (connection);
		} while (gtk_tree_model_iter_next (priv->connections_model, &iter));
	}

	g_hash_table_destroy (priv->new_ports);

	G_OBJECT_CLASS (ce_page_controller_parent_class)->dispose (object);
}

static void
stuff_changed (GtkWidget *widget, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static gboolean
find_connection (CEPageController *self, NMRemoteConnection *connection, GtkTreeIter *iter)
{
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);

	if (!gtk_tree_model_get_iter_first (priv->connections_model, iter))
		return FALSE;

	do {
		NMRemoteConnection *candidate = NULL;

		gtk_tree_model_get (priv->connections_model, iter,
		                    COL_CONNECTION, &candidate,
		                    -1);
		if (candidate == connection)
			return TRUE;
	} while (gtk_tree_model_iter_next (priv->connections_model, iter));

	return FALSE;
}

static void
connection_removed (NMClient *client,
                    NMRemoteConnection *connection,
                    gpointer user_data)
{
	CEPageController *self = CE_PAGE_CONTROLLER (user_data);
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	GtkTreeIter iter;

	if (!find_connection (self, connection, &iter))
		return;

	gtk_list_store_remove (GTK_LIST_STORE (priv->connections_model), &iter);
	ce_page_changed (CE_PAGE (self));
}

static void
connection_changed (NMRemoteConnection *connection, gpointer user_data)
{
	CEPageController *self = CE_PAGE_CONTROLLER (user_data);
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	GtkTreeIter iter;
	NMSettingConnection *s_con;

	if (!find_connection (self, connection, &iter))
		return;

	/* Name might have changed */
	s_con = nm_connection_get_setting_connection (NM_CONNECTION (connection));
	gtk_list_store_set (GTK_LIST_STORE (priv->connections_model), &iter,
	                    COL_NAME, nm_setting_connection_get_id (s_con),
	                    -1);
}

static NMDevice *
get_device_for_connection (NMClient *client, NMConnection *conn)
{
	const GPtrArray *devices;
	NMSettingConnection *s_con;
	int i;

	devices = nm_client_get_devices (client);
	if (!devices)
		return NULL;

	/* Make sure the connection is actually locked to a specific device */
	s_con = nm_connection_get_setting_connection (conn);
	if (   !nm_setting_connection_get_interface_name (s_con)
	       && !nm_connection_get_interface_name (conn)) {
		NMSetting *s_hw;
		gs_free char *mac_address = NULL;

		s_hw = nm_connection_get_setting_by_name (conn, nm_setting_connection_get_connection_type (s_con));
		if (!s_hw || !g_object_class_find_property (G_OBJECT_GET_CLASS (s_hw), "mac-address"))
			return NULL;

		g_object_get (G_OBJECT (s_hw), "mac-address", &mac_address, NULL);
		if (!mac_address)
			return NULL;
	}

	/* OK, now find that device */
	for (i = 0; i < devices->len; i++) {
		NMDevice *device = devices->pdata[i];

		if (nm_device_connection_compatible (device, conn, NULL))
			return device;
	}

	return NULL;
}

static void
check_new_port_physical_port (CEPageController *self, NMConnection *conn)
{
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	NMConnection *conn2;
	NMDevice *dev, *dev2;
	const char *id, *id2;
	GtkTreeIter iter;

	dev = get_device_for_connection (CE_PAGE (self)->client, conn);
	if (!dev)
		return;
	id = nm_device_get_physical_port_id (dev);
	if (!id)
		return;

	if (!gtk_tree_model_get_iter_first (priv->connections_model, &iter))
		return;
	do {
		gtk_tree_model_get (priv->connections_model, &iter, 0, &conn2, -1);
		g_object_unref (conn2); /* gtk_tree_model_get() adds a ref */
		dev2 = get_device_for_connection (CE_PAGE (self)->client, conn2);
		if (!dev2)
			continue;
		if (dev == dev2) {
			nm_connection_editor_warning (CE_PAGE (self)->parent_window,
			                              _("Duplicate ports"),
			                              _("Ports “%s” and “%s” both apply to device “%s”"),
			                              nm_connection_get_id (conn),
			                              nm_connection_get_id (conn2),
			                              nm_device_get_iface (dev));
			return;
		}

		id2 = nm_device_get_physical_port_id (dev2);
		if (self->aggregating && id && id2 && !strcmp (id, id2)) {
			nm_connection_editor_warning (CE_PAGE (self)->parent_window,
			                              _("Duplicate ports"),
			                              _("Ports “%s” and “%s” apply to different virtual "
			                                "ports (“%s” and “%s”) of the same physical device."),
			                              nm_connection_get_id (conn),
			                              nm_connection_get_id (conn2),
			                              nm_device_get_iface (dev),
			                              nm_device_get_iface (dev2));
			return;
		}
	} while (gtk_tree_model_iter_next (priv->connections_model, &iter));
}

static void
connection_added (NMClient *client,
                  NMRemoteConnection *connection,
                  gpointer user_data)
{
	CEPageController *self = CE_PAGE_CONTROLLER (user_data);
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	NMSettingConnection *s_con;
	const char *controller_type;
	const char *port_type, *controller;
	const char *interface_name;
	GtkTreeIter iter;

	s_con = nm_connection_get_setting_connection (CE_PAGE (self)->connection);
	controller_type = nm_setting_connection_get_connection_type (s_con);

	s_con = nm_connection_get_setting_connection (NM_CONNECTION (connection));
	if (!s_con)
		return;

	port_type = nm_setting_connection_get_slave_type (s_con);
	if (g_strcmp0 (port_type, controller_type) != 0)
		return;

	controller = nm_setting_connection_get_master (s_con);
	if (!controller)
		return;

	interface_name = nm_connection_get_interface_name (CE_PAGE (self)->connection);
	if (g_strcmp0 (controller, interface_name) != 0 && g_strcmp0 (controller, priv->uuid) != 0)
		return;

	check_new_port_physical_port (self, NM_CONNECTION (connection));

	gtk_list_store_append (GTK_LIST_STORE (priv->connections_model), &iter);
	gtk_list_store_set (GTK_LIST_STORE (priv->connections_model), &iter,
	                    COL_CONNECTION, connection,
	                    COL_NAME, nm_setting_connection_get_id (s_con),
	                    -1);
	ce_page_changed (CE_PAGE (self));

	g_signal_connect (client, NM_CLIENT_CONNECTION_REMOVED,
	                  G_CALLBACK (connection_removed), self);
	g_signal_connect (connection, NM_CONNECTION_CHANGED,
	                  G_CALLBACK (connection_changed), self);
}

static void
connections_selection_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	CEPageController *self = user_data;
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	GtkTreeIter iter;
	GtkTreeModel *model;
	NMRemoteConnection *connection;
	NMSettingConnection *s_con;
	gboolean sensitive = FALSE;

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
		                    0, &connection,
		                    -1);
		s_con = nm_connection_get_setting_connection (NM_CONNECTION (connection));
		g_assert (s_con);

		sensitive = !nm_setting_connection_get_read_only (s_con);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (priv->edit), sensitive);
	gtk_widget_set_sensitive (GTK_WIDGET (priv->delete), sensitive);
}

static void
add_response_cb (NMConnectionEditor *editor, GtkResponseType response, gpointer user_data)
{
	CEPageController *self = user_data;
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	const char *uuid;

	if (response == GTK_RESPONSE_OK) {
		uuid = nm_connection_get_uuid (editor->connection);
		g_hash_table_add (priv->new_ports, g_strdup (uuid));
	}
	g_object_unref (editor);
}

static void
add_connection (FUNC_TAG_NEW_CONNECTION_RESULT_IMPL,
                NMConnection *connection,
                gpointer user_data)
{
	CEPageController *self = user_data;
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	NMSettingConnection *s_con;
	NMConnectionEditor *editor;
	const char *iface_name, *controller_type;
	char *name;

	if (!connection)
		return;

	s_con = nm_connection_get_setting_connection (CE_PAGE (self)->connection);
	controller_type = nm_setting_connection_get_connection_type (s_con);

	/* Mark the connection as a port, and rename it. */
	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con != NULL);

	iface_name = gtk_entry_get_text (priv->interface_name);
	if (!*iface_name)
		iface_name = nm_connection_get_interface_name (connection);
	if (!iface_name || !nm_utils_is_valid_iface_name (iface_name, NULL))
		iface_name = nm_connection_get_id (connection);
	name = g_strdup_printf (_("%s port %d"), iface_name,
	                        gtk_tree_model_iter_n_children (priv->connections_model, NULL) + 1);

	g_object_set (G_OBJECT (s_con),
	              NM_SETTING_CONNECTION_ID, name,
	              NM_SETTING_CONNECTION_MASTER, priv->uuid,
	              NM_SETTING_CONNECTION_SLAVE_TYPE, controller_type,
	              NM_SETTING_CONNECTION_AUTOCONNECT, TRUE,
	              NULL);
	g_free (name);

	editor = ce_page_new_editor (CE_PAGE (self), priv->toplevel, connection);
	if (!editor)
		return;

	g_signal_connect (editor, NM_CONNECTION_EDITOR_DONE, G_CALLBACK (add_response_cb), self);
	nm_connection_editor_run (editor);
}

static void
add_clicked (GtkButton *button, gpointer user_data)
{
	CEPageController *self = user_data;

	CE_PAGE_CONTROLLER_GET_CLASS (self)->add_port (self, add_connection);
}

static NMRemoteConnection *
get_selected_connection (CEPageController *self)
{
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	GtkTreeSelection *selection;
	GList *selected_rows;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	NMRemoteConnection *connection = NULL;

	selection = gtk_tree_view_get_selection (priv->connections);
	selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);
	if (!selected_rows)
		return NULL;

	if (gtk_tree_model_get_iter (model, &iter, (GtkTreePath *) selected_rows->data))
		gtk_tree_model_get (model, &iter, 0, &connection, -1);

	g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);

	return connection;
}

static void
edit_done_cb (NMConnectionEditor *editor, GtkResponseType response, gpointer user_data)
{
	g_object_unref (editor);
}

static void
edit_clicked (GtkButton *button, gpointer user_data)
{
	CEPageController *self = user_data;
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	NMConnectionEditor *editor;
	NMRemoteConnection *connection;

	connection = get_selected_connection (self);
	if (!connection)
		return;

	editor = nm_connection_editor_get (NM_CONNECTION (connection));
	if (editor) {
		nm_connection_editor_present (editor);
		return;
	}

	editor = ce_page_new_editor (CE_PAGE (self), priv->toplevel, NM_CONNECTION (connection));
	if (!editor)
		return;

	g_signal_connect (editor, NM_CONNECTION_EDITOR_DONE, G_CALLBACK (edit_done_cb), self);
	nm_connection_editor_run (editor);
}

static void
connection_double_clicked_cb (GtkTreeView *tree_view,
                              GtkTreePath *path,
                              GtkTreeViewColumn *column,
                              gpointer user_data)
{
	edit_clicked (NULL, user_data);
}

static void
delete_result_cb (FUNC_TAG_DELETE_CONNECTION_RESULT_IMPL,
                  NMRemoteConnection *connection,
                  gboolean deleted,
                  gpointer user_data)
{
	CEPageController *self = user_data;
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);

	if (deleted) {
		g_hash_table_remove (priv->new_ports,
		                     nm_connection_get_uuid (NM_CONNECTION (connection)));
	}
}

static void
delete_clicked (GtkButton *button, gpointer user_data)
{
	CEPageController *self = user_data;
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	NMRemoteConnection *connection;

	connection = get_selected_connection (self);
	if (!connection)
		return;

	delete_connection (priv->toplevel, connection, delete_result_cb, self);
}

static void
populate_ui (CEPageController *self)
{
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	NMSettingConnection *s_con;
	const char *iface;
	const GPtrArray *connections;
	int i;

	s_con = nm_connection_get_setting_connection (CE_PAGE (self)->connection);
	g_return_if_fail (s_con != NULL);

	/* Interface name */
	iface = nm_connection_get_interface_name (CE_PAGE (self)->connection);
	gtk_entry_set_text (priv->interface_name, iface ? iface : "");

	/* Port connections */
	connections = nm_client_get_connections (CE_PAGE (self)->client);
	for (i = 0; i < connections->len; i++)
		connection_added (CE_PAGE (self)->client, connections->pdata[i], self);
}

static void
finish_setup (CEPageController *self, gpointer user_data)
{
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	GtkTreeSelection *selection;
	GtkBuilder *builder;
	NMSettingConnection *s_con;

	builder = CE_PAGE (self)->builder;

	priv->interface_name = GTK_ENTRY (gtk_builder_get_object (builder, "controller_interface"));

	priv->connections = GTK_TREE_VIEW (gtk_builder_get_object (builder, "controller_connections"));
	priv->connections_model = GTK_TREE_MODEL (gtk_builder_get_object (builder, "controller_connections_model"));
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (priv->connections_model),
	                                 COL_NAME, name_sort_func,
	                                 NULL, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->connections_model),
	                                      COL_NAME, GTK_SORT_ASCENDING);

	priv->add = GTK_BUTTON (gtk_builder_get_object (builder, "controller_connection_add"));
	priv->edit = GTK_BUTTON (gtk_builder_get_object (builder, "controller_connection_edit"));
	priv->delete = GTK_BUTTON (gtk_builder_get_object (builder, "controller_connection_delete"));

	priv->toplevel = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (priv->connections),
	                                                      GTK_TYPE_WINDOW));

	s_con = nm_connection_get_setting_connection (CE_PAGE (self)->connection);
	priv->uuid = nm_setting_connection_get_uuid (s_con);

	populate_ui (self);

	g_signal_connect (CE_PAGE (self)->client, NM_CLIENT_CONNECTION_ADDED,
	                  G_CALLBACK (connection_added), self);

	g_signal_connect (priv->interface_name, "changed", G_CALLBACK (stuff_changed), self);

	g_signal_connect (priv->add, "clicked", G_CALLBACK (add_clicked), self);
	g_signal_connect (priv->edit, "clicked", G_CALLBACK (edit_clicked), self);
	g_signal_connect (priv->delete, "clicked", G_CALLBACK (delete_clicked), self);

	g_signal_connect (priv->connections, "row-activated", G_CALLBACK (connection_double_clicked_cb), self);

	selection = gtk_tree_view_get_selection (priv->connections);
	g_signal_connect (selection, "changed", G_CALLBACK (connections_selection_changed_cb), self);
	connections_selection_changed_cb (selection, self);
}

static void
ui_to_setting (CEPageController *self)
{
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	NMSettingConnection *s_con;
	const char *interface_name;

	/* Interface name */
	s_con = nm_connection_get_setting_connection (CE_PAGE (self)->connection);
	interface_name = gtk_entry_get_text (priv->interface_name);
	if (!*interface_name)
		interface_name = NULL;
	g_object_set (s_con, "interface-name", interface_name, NULL);

	/* Ports are updated as they're edited, so nothing to do */
}

static gboolean
ce_page_validate_v (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageController *self = CE_PAGE_CONTROLLER (page);

	/* We don't need to recursively check that the ports connections
	 * are valid because they can't end up in the table if they're not.
	 */

	ui_to_setting (self);

	/* Subtype ce_page_validate_v() method will validate the interface name */
	return TRUE;
}

static gboolean
last_update (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageController *self = CE_PAGE_CONTROLLER (page);
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	const char *interface_name, *tmp, *uuid;
	NMSettingConnection *s_con;
	GtkTreeIter iter;

	/* No new port added - leave master property as it is. */
	if (g_hash_table_size (priv->new_ports) == 0)
		return TRUE;

	/*
	 * Set master property of all  ports to be the interface name.
	 * Even if UUID has the advantage of being stable and thus easier to use,
	 * users may prefer using interface name instead.
	*/
	interface_name = gtk_entry_get_text (priv->interface_name);
	if (gtk_tree_model_get_iter_first (priv->connections_model, &iter)) {
		do {
			NMRemoteConnection *rcon = NULL;

			gtk_tree_model_get (priv->connections_model, &iter,
			                    COL_CONNECTION, &rcon,
			                    -1);
			tmp = nm_connection_get_interface_name (NM_CONNECTION (rcon));
			uuid = nm_connection_get_uuid (NM_CONNECTION (rcon));
			if (   g_hash_table_contains (priv->new_ports, uuid)
			    && g_strcmp0 (interface_name, tmp) != 0) {
				s_con = nm_connection_get_setting_connection (NM_CONNECTION (rcon));
				g_object_set (s_con, NM_SETTING_CONNECTION_MASTER, interface_name, NULL);
				nm_remote_connection_commit_changes_async (rcon, TRUE, NULL, NULL, NULL);
			}
			g_object_unref (rcon);
		} while (gtk_tree_model_iter_next (priv->connections_model, &iter));
	}
	return TRUE;
}

static void
ce_page_controller_init (CEPageController *self)
{
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);

	priv->new_ports = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
ce_page_controller_class_init (CEPageControllerClass *controller_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (controller_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (controller_class);

	g_type_class_add_private (object_class, sizeof (CEPageControllerPrivate));

	/* virtual methods */
	object_class->constructed = constructed;
	object_class->dispose = dispose;

	parent_class->ce_page_validate_v = ce_page_validate_v;
	parent_class->last_update = last_update;
}

gboolean
ce_page_controller_has_ports (CEPageController *self)
{
	CEPageControllerPrivate *priv = CE_PAGE_CONTROLLER_GET_PRIVATE (self);
	GtkTreeIter iter;

	return gtk_tree_model_get_iter_first (priv->connections_model, &iter);
}
