// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Copyright 2012 - 2014 Red Hat, Inc.
 */

#include "nm-default.h"

#include <stdlib.h>

#include "page-bridge.h"
#include "nm-connection-editor.h"
#include "connection-helpers.h"

G_DEFINE_TYPE (CEPageBridge, ce_page_bridge, CE_TYPE_PAGE_CONTROLLER)

#define CE_PAGE_BRIDGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CE_TYPE_PAGE_BRIDGE, CEPageBridgePrivate))

typedef struct {
	NMSettingBridge *setting;

	GtkWindow *toplevel;

	GtkSpinButton *ageing_time;
	GtkCheckButton *mcast_snoop;
	GtkCheckButton *stp;
	GtkSpinButton *priority;
	GtkSpinButton *forward_delay;
	GtkSpinButton *hello_time;
	GtkSpinButton *max_age;
	GtkSpinButton *group_fwd_mask;
	GtkLabel *group_fwd_mask_label;

} CEPageBridgePrivate;

/* The group_fwd_mask property is available in libnm 1.10, but since we only
 * require 1.8 at the moment, enable it only when detected at runtime.
 */
static gboolean group_fwd_mask_supported;

static void
bridge_private_init (CEPageBridge *self)
{
	CEPageBridgePrivate *priv = CE_PAGE_BRIDGE_GET_PRIVATE (self);
	GtkBuilder *builder;

	builder = CE_PAGE (self)->builder;

	priv->ageing_time = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "bridge_ageing_time"));
	priv->mcast_snoop = GTK_CHECK_BUTTON (gtk_builder_get_object (builder, "bridge_mcast_snoop_checkbox"));
	priv->stp = GTK_CHECK_BUTTON (gtk_builder_get_object (builder, "bridge_stp_checkbox"));
	priv->priority = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "bridge_priority"));
	priv->forward_delay = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "bridge_forward_delay"));
	priv->hello_time = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "bridge_hello_time"));
	priv->max_age = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "bridge_max_age"));
	priv->group_fwd_mask = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "bridge_group_fwd_mask"));
	priv->group_fwd_mask_label = GTK_LABEL (gtk_builder_get_object (builder, "bridge_group_fwd_mask_label"));

	priv->toplevel = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (priv->stp),
	                                                      GTK_TYPE_WINDOW));
}

static void
stuff_changed (GtkWidget *w, gpointer user_data)
{
	ce_page_changed (CE_PAGE (user_data));
}

static void
stp_toggled (GtkToggleButton *stp, gpointer user_data)
{
	CEPageBridge *self = user_data;
	CEPageBridgePrivate *priv = CE_PAGE_BRIDGE_GET_PRIVATE (self);

	if (gtk_toggle_button_get_active (stp)) {
		gtk_widget_set_sensitive (GTK_WIDGET (priv->priority), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (priv->forward_delay), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (priv->hello_time), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (priv->max_age), TRUE);
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET (priv->priority), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (priv->forward_delay), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (priv->hello_time), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (priv->max_age), FALSE);
	}
	ce_page_changed (CE_PAGE (user_data));
}

static void
populate_ui (CEPageBridge *self)
{
	CEPageBridgePrivate *priv = CE_PAGE_BRIDGE_GET_PRIVATE (self);
	NMSettingBridge *s_bridge = priv->setting;
	gboolean stp, mcast_snoop;
	int priority, forward_delay, hello_time, max_age, ageing_time;
	guint32 group_fwd_mask;

	gtk_widget_set_visible (GTK_WIDGET (priv->group_fwd_mask), group_fwd_mask_supported);
	gtk_widget_set_visible (GTK_WIDGET (priv->group_fwd_mask_label), group_fwd_mask_supported);

	/* Ageing time */
	ageing_time = nm_setting_bridge_get_ageing_time (s_bridge);
	gtk_spin_button_set_value (priv->ageing_time, (gdouble) ageing_time);
	g_signal_connect (priv->ageing_time, "value-changed",
	                  G_CALLBACK (stuff_changed),
	                  self);

	/* Multicast snooping */
	mcast_snoop = nm_setting_bridge_get_multicast_snooping (s_bridge);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->mcast_snoop), mcast_snoop);
	g_signal_connect (priv->mcast_snoop, "toggled", G_CALLBACK (stuff_changed), self);

	/* STP */
	g_signal_connect (priv->stp, "toggled",
	                  G_CALLBACK (stp_toggled),
	                  self);
	stp = nm_setting_bridge_get_stp (s_bridge);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->stp), stp);
	stp_toggled (GTK_TOGGLE_BUTTON (priv->stp), self);

	/* Priority */
	priority = nm_setting_bridge_get_priority (s_bridge);
	gtk_spin_button_set_value (priv->priority, (gdouble) priority);
	g_signal_connect (priv->priority, "value-changed",
	                  G_CALLBACK (stuff_changed),
	                  self);

	/* Forward delay */
	forward_delay = nm_setting_bridge_get_forward_delay (s_bridge);
	gtk_spin_button_set_value (priv->forward_delay, (gdouble) forward_delay);
	g_signal_connect (priv->forward_delay, "value-changed",
	                  G_CALLBACK (stuff_changed),
	                  self);

	/* Hello time */
	hello_time = nm_setting_bridge_get_hello_time (s_bridge);
	gtk_spin_button_set_value (priv->hello_time, (gdouble) hello_time);
	g_signal_connect (priv->hello_time, "value-changed",
	                  G_CALLBACK (stuff_changed),
	                  self);

	/* Max age */
	max_age = nm_setting_bridge_get_max_age (s_bridge);
	gtk_spin_button_set_value (priv->max_age, (gdouble) max_age);
	g_signal_connect (priv->max_age, "value-changed",
	                  G_CALLBACK (stuff_changed),
	                  self);

	/* Group forward mask */
	if (group_fwd_mask_supported) {
		g_object_get (s_bridge, "group-forward-mask", &group_fwd_mask, NULL);
		gtk_spin_button_set_value (priv->group_fwd_mask, (gdouble) group_fwd_mask);
		g_signal_connect (priv->group_fwd_mask, "value-changed",
		                  G_CALLBACK (stuff_changed),
		                  self);
	}
}

static void
create_connection (CEPageController *controller, NMConnection *connection)
{
	NMSetting *s_port;

	s_port = nm_connection_get_setting (connection, NM_TYPE_SETTING_BRIDGE_PORT);
	if (!s_port) {
		s_port = nm_setting_bridge_port_new ();
		nm_connection_add_setting (connection, s_port);
	}
}

static gboolean
connection_type_filter (FUNC_TAG_NEW_CONNECTION_TYPE_FILTER_IMPL,
                        GType type,
                        gpointer self)
{
	return nm_utils_check_virtual_device_compatibility (NM_TYPE_SETTING_BRIDGE, type);
}

static void
add_port (CEPageController *controller, NewConnectionResultFunc result_func)
{
	CEPageBridge *self = CE_PAGE_BRIDGE (controller);
	CEPageBridgePrivate *priv = CE_PAGE_BRIDGE_GET_PRIVATE (self);

	new_connection_dialog (priv->toplevel,
	                       CE_PAGE (self)->client,
	                       connection_type_filter,
	                       result_func,
	                       controller);
}

static void
finish_setup (CEPageBridge *self, gpointer user_data)
{
	populate_ui (self);
}

CEPage *
ce_page_bridge_new (NMConnectionEditor *editor,
                    NMConnection *connection,
                    GtkWindow *parent_window,
                    NMClient *client,
                    const char **out_secrets_setting_name,
                    GError **error)
{
	CEPageBridge *self;
	CEPageBridgePrivate *priv;

	self = CE_PAGE_BRIDGE (ce_page_new (CE_TYPE_PAGE_BRIDGE,
	                                  editor,
	                                  connection,
	                                  parent_window,
	                                  client,
	                                  "/org/gnome/nm_connection_editor/ce-page-bridge.ui",
	                                  "BridgePage",
	                                  _("Bridge")));
	if (!self) {
		g_set_error_literal (error, NMA_ERROR, NMA_ERROR_GENERIC,
		                     _("Could not load bridge user interface."));
		return NULL;
	}

	bridge_private_init (self);
	priv = CE_PAGE_BRIDGE_GET_PRIVATE (self);

	priv->setting = nm_connection_get_setting_bridge (connection);
	if (!priv->setting) {
		priv->setting = NM_SETTING_BRIDGE (nm_setting_bridge_new ());
		nm_connection_add_setting (connection, NM_SETTING (priv->setting));
	}

	g_signal_connect (self, CE_PAGE_INITIALIZED, G_CALLBACK (finish_setup), NULL);

	return CE_PAGE (self);
}

static void
ui_to_setting (CEPageBridge *self)
{
	CEPageBridgePrivate *priv = CE_PAGE_BRIDGE_GET_PRIVATE (self);
	int ageing_time, priority, forward_delay, hello_time, max_age, group_fwd_mask;
	gboolean stp, mcast_snoop;

	ageing_time = gtk_spin_button_get_value_as_int (priv->ageing_time);
	mcast_snoop = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->mcast_snoop));
	stp = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->stp));
	g_object_set (G_OBJECT (priv->setting),
	              NM_SETTING_BRIDGE_AGEING_TIME, ageing_time,
	              NM_SETTING_BRIDGE_MULTICAST_SNOOPING, mcast_snoop,
	              NM_SETTING_BRIDGE_STP, stp,
	              NULL);

	if (group_fwd_mask_supported) {
		group_fwd_mask = gtk_spin_button_get_value_as_int (priv->group_fwd_mask);
		g_object_set (G_OBJECT (priv->setting),
		              "group-forward-mask", (guint32) group_fwd_mask,
		              NULL);
	}

	if (stp) {
		priority = gtk_spin_button_get_value_as_int (priv->priority);
		forward_delay = gtk_spin_button_get_value_as_int (priv->forward_delay);
		hello_time = gtk_spin_button_get_value_as_int (priv->hello_time);
		max_age = gtk_spin_button_get_value_as_int (priv->max_age);

		g_object_set (G_OBJECT (priv->setting),
		              NM_SETTING_BRIDGE_PRIORITY, priority,
		              NM_SETTING_BRIDGE_FORWARD_DELAY, forward_delay,
		              NM_SETTING_BRIDGE_HELLO_TIME, hello_time,
		              NM_SETTING_BRIDGE_MAX_AGE, max_age,
		              NULL);
	}
}

static gboolean
ce_page_validate_v (CEPage *page, NMConnection *connection, GError **error)
{
	CEPageBridge *self = CE_PAGE_BRIDGE (page);
	CEPageBridgePrivate *priv = CE_PAGE_BRIDGE_GET_PRIVATE (self);

	if (!CE_PAGE_CLASS (ce_page_bridge_parent_class)->ce_page_validate_v (page, connection, error))
		return FALSE;

	ui_to_setting (self);
	return nm_setting_verify (NM_SETTING (priv->setting), connection, error);
}

static void
ce_page_bridge_init (CEPageBridge *self)
{
}

static void
ce_page_bridge_class_init (CEPageBridgeClass *bridge_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (bridge_class);
	CEPageClass *parent_class = CE_PAGE_CLASS (bridge_class);
	CEPageControllerClass *controller_class = CE_PAGE_CONTROLLER_CLASS (bridge_class);
	GObjectClass *setting_class;

	g_type_class_add_private (object_class, sizeof (CEPageBridgePrivate));

	/* virtual methods */
	parent_class->ce_page_validate_v = ce_page_validate_v;
	controller_class->create_connection = create_connection;
	controller_class->add_port = add_port;

	/* check whether libnm supports the bridge.group-forward-mask property */
	setting_class = g_type_class_ref (NM_TYPE_SETTING_BRIDGE);
	group_fwd_mask_supported = !!g_object_class_find_property (setting_class,
	                                                           "group-forward-mask");
	g_type_class_unref (setting_class);
}

void
bridge_connection_new (FUNC_TAG_PAGE_NEW_CONNECTION_IMPL,
                       GtkWindow *parent,
                       const char *detail,
                       gpointer detail_data,
                       NMConnection *connection,
                       NMClient *client,
                       PageNewConnectionResultFunc result_func,
                       gpointer user_data)
{
	NMSettingConnection *s_con;
	int bridge_num = 0, num, i;
	const GPtrArray *connections;
	NMConnection *conn2;
	const char *iface;
	char *my_iface;
	gs_unref_object NMConnection *connection_tmp = NULL;

	connection = _ensure_connection_other (connection, &connection_tmp);
	ce_page_complete_connection (connection,
	                             _("Bridge connection %d"),
	                             NM_SETTING_BRIDGE_SETTING_NAME,
	                             TRUE,
	                             client);
	nm_connection_add_setting (connection, nm_setting_bridge_new ());

	/* Find an available interface name */
	connections = nm_client_get_connections (client);
	for (i = 0; i < connections->len; i++) {
		conn2 = connections->pdata[i];

		if (!nm_connection_is_type (conn2, NM_SETTING_BRIDGE_SETTING_NAME))
			continue;
		iface = nm_connection_get_interface_name (conn2);
		if (!iface || strncmp (iface, "bridge", 6) != 0 || !g_ascii_isdigit (iface[6]))
			continue;

		num = atoi (iface + 6);
		if (bridge_num <= num)
			bridge_num = num + 1;
	}

	s_con = nm_connection_get_setting_connection (connection);
	my_iface = g_strdup_printf ("bridge%d", bridge_num);
	g_object_set (G_OBJECT (s_con),
	              NM_SETTING_CONNECTION_INTERFACE_NAME, my_iface,
	              NULL);
	g_free (my_iface);

	(*result_func) (FUNC_TAG_PAGE_NEW_CONNECTION_RESULT_CALL, connection, FALSE, NULL, user_data);
}
