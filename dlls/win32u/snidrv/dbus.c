/*
 * DBus tray support
 *
 * Copyright 2023 Sergei Chernyadyev
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#ifdef SONAME_LIBDBUS_1
#include "snidrv.h"

#include <pthread.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>
#include <dlfcn.h>
#include <poll.h>

#include <dbus/dbus.h>

#include "wine/list.h"
#include "wine/unixlib.h"
#include "wine/gdi_driver.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(winesni);

#define DBUS_FUNCS               \
    DO_FUNC(dbus_bus_add_match); \
    DO_FUNC(dbus_bus_get); \
    DO_FUNC(dbus_bus_get_private); \
    DO_FUNC(dbus_bus_add_match); \
    DO_FUNC(dbus_bus_remove_match); \
    DO_FUNC(dbus_bus_get_unique_name);   \
    DO_FUNC(dbus_connection_add_filter); \
    DO_FUNC(dbus_connection_read_write); \
    DO_FUNC(dbus_connection_dispatch); \
    DO_FUNC(dbus_connection_get_dispatch_status); \
    DO_FUNC(dbus_connection_read_write_dispatch); \
    DO_FUNC(dbus_connection_remove_filter); \
    DO_FUNC(dbus_connection_send); \
    DO_FUNC(dbus_connection_send_with_reply); \
    DO_FUNC(dbus_connection_send_with_reply_and_block); \
    DO_FUNC(dbus_connection_flush); \
    DO_FUNC(dbus_connection_try_register_object_path);  \
    DO_FUNC(dbus_connection_unregister_object_path);  \
    DO_FUNC(dbus_connection_list_registered);  \
    DO_FUNC(dbus_connection_close);  \
    DO_FUNC(dbus_connection_ref);   \
    DO_FUNC(dbus_connection_unref);   \
    DO_FUNC(dbus_connection_get_object_path_data);  \
    DO_FUNC(dbus_connection_set_watch_functions);   \
    DO_FUNC(dbus_watch_get_unix_fd); \
    DO_FUNC(dbus_watch_handle); \
    DO_FUNC(dbus_watch_get_flags); \
    DO_FUNC(dbus_watch_get_enabled); \
    DO_FUNC(dbus_error_free); \
    DO_FUNC(dbus_error_init); \
    DO_FUNC(dbus_error_is_set); \
    DO_FUNC(dbus_set_error_from_message); \
    DO_FUNC(dbus_free_string_array); \
    DO_FUNC(dbus_message_get_args); \
    DO_FUNC(dbus_message_get_interface); \
    DO_FUNC(dbus_message_get_member); \
    DO_FUNC(dbus_message_get_path); \
    DO_FUNC(dbus_message_get_type); \
    DO_FUNC(dbus_message_is_signal); \
    DO_FUNC(dbus_message_iter_append_basic); \
    DO_FUNC(dbus_message_iter_get_arg_type); \
    DO_FUNC(dbus_message_iter_get_basic); \
    DO_FUNC(dbus_message_iter_append_fixed_array); \
    DO_FUNC(dbus_message_iter_get_fixed_array); \
    DO_FUNC(dbus_message_iter_init); \
    DO_FUNC(dbus_message_iter_init_append); \
    DO_FUNC(dbus_message_iter_next); \
    DO_FUNC(dbus_message_iter_recurse); \
    DO_FUNC(dbus_message_iter_open_container);  \
    DO_FUNC(dbus_message_iter_close_container);  \
    DO_FUNC(dbus_message_iter_abandon_container_if_open);  \
    DO_FUNC(dbus_message_new_method_return);  \
    DO_FUNC(dbus_message_new_method_call); \
    DO_FUNC(dbus_message_new_signal); \
    DO_FUNC(dbus_message_is_method_call);  \
    DO_FUNC(dbus_message_new_error);  \
    DO_FUNC(dbus_pending_call_block); \
    DO_FUNC(dbus_pending_call_unref); \
    DO_FUNC(dbus_pending_call_steal_reply); \
    DO_FUNC(dbus_threads_init_default); \
    DO_FUNC(dbus_message_unref)

#define DO_FUNC(f) static typeof(f) * p_##f
DBUS_FUNCS;
#undef DO_FUNC

/* an individual systray icon */
struct tray_icon
{
    struct list     entry;
    HWND            owner;    /* the HWND passed in to the Shell_NotifyIcon call */
    HICON           hIcon;
    void*           icon_bitmap;
    UINT            icon_width;
    UINT            icon_height;
    UINT            state;    /* state flags */
    UINT            id;       /* the unique id given by the app */
    UINT            callback_message;
    char            tiptext[128 * 3];    /* tooltip text */
    UINT            version;         /* notify icon api version */
    unsigned int    notification_id;
    DBusConnection* connection;
    DBusWatch* watch;
    int watch_fd;
    UINT watch_flags;
    pthread_mutex_t mutex; /* mutex */
};

static pthread_once_t init_control = PTHREAD_ONCE_INIT;

static struct list sni_list = LIST_INIT( sni_list );

static pthread_mutex_t list_mutex;

struct standalone_notification {
    struct list entry;

    HWND owner;
    UINT id;
    unsigned int notification_id;
};

static struct list standalone_notification_list = LIST_INIT( standalone_notification_list );

static pthread_mutex_t standalone_notifications_mutex = PTHREAD_MUTEX_INITIALIZER;


#define BALLOON_SHOW_MIN_TIMEOUT 10000
#define BALLOON_SHOW_MAX_TIMEOUT 30000

static void* dbus_module = NULL;
static const char* watcher_interface_name = "org.kde.StatusNotifierWatcher";
static const char* item_interface_name = "org.kde.StatusNotifierItem";

static const char* notifications_interface_name = "org.freedesktop.Notifications";

static DBusConnection *global_connection;
static DBusWatch *global_connection_watch;
static int global_connection_watch_fd;
static UINT global_connection_watch_flags;
static BOOL sni_initialized = FALSE;
static BOOL notifications_initialized = FALSE;

static char* status_notifier_dst_path = NULL;
static char* notifications_dst_path = NULL;

static const char *status_field = "Status";
static const char *icon_field = "IconPixmap";
static const char *icon_name_field = "IconName";
static const char *title_field = "Title";
static const char *category_field = "Category";
static const char *id_field = "Id";

static BOOL get_notifier_watcher_owner(void);

static BOOL load_dbus_functions(void)
{
    if (!(dbus_module = dlopen( SONAME_LIBDBUS_1, RTLD_NOW )))
        goto failed;

#define DO_FUNC(f) if (!(p_##f = dlsym( dbus_module, #f ))) goto failed
    DBUS_FUNCS;
#undef DO_FUNC
    return TRUE;

failed:
    WARN( "failed to load DBUS support: %s\n", dlerror() );
    return FALSE;
}

static void sni_finalize(void)
{
    pthread_mutex_destroy(&list_mutex);
    free(status_notifier_dst_path);
}

static void notifications_finalize(void)
{
    free(notifications_dst_path);
}

static void dbus_finalize(void)
{
    if (global_connection != NULL)
    {
        p_dbus_connection_flush(global_connection);
        p_dbus_connection_close(global_connection);
        p_dbus_connection_unref(global_connection);
    }
    if (dbus_module != NULL)
    {
        dlclose(dbus_module);
    }
}

static dbus_bool_t add_watch(DBusWatch *w, void *data);
static void remove_watch(DBusWatch *w, void *data);
static void toggle_watch(DBusWatch *w, void *data);

static BOOL notifications_initialize(void);

static BOOL dbus_initialize(void)
{
    DBusError error;
    p_dbus_error_init( &error );
    if (!p_dbus_threads_init_default()) return FALSE;
    if (!(global_connection = p_dbus_bus_get_private( DBUS_BUS_SESSION, &error )))
    {
        WARN("failed to get system dbus connection: %s\n", error.message );
        p_dbus_error_free( &error );
        return FALSE;
    }

    if (!p_dbus_connection_set_watch_functions(global_connection, add_watch, remove_watch,
                                               toggle_watch, NULL, NULL))
    {
        WARN("dbus_set_watch_functions() failed\n");
        return FALSE;
    }
    return TRUE;
}

static void snidrv_once_initialize(void)
{
    if (!load_dbus_functions()) goto err;
    if (!dbus_initialize()) goto err;
    if (get_notifier_watcher_owner())
        /* TODO: replace this with Interlocked if there will be a getter function for this variable */
        sni_initialized = TRUE;
    if (notifications_initialize())
        notifications_initialized = TRUE;
err:
    if (!sni_initialized)
        sni_finalize();
    if (!notifications_initialized)
        notifications_finalize();
    if (!sni_initialized && !notifications_initialized)
        dbus_finalize();
}

BOOL snidrv_init(void)
{
    pthread_once(&init_control, snidrv_once_initialize);
    return sni_initialized;
}

BOOL snidrv_notification_init(void)
{
    pthread_once(&init_control, snidrv_once_initialize);
    return notifications_initialized;
}

static dbus_bool_t add_watch(DBusWatch *w, void *data)
{
    int fd;
    unsigned int flags, poll_flags;
    if (!p_dbus_watch_get_enabled(w))
        return TRUE;

    fd = p_dbus_watch_get_unix_fd(w);
    flags = p_dbus_watch_get_flags(w);
    poll_flags = 0;

    if (flags & DBUS_WATCH_READABLE)
        poll_flags |= POLLIN;
    if (flags & DBUS_WATCH_WRITABLE)
        poll_flags |= POLLOUT;

    pthread_mutex_lock(&list_mutex);
    if (data != NULL)
    {
        struct tray_icon *icon = (struct tray_icon *) data;
        icon->watch_fd = fd;
        icon->watch_flags = poll_flags;
        icon->watch = w;
    }
    else
    {
        /* global connection */
        global_connection_watch_fd = fd;
        global_connection_watch_flags = poll_flags;
        global_connection_watch = w;
    }
    pthread_mutex_unlock(&list_mutex);

    return TRUE;
}

static void remove_watch(DBusWatch *w, void *data)
{
    pthread_mutex_lock(&list_mutex);
    if (data != NULL)
    {
        struct tray_icon *icon = (struct tray_icon *) data;
        icon->watch_fd = 0;
        icon->watch_flags = 0;
        icon->watch = NULL;
    }
    else
    {
        /* global connection */
        global_connection_watch_fd = 0;
        global_connection_watch_flags = 0;
        global_connection_watch = NULL;
    }
    pthread_mutex_unlock(&list_mutex);
}

static void toggle_watch(DBusWatch *w, void *data)
{
    if (p_dbus_watch_get_enabled(w))
        add_watch(w, data);
    else
        remove_watch(w, data);
}

static const char* dbus_name_owning_match = "type='signal',"
    "interface='org.freedesktop.DBus',"
    "sender='org.freedesktop.DBus',"
    "member='NameOwnerChanged'";

static const char* dbus_notification_close_signal = "type='signal',"
    "interface='org.freedesktop.Notifications',"
    "member='NotificationClosed'";

static const char* const object_path = "/StatusNotifierItem";

static BOOL register_notification_item(DBusConnection* ctx);

static void restore_items(DBusConnection *ctx)
{
    struct tray_icon *icon;

    LIST_FOR_EACH_ENTRY( icon, &sni_list, struct tray_icon, entry )
        register_notification_item(icon->connection);
}

static DBusHandlerResult name_owner_filter( DBusConnection *ctx, DBusMessage *msg, void *user_data )
{
    char *interface_name, *old_path, *new_path;
    DBusError error;

    p_dbus_error_init( &error );

    if (p_dbus_message_is_signal( msg, "org.freedesktop.DBus", "NameOwnerChanged" ) &&
        p_dbus_message_get_args( msg, &error, DBUS_TYPE_STRING, &interface_name, DBUS_TYPE_STRING, &old_path,
                                 DBUS_TYPE_STRING, &new_path, DBUS_TYPE_INVALID ))
    {
        /* check if watcher is disabled first*/
        if (strcmp(interface_name, watcher_interface_name) == 0)
        {
            /* TODO: lock the mutex */
            if (status_notifier_dst_path == NULL || status_notifier_dst_path[0] == '\0')
            {
                pthread_mutex_lock(&list_mutex);
                /* switch between KDE and freedesktop interfaces, despite most implementations rely on KDE interface names */

                old_path = status_notifier_dst_path;
                status_notifier_dst_path = strdup(new_path);
                free(old_path);

                if (status_notifier_dst_path != NULL && status_notifier_dst_path[0] != '\0')
                    restore_items(ctx);

                pthread_mutex_unlock(&list_mutex);
            }
            else if (status_notifier_dst_path != NULL &&
                     status_notifier_dst_path[0] != '\0' &&
                     strcmp(interface_name, watcher_interface_name) == 0)
            {
                pthread_mutex_lock(&list_mutex);
                old_path = status_notifier_dst_path;
                status_notifier_dst_path = strdup(new_path);
                free(old_path);
                pthread_mutex_lock(&list_mutex);
            }
        }
        else if (strcmp(interface_name, notifications_interface_name) == 0)
        {
            struct standalone_notification *this, *next;
            pthread_mutex_lock(&standalone_notifications_mutex);
            old_path = notifications_dst_path;
            notifications_dst_path = strdup(new_path);
            free(old_path);

            LIST_FOR_EACH_ENTRY_SAFE( this, next, &standalone_notification_list, struct standalone_notification, entry )
            {
                list_remove(&this->entry);
                free(this);
            }
            pthread_mutex_unlock(&standalone_notifications_mutex);
        }
    }
    else if (p_dbus_message_is_signal( msg, notifications_interface_name, "NotificationClosed" ))
    {
        unsigned int id, reason;
        struct standalone_notification *this, *next;
        if (!p_dbus_message_get_args( msg, &error, DBUS_TYPE_UINT32, &id, DBUS_TYPE_UINT32, &reason ))
            goto cleanup;
        pthread_mutex_lock(&standalone_notifications_mutex);
        /* TODO: clear the list */
        LIST_FOR_EACH_ENTRY_SAFE( this, next, &standalone_notification_list, struct standalone_notification, entry )
        {
            if (this->notification_id == id)
            {
                list_remove(&this->entry);
                free(this);
            }
        }
        pthread_mutex_unlock(&standalone_notifications_mutex);
    }

cleanup:
    p_dbus_error_free( &error );
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static BOOL get_owner_for_interface(DBusConnection* connection, const char* interface_name, char** owner_path)
{
    DBusMessage* msg = NULL;
    DBusMessageIter args;
    DBusPendingCall* pending;
    DBusError error;
    char* status_notifier_dest = NULL;
    p_dbus_error_init( &error );
    msg = p_dbus_message_new_method_call("org.freedesktop.DBus",
                                         "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus",
                                         "GetNameOwner");
    if (!msg) goto err;

    p_dbus_message_iter_init_append(msg, &args);
    if (!p_dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface_name )) goto err;
    if (!p_dbus_connection_send_with_reply (connection, msg, &pending, -1)) goto err;
    if (!pending) goto err;

    p_dbus_message_unref(msg);

    p_dbus_pending_call_block(pending);

    msg = p_dbus_pending_call_steal_reply(pending);
    p_dbus_pending_call_unref(pending);
    if (!msg) goto err;

    if (p_dbus_set_error_from_message (&error, msg))
    {
        WARN("failed to query an owner - %s: %s\n", error.name, error.message);
        p_dbus_error_free( &error);
        goto err;
    }
    else if (!p_dbus_message_get_args( msg, &error, DBUS_TYPE_STRING, &status_notifier_dest,
                                         DBUS_TYPE_INVALID ))
    {
        WARN("failed to get a response - %s: %s\n", error.name, error.message);
        p_dbus_error_free( &error );
        goto err;
    }
    *owner_path = strdup(status_notifier_dest);
    p_dbus_message_unref(msg);
    return TRUE;
err:
    p_dbus_message_unref(msg);
    return FALSE;
}

static BOOL get_notifier_watcher_owner_for_interface(DBusConnection* connection, const char* interface_name, const char* sni_interface_name)
{
    if (!get_owner_for_interface(connection, interface_name, &status_notifier_dst_path))
        return FALSE;
    TRACE("found notifier destination name %s\n", status_notifier_dst_path);
    watcher_interface_name = interface_name;
    item_interface_name = sni_interface_name;
    return TRUE;
}

static BOOL get_notifier_watcher_owner(void)
{
    DBusError error;
    pthread_mutexattr_t attr;
    p_dbus_error_init( &error );

    pthread_mutexattr_init( &attr );
    pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
    pthread_mutex_init( &list_mutex, &attr );

    if (!get_notifier_watcher_owner_for_interface(global_connection, watcher_interface_name, item_interface_name))
    {
        WARN("failed to query watcher interface owner\n");
        goto err;
    }

    p_dbus_connection_add_filter( global_connection, name_owner_filter, NULL, NULL );
    p_dbus_bus_add_match( global_connection, dbus_name_owning_match, &error );
    p_dbus_bus_add_match( global_connection, dbus_notification_close_signal, &error );
    if (p_dbus_error_is_set(&error))
    {
        WARN("failed to register matcher %s: %s\n", error.name, error.message);
        p_dbus_error_free( &error);
        goto err;
    }
    pthread_mutexattr_destroy( &attr );
    return TRUE;

err:
    pthread_mutexattr_destroy( &attr );
    return FALSE;
}

static BOOL notifications_initialize(void)
{
    char* dst_path = NULL;
    BOOL ret = get_owner_for_interface(global_connection, "org.freedesktop.Notifications", &dst_path);
    notifications_dst_path = dst_path;
    return ret;
}

static BOOL handle_notification_icon(DBusMessageIter *iter, const unsigned char* icon_bits, unsigned width, unsigned height)
{
    DBusMessageIter sIter,bIter;
    unsigned row_stride = width * 4;
    const unsigned channel_count = 4;
    const unsigned bits_per_sample = 8;
    const bool has_alpha = true;
    if (!p_dbus_message_iter_open_container(iter, 'r', NULL, &sIter))
    {
        WARN("Failed to open struct inside array!\n");
        goto fail;
    }

    p_dbus_message_iter_append_basic(&sIter, 'i', &width);
    p_dbus_message_iter_append_basic(&sIter, 'i', &height);
    p_dbus_message_iter_append_basic(&sIter, 'i', &row_stride);
    p_dbus_message_iter_append_basic(&sIter, 'b', &has_alpha);
    p_dbus_message_iter_append_basic(&sIter, 'i', &bits_per_sample);
    p_dbus_message_iter_append_basic(&sIter, 'i', &channel_count);

    if (p_dbus_message_iter_open_container(&sIter, 'a', DBUS_TYPE_BYTE_AS_STRING, &bIter))
    {
        p_dbus_message_iter_append_fixed_array(&bIter, DBUS_TYPE_BYTE, &icon_bits, width * height * 4);
        p_dbus_message_iter_close_container(&sIter, &bIter);
    }
    else
    {
        p_dbus_message_iter_abandon_container_if_open(iter, &sIter);
        goto fail;
    }
    p_dbus_message_iter_close_container(iter, &sIter);
    return TRUE;
fail:
    return FALSE;
}

static BOOL close_notification(DBusConnection* connection, UINT id)
{
    BOOL ret = FALSE;

    DBusMessage* msg = NULL;
    DBusMessageIter args;
    DBusPendingCall* pending;
    DBusError error;

    p_dbus_error_init( &error );
    msg = p_dbus_message_new_method_call(notifications_dst_path,
                                         "/org/freedesktop/Notifications",
                                         notifications_interface_name,
                                         "CloseNotification");
    if (!msg) goto err;
    p_dbus_message_iter_init_append(msg, &args);
    if (!p_dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &id )) goto err;

    if (!p_dbus_connection_send_with_reply (connection, msg, &pending, -1))
        goto err;

    if (!pending) goto err;

    p_dbus_message_unref(msg);

    p_dbus_pending_call_block(pending);

    msg = p_dbus_pending_call_steal_reply(pending);
    p_dbus_pending_call_unref(pending);
    if (!msg) goto err;
    if (p_dbus_set_error_from_message (&error, msg))
    {
        WARN("got an error - %s: %s\n", error.name, error.message);
        p_dbus_error_free( &error);
    }
    ret = TRUE;
err:
    p_dbus_message_unref(msg);

    return ret;
}

static BOOL send_notification(DBusConnection* connection, UINT id, const WCHAR* title, const WCHAR* text, HICON icon, UINT info_flags, UINT timeout, unsigned int *p_new_id)
{
    char info_text[256 * 3];
    char info_title[128 * 3];
    const char *info_text_ptr = info_text, *info_title_ptr = info_title;
    const char* empty_string = "";
    const char* icon_name = "";
    BOOL ret = FALSE;
    DBusMessage* msg = NULL;
    DBusMessageIter args, aIter, eIter, vIter;
    DBusPendingCall* pending;
    DBusError error;
    /* icon */
    void* icon_bits = NULL;
    unsigned width, height;
    HICON new_icon = NULL;
    int expire_timeout;
    /* no text for balloon, so no balloon  */
    if (!text || !text[0])
        return TRUE;

    info_title[0] = 0;
    info_text[0] = 0;
    if (title) ntdll_wcstoumbs(title, wcslen(title) + 1, info_title, ARRAY_SIZE(info_title), FALSE);
    if (text) ntdll_wcstoumbs(text, wcslen(text) + 1, info_text, ARRAY_SIZE(info_text), FALSE);
    /*icon*/
    if ((info_flags & NIIF_ICONMASK) == NIIF_USER && icon)
    {
        unsigned int *u_icon_bits;
        new_icon = CopyImage(icon, IMAGE_ICON, 0, 0, 0);
        if (!create_bitmap_from_icon(new_icon, &width, &height, &icon_bits))
        {
            WARN("failed to copy icon %p\n", new_icon);
            goto err;
        }
        u_icon_bits = icon_bits;
        /* convert to RGBA, turns out that unlike tray icons it needs RGBA */
        for (unsigned i = 0; i < width * height; i++)
        {
#ifdef WORDS_BIGENDIAN
            u_icon_bits[i] = (u_icon_bits[i] << 8) | (u_icon_bits[i] >> 24);
#else
            u_icon_bits[i] = (u_icon_bits[i] << 24) | (u_icon_bits[i] >> 8);
#endif
        }
    }
    else
    {
        /* show placeholder icons */
        switch (info_flags & NIIF_ICONMASK)
        {
        case NIIF_INFO:
            icon_name = "dialog-information";
            break;
        case NIIF_WARNING:
            icon_name = "dialog-warning";
            break;
        case NIIF_ERROR:
            icon_name = "dialog-error";
            break;
        default:
            break;
        }
    }
    p_dbus_error_init( &error );
    msg = p_dbus_message_new_method_call(notifications_dst_path,
                                         "/org/freedesktop/Notifications",
                                         notifications_interface_name,
                                         "Notify");
    if (!msg) goto err;

    p_dbus_message_iter_init_append(msg, &args);
    if (!p_dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &empty_string ))
        goto err;
    /* override id */
    if (!p_dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &id ))
        goto err;
    /* icon name */
    if (!p_dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &icon_name ))
        goto err;
    /* title */
    if (!p_dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info_title_ptr ))
        goto err;
    /* body */
    if (!p_dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info_text_ptr ))
        goto err;
    /* actions */
    /* TODO: add default action */
    if (p_dbus_message_iter_open_container(&args, 'a', DBUS_TYPE_STRING_AS_STRING, &aIter))
        p_dbus_message_iter_close_container(&args, &aIter);
    else
        goto err;

    /* hints */
    if (p_dbus_message_iter_open_container(&args, 'a', "{sv}", &aIter))
    {
        if ((info_flags & NIIF_ICONMASK) == NIIF_USER && icon)
        {
            const char* icon_data_field = "image-data";
            if (!p_dbus_message_iter_open_container(&aIter, 'e', NULL, &eIter))
            {
                p_dbus_message_iter_abandon_container_if_open(&args, &aIter);
                goto err;
            }

            p_dbus_message_iter_append_basic(&eIter, 's', &icon_data_field);

            if (!p_dbus_message_iter_open_container(&eIter, 'v', "(iiibiiay)", &vIter))
            {
                p_dbus_message_iter_abandon_container_if_open(&aIter, &eIter);
                p_dbus_message_iter_abandon_container_if_open(&args, &aIter);
                goto err;
            }

            if (!handle_notification_icon(&vIter, icon_bits, width, height))
            {
                p_dbus_message_iter_abandon_container_if_open(&eIter, &vIter);
                p_dbus_message_iter_abandon_container_if_open(&aIter, &eIter);
                p_dbus_message_iter_abandon_container_if_open(&args, &aIter);
                goto err;
            }

            p_dbus_message_iter_close_container(&eIter, &vIter);
            p_dbus_message_iter_close_container(&aIter, &eIter);
        }
        p_dbus_message_iter_close_container(&args, &aIter);
    }
    else
        goto err;
    if (timeout == 0)
        /* just set it to system default */
        expire_timeout = -1;
    else
        expire_timeout = max(min(timeout, BALLOON_SHOW_MAX_TIMEOUT), BALLOON_SHOW_MIN_TIMEOUT);

    /* timeout */
    if (!p_dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &expire_timeout ))
        goto err;
    if (!p_dbus_connection_send_with_reply (connection, msg, &pending, -1))
        goto err;
    if (!pending) goto err;

    p_dbus_message_unref(msg);

    p_dbus_pending_call_block(pending);

    msg = p_dbus_pending_call_steal_reply(pending);
    p_dbus_pending_call_unref(pending);
    if (!msg) goto err;

    if (p_dbus_set_error_from_message (&error, msg))
    {
        WARN("failed to create a notification - %s: %s\n", error.name, error.message);
        p_dbus_error_free( &error);
        goto err;
    }
    if (!p_dbus_message_iter_init(msg, &args))
        goto err;

    if (DBUS_TYPE_UINT32 != p_dbus_message_iter_get_arg_type(&args))
        goto err;
    else if (p_new_id)
        p_dbus_message_iter_get_basic(&args, p_new_id);
    ret = TRUE;
err:
    p_dbus_message_unref(msg);
    if (new_icon) NtUserDestroyCursor(new_icon, 0);
    free(icon_bits);
    return ret;
}

static BOOL handle_id(DBusConnection* conn, DBusMessageIter *iter, const struct tray_icon* icon)
{
    char id[64];
    const char* id_ptr = id;
    snprintf(id, 64, "wine_tray_%p_%d", (void*)icon->owner, icon->id);
    p_dbus_message_iter_append_basic(iter, 's', &id_ptr);
    return true;
}

static bool handle_icon_name(DBusConnection* conn, DBusMessageIter *iter)
{
    const char* icon_name = "wine_tray_icon";
    return p_dbus_message_iter_append_basic(iter, 's', &icon_name);
}

static BOOL handle_icon(DBusConnection* conn, DBusMessageIter *iter, const struct tray_icon* icon)
{
    DBusMessageIter aIter, sIter,bIter;
    if (!p_dbus_message_iter_open_container(iter, 'a', "(iiay)", &aIter))
    {
        WARN("Failed to open array!\n");
        goto fail;
    }

    if (!p_dbus_message_iter_open_container(&aIter, 'r', NULL, &sIter))
    {
        WARN("Failed to open struct inside array!\n");
        p_dbus_message_iter_abandon_container_if_open(iter, &aIter);
        goto fail;
    }

    p_dbus_message_iter_append_basic(&sIter, 'i', &icon->icon_width);
    p_dbus_message_iter_append_basic(&sIter, 'i', &icon->icon_height);

    if (p_dbus_message_iter_open_container(&sIter, 'a', DBUS_TYPE_BYTE_AS_STRING, &bIter))
    {
        p_dbus_message_iter_append_fixed_array(&bIter, DBUS_TYPE_BYTE, &icon->icon_bitmap, icon->icon_width * icon->icon_height * 4);
        p_dbus_message_iter_close_container(&sIter, &bIter);
    }
    else
        goto fail;

    p_dbus_message_iter_close_container(&aIter, &sIter);
    p_dbus_message_iter_close_container(iter, &aIter);
    return TRUE;
fail:
    return FALSE;
}

static void handle_title(DBusConnection* conn, DBusMessageIter *iter, const struct tray_icon* icon)
{
    const char* tiptext = (const char*)icon->tiptext;
    p_dbus_message_iter_append_basic(iter, 's', &tiptext);
}

static void handle_category(DBusConnection* conn, DBusMessageIter *iter)
{
    const char *cat = "ApplicationStatus";
    p_dbus_message_iter_append_basic(iter, 's', &cat);
}

static void handle_status(DBusConnection* conn, DBusMessageIter *iter, const struct tray_icon* icon)
{
    const char *status = (icon->state & NIS_HIDDEN) ? "Passive" : "Active";
    p_dbus_message_iter_append_basic(iter, 's', &status);
}

static BOOL notify_owner( const struct tray_icon *icon, UINT msg, unsigned short x, unsigned short y)
{
    WPARAM wp = icon->id;
    LPARAM lp = msg;

    if (icon->version >= NOTIFYICON_VERSION_4)
    {
        POINT pt = { x, y };

        wp = MAKEWPARAM( pt.x, pt.y );
        lp = MAKELPARAM( msg, icon->id );
    }

    TRACE( "relaying 0x%x\n", msg );
    if (!NtUserMessageCall( icon->owner, icon->callback_message, wp, lp, 0, NtUserSendNotifyMessage, FALSE ))
    {
        WARN( "application window was destroyed, removing icon %u\n", icon->id );
        /* TODO: delete the icon */
        return FALSE;
    }
    return TRUE;
}

static DBusHandlerResult notification_send_error(DBusConnection *conn, DBusMessage *message, const char* error, const char* message_text)
{
    DBusMessage* reply;
    unsigned serial = 0;
    reply = p_dbus_message_new_error (message, error, message_text);
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;

    if (!p_dbus_connection_send(conn, reply, &serial))
    {
        p_dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    p_dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

DBusHandlerResult get_all_tray_properties(DBusConnection* conn, DBusMessage *message, const struct tray_icon* icon)
{
    DBusMessage* reply;
    DBusMessageIter iter, aIter, vIter;
    DBusMessageIter eIter;
    unsigned serial = 0;
    reply = p_dbus_message_new_method_return(message);
    if (!reply) goto fail;

    p_dbus_message_iter_init_append(reply, &iter);
    pthread_mutex_lock((pthread_mutex_t*)&icon->mutex);

    if (!p_dbus_message_iter_open_container(&iter, 'a', "{sv}", &aIter)) goto fail;

    if (!p_dbus_message_iter_open_container(&aIter, 'e', NULL, &eIter))
    {
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    p_dbus_message_iter_append_basic(&eIter, 's', &id_field);

    if (!p_dbus_message_iter_open_container(&eIter, 'v', "s", &vIter))
    {
        p_dbus_message_iter_abandon_container_if_open(&aIter, &eIter);
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    handle_id(conn, &vIter, icon);

    p_dbus_message_iter_close_container(&eIter, &vIter);
    p_dbus_message_iter_close_container(&aIter, &eIter);

    if (!p_dbus_message_iter_open_container(&aIter, 'e', NULL, &eIter))
    {
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    p_dbus_message_iter_append_basic(&eIter, 's', &category_field);

    if (!p_dbus_message_iter_open_container(&eIter, 'v', "s", &vIter))
    {
        p_dbus_message_iter_abandon_container_if_open(&aIter, &eIter);
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    handle_category(conn, &vIter);

    p_dbus_message_iter_close_container(&eIter, &vIter);
    p_dbus_message_iter_close_container(&aIter, &eIter);
    /* Title */
    if (!p_dbus_message_iter_open_container(&aIter, 'e', NULL, &eIter))
    {
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    p_dbus_message_iter_append_basic(&eIter, 's', &title_field);

    if (!p_dbus_message_iter_open_container(&eIter, 'v', "s", &vIter))
    {
        p_dbus_message_iter_abandon_container_if_open(&aIter, &eIter);
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    handle_title(conn, &vIter, icon);

    p_dbus_message_iter_close_container(&eIter, &vIter);
    p_dbus_message_iter_close_container(&aIter, &eIter);
    /* status */
    if (!p_dbus_message_iter_open_container(&aIter, 'e', NULL, &eIter))
    {
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    p_dbus_message_iter_append_basic(&eIter, 's', &status_field);

    if (!p_dbus_message_iter_open_container(&eIter, 'v', "s", &vIter))
    {
        p_dbus_message_iter_abandon_container_if_open(&aIter, &eIter);
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    handle_status(conn, &vIter, icon);

    p_dbus_message_iter_close_container(&eIter, &vIter);
    p_dbus_message_iter_close_container(&aIter, &eIter);

    if (icon->icon_bitmap != NULL)
    {
        /* Icon */
        if (!p_dbus_message_iter_open_container(&aIter, 'e', NULL, &eIter))
        {
            p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
            goto fail;
        }

        p_dbus_message_iter_append_basic(&eIter, 's', &icon_field);

        if (!p_dbus_message_iter_open_container(&eIter, 'v', "a(iiay)", &vIter))
        {
            WARN("failed to create iconpixmap array\n");
            p_dbus_message_iter_abandon_container_if_open(&aIter, &eIter);
            p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
            goto fail;
        }

        handle_icon(conn, &vIter, icon);

        p_dbus_message_iter_close_container(&eIter, &vIter);
        p_dbus_message_iter_close_container(&aIter, &eIter);
    }

    if (!p_dbus_message_iter_open_container(&aIter, 'e', NULL, &eIter))
    {
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    p_dbus_message_iter_append_basic(&eIter, 's', &icon_name_field);

    if (!p_dbus_message_iter_open_container(&eIter, 'v', "s", &vIter))
    {
        WARN("failed to create icon name value\n");
        p_dbus_message_iter_abandon_container_if_open(&aIter, &eIter);
        p_dbus_message_iter_abandon_container_if_open(&iter, &aIter);
        goto fail;
    }

    handle_icon_name(conn, &vIter);

    p_dbus_message_iter_close_container(&eIter, &vIter);
    p_dbus_message_iter_close_container(&aIter, &eIter);
    p_dbus_message_iter_close_container(&iter, &aIter);

    pthread_mutex_unlock((pthread_mutex_t*)&icon->mutex);

    if (!p_dbus_connection_send(conn, reply, &serial))
        goto send_fail;

    p_dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
fail:
    pthread_mutex_unlock((pthread_mutex_t*)&icon->mutex);
send_fail:
    p_dbus_message_unref(reply);

    return notification_send_error(conn, message, "org.freedesktop.DBus.Error.Failed", "got an error while processing properties");
}

DBusHandlerResult notification_message_handler(DBusConnection *conn, DBusMessage *message, void *data)
{
    const struct tray_icon* icon = (struct tray_icon*)data;
    if (p_dbus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get"))
    {
        const char* interface_name = "";
        const char* property_name = "";
        DBusMessage* reply = NULL;
        DBusMessageIter iter, vIter;
        unsigned serial = 0;
        DBusHandlerResult ret = DBUS_HANDLER_RESULT_HANDLED;
        DBusError error;
        p_dbus_error_init(&error);
        if (!p_dbus_message_get_args( message, &error, DBUS_TYPE_STRING, &interface_name,
                                     DBUS_TYPE_STRING, &property_name, DBUS_TYPE_INVALID ))
        {
            DBusHandlerResult ret = notification_send_error (conn, message, error.name, error.message);
            p_dbus_error_free( &error);
            return ret;
        }

        if (strcmp(interface_name, item_interface_name) != 0)
        {
            char error_message[128];
            snprintf(error_message, sizeof(error_message), "unsupported interface %s", interface_name);
            return notification_send_error (conn, message, "org.freedesktop.DBus.Error.UnknownProperty", error_message);
        }

        pthread_mutex_lock((pthread_mutex_t*)&icon->mutex);

        if (strcmp(property_name, title_field) == 0)
        {
            reply = p_dbus_message_new_method_return(message);
            p_dbus_message_iter_init_append(reply, &iter);
            if (!p_dbus_message_iter_open_container(&iter, 'v', "s", &vIter))
            {
                p_dbus_message_unref(reply);
                ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
                goto err_get;
            }
            handle_title(conn, &vIter, icon);
            p_dbus_message_iter_close_container(&iter, &vIter);
        }
        else if (strcmp(property_name, id_field) == 0)
        {
            reply = p_dbus_message_new_method_return(message);
            p_dbus_message_iter_init_append(reply, &iter);
            if (!p_dbus_message_iter_open_container(&iter, 'v', "s", &vIter))
            {
                p_dbus_message_unref(reply);
                ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
                goto err_get;
            }
            handle_id(conn, &vIter, icon);
            p_dbus_message_iter_close_container(&iter, &vIter);
        }
        else if (strcmp(property_name, icon_field) == 0 && icon->hIcon)
        {
            reply = p_dbus_message_new_method_return(message);
            p_dbus_message_iter_init_append(reply, &iter);
            if (!p_dbus_message_iter_open_container(&iter, 'v', "a(iiay)", &vIter))
            {
                p_dbus_message_unref(reply);
                ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
                goto err_get;
            }
            handle_icon(conn, &vIter, icon);
            p_dbus_message_iter_close_container(&iter, &vIter);
        }
        else if (strcmp(property_name, status_field) == 0)
        {
            reply = p_dbus_message_new_method_return(message);
            p_dbus_message_iter_init_append(reply, &iter);
            if (!p_dbus_message_iter_open_container(&iter, 'v', "s", &vIter))
            {
                p_dbus_message_unref(reply);
                ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
                goto err_get;
            }
            handle_status(conn, &vIter, icon);
            p_dbus_message_iter_close_container(&iter, &vIter);
        }
        else
        {
            char error_message[128];
            pthread_mutex_unlock((pthread_mutex_t*)&icon->mutex);
            snprintf(error_message, sizeof(error_message), "interface doesn't have the property %s", property_name);
            return notification_send_error (conn, message, "org.freedesktop.DBus.Error.UnknownProperty", error_message);
        }

        if (!p_dbus_connection_send(conn, reply, &serial))
        {
            p_dbus_message_unref(reply);
            ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
            goto err_get;
        }

        p_dbus_message_unref(reply);
    err_get:
        pthread_mutex_unlock((pthread_mutex_t*)&icon->mutex);
        return ret;
    }
    else if (p_dbus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "GetAll"))
    {
        const char* interface_name = "";
        DBusMessageIter args;
        if (!p_dbus_message_iter_init(message, &args))
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        else if (DBUS_TYPE_STRING != p_dbus_message_iter_get_arg_type(&args))
        {
            return notification_send_error (conn, message, "org.freedesktop.DBus.Error.InvalidArgs", "Call to Get has wrong args");
        }
        else
            p_dbus_message_iter_get_basic(&args, &interface_name);

        if (strcmp(item_interface_name, interface_name) == 0)
            return get_all_tray_properties(conn, message, icon);
        else
            return notification_send_error(conn, message, "org.freedesktop.DBus.Error.UnknownInterface", "Call to Get has wrong args");

    }
    else if (p_dbus_message_is_method_call(message, item_interface_name, "ContextMenu"))
    {
        int x,y;
        DBusMessageIter args;
        if (!p_dbus_message_iter_init(message, &args))
            return DBUS_HANDLER_RESULT_NEED_MEMORY;

        if (DBUS_TYPE_INT32 != p_dbus_message_iter_get_arg_type(&args))
            return notification_send_error (conn, message, "org.freedesktop.DBus.Error.InvalidArgs", "Call to Get has wrong args");
        else
            p_dbus_message_iter_get_basic(&args, &x);

        if (!p_dbus_message_iter_next(&args) || DBUS_TYPE_INT32 != p_dbus_message_iter_get_arg_type(&args))
            return notification_send_error (conn, message, "org.freedesktop.DBus.Error.InvalidArgs", "Call to Get has wrong args");
        else
            p_dbus_message_iter_get_basic(&args, &y);

        notify_owner( icon, WM_RBUTTONDOWN, (unsigned short) x, (unsigned short) y);
        if (icon->version > 0)
            notify_owner( icon, WM_CONTEXTMENU, (unsigned short) x, (unsigned short) y);
    }
    else if (p_dbus_message_is_method_call(message, item_interface_name, "Activate"))
    {
        int x,y;
        DBusMessageIter args;
        if (!p_dbus_message_iter_init(message, &args))
            return DBUS_HANDLER_RESULT_NEED_MEMORY;

        if (DBUS_TYPE_INT32 != p_dbus_message_iter_get_arg_type(&args))
            return notification_send_error (conn, message, "org.freedesktop.DBus.Error.InvalidArgs", "Call to Get has wrong args");
        else
            p_dbus_message_iter_get_basic(&args, &x);

        if (!p_dbus_message_iter_next(&args) || DBUS_TYPE_INT32 != p_dbus_message_iter_get_arg_type(&args))
            return notification_send_error (conn, message, "org.freedesktop.DBus.Error.InvalidArgs", "Call to Get has wrong args");
        else
            p_dbus_message_iter_get_basic(&args, &y);

        notify_owner( icon, WM_LBUTTONDOWN, (unsigned short) x, (unsigned short) y);
        if (icon->version > 0) notify_owner( icon, NIN_SELECT, (unsigned short) x, (unsigned short) y);
    }
    else if (p_dbus_message_is_method_call(message, item_interface_name, "SecondaryActivate"))
    {
        int x,y;
        DBusMessageIter args;
        if (!p_dbus_message_iter_init(message, &args))
            return DBUS_HANDLER_RESULT_NEED_MEMORY;

        if (DBUS_TYPE_INT32 != p_dbus_message_iter_get_arg_type(&args))
            return notification_send_error (conn, message, "org.freedesktop.DBus.Error.InvalidArgs", "Call to Get has wrong args");
        else
            p_dbus_message_iter_get_basic(&args, &x);

        if (!p_dbus_message_iter_next(&args) || DBUS_TYPE_INT32 != p_dbus_message_iter_get_arg_type(&args))
            return notification_send_error (conn, message, "org.freedesktop.DBus.Error.InvalidArgs", "Call to Get has wrong args");
        else
            p_dbus_message_iter_get_basic(&args, &y);
        notify_owner( icon, WM_MBUTTONDOWN, (unsigned short) x, (unsigned short) y);
    }
    else if (p_dbus_message_is_method_call(message, item_interface_name, "Scroll"))
    {
        /* do nothing */
    }
    else if (p_dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_METHOD_CALL)
        return notification_send_error (conn, message, "DBus.Error.UnknownMethod", "Unknown method");

    return DBUS_HANDLER_RESULT_HANDLED;
}

const DBusObjectPathVTable notification_vtable =
{
    .message_function = notification_message_handler,
};

BOOL snidrv_run_loop()
{
    DBusConnection* conns[128];
    DBusWatch* watches[128];
    struct pollfd fd_info[128];
    int fd_count;
    struct pollfd* fd_ptr = fd_info;
    while (true)
    {
        int i, poll_ret;
        struct tray_icon* icon;
        fd_count = 0;
        /* TODO: add condvar if there are no connections available */
        pthread_mutex_lock(&list_mutex);
        if (global_connection_watch_fd)
        {
            conns[fd_count] = p_dbus_connection_ref(global_connection);
            watches[fd_count] = global_connection_watch;
            fd_info[fd_count++] = (struct pollfd) {
                .fd = global_connection_watch_fd,
                .events = global_connection_watch_flags,
                .revents = 0,
            };
        }
        LIST_FOR_EACH_ENTRY( icon, &sni_list, struct tray_icon, entry )
        {
            if (fd_count >= 128)
                break;
            if (!icon->watch_fd)
                continue;
            conns[fd_count] = p_dbus_connection_ref(icon->connection);
            watches[fd_count] = icon->watch;
            fd_info[fd_count++] = (struct pollfd) {
                .fd = icon->watch_fd,
                .events = icon->watch_flags,
                .revents = 0,
            };
        }
        pthread_mutex_unlock(&list_mutex);

        poll_ret = poll(fd_ptr, fd_count, 100);
        if (poll_ret == 0)
            goto cleanup;
        if (poll_ret == -1)
        {
            ERR("fd poll error\n");
            goto cleanup;
        }

        for ( i = 0; i < fd_count; i++ )
        {
            if (fd_info[i].revents & (POLLERR | POLLHUP | POLLNVAL)) continue;
            if (fd_info[i].revents & POLLIN)
            {
                p_dbus_watch_handle(watches[i], DBUS_WATCH_READABLE);
                while ( p_dbus_connection_get_dispatch_status ( conns[i] ) == DBUS_DISPATCH_DATA_REMAINS )
                {
                    p_dbus_connection_dispatch ( conns[i] ) ;
                }
            }
            if (fd_info[i].revents & POLLOUT)
                p_dbus_watch_handle(watches[i], DBUS_WATCH_WRITABLE);
        }
    cleanup:
        for ( i = 0; i < fd_count; i++ )
        {
            p_dbus_connection_unref(conns[i]);
        }
    }
    return 0;
}

static struct tray_icon *get_icon(HWND owner, UINT id)
{
    struct tray_icon *this;

    pthread_mutex_lock(&list_mutex);
    LIST_FOR_EACH_ENTRY( this, &sni_list, struct tray_icon, entry )
    {
        if ((this->id == id) && (this->owner == owner))
        {
            pthread_mutex_unlock(&list_mutex);
            return this;
        }
    }
    pthread_mutex_unlock(&list_mutex);
    return NULL;
}

static BOOL register_notification_item(DBusConnection* connection)
{
    DBusMessageIter args;
    DBusPendingCall* pending;
    DBusMessage* msg = NULL;
    DBusError error;
    const char* connection_service_name = p_dbus_bus_get_unique_name(connection);

    p_dbus_error_init( &error );
    msg = p_dbus_message_new_method_call(status_notifier_dst_path,
                                         "/StatusNotifierWatcher",
                                         watcher_interface_name ,
                                         "RegisterStatusNotifierItem");
    if (!msg) goto err;

    p_dbus_message_iter_init_append(msg, &args);
    if (!p_dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &connection_service_name ))
        goto err;

    if (!p_dbus_connection_send_with_reply (connection, msg, &pending, -1))
        goto err;

    p_dbus_message_unref(msg);

    p_dbus_pending_call_block(pending);

    msg = p_dbus_pending_call_steal_reply(pending);
    if (NULL == msg) goto err;

    p_dbus_pending_call_unref(pending);

    if (p_dbus_set_error_from_message (&error, msg))
    {
        WARN("got error %s: %s\n", error.name, error.message);
        p_dbus_error_free( &error);
        goto err;
    }

    p_dbus_message_unref(msg);
    return TRUE;
err:
    if (msg != NULL) p_dbus_message_unref(msg);
    return FALSE;
}

static BOOL send_signal_to_item(DBusConnection* connection, const char* signal_name)
{
    DBusMessage* msg;
    DBusError error;
    unsigned serial = 0;
    p_dbus_error_init( &error );
    msg = p_dbus_message_new_signal(object_path,
                                    item_interface_name,
                                    signal_name);
    if (NULL == msg) return FALSE;

    if (!p_dbus_connection_send (connection, msg, &serial))
    {
        p_dbus_message_unref(msg);
        return FALSE;
    }

    p_dbus_message_unref(msg);

    return TRUE;
}


BOOL get_icon_data(const NOTIFYICONDATAW* icon_data, struct tray_icon* dst)
{
    void* bits = NULL;
    unsigned width, height;
    HICON new_icon = NULL;
    new_icon = CopyImage(icon_data->hIcon, IMAGE_ICON, 0, 0, 0);
    if (!create_bitmap_from_icon(new_icon, &width, &height, &bits))
        goto fail;

    if (dst->hIcon) NtUserDestroyCursor(dst->hIcon, 0);
    if (dst->icon_bitmap) free(dst->icon_bitmap);
    dst->hIcon = new_icon;
    dst->icon_bitmap = bits;
    dst->icon_width = width;
    dst->icon_height = height;
    return TRUE;
fail:
    NtUserDestroyCursor(new_icon, 0);
    free(bits);
    return FALSE;
}

BOOL snidrv_add_notify_icon(const NOTIFYICONDATAW* icon_data)
{
    DBusConnection* connection = NULL;
    struct tray_icon* icon = NULL;
    DBusError error;
    bool registered = false;
    p_dbus_error_init( &error );

    if (!(connection = p_dbus_bus_get_private( DBUS_BUS_SESSION, &error )))
    {
        WARN("failed to get system dbus connection: %s\n", error.message );
        p_dbus_error_free( &error );
        goto fail;
    }

    icon = malloc(sizeof(struct tray_icon));
    if (!icon) goto fail;

    memset(icon, 0, sizeof(*icon));
    icon->id = icon_data->uID;
    icon->owner = icon_data->hWnd;
    icon->connection = connection;
    if (pthread_mutex_init(&icon->mutex, NULL))
    {
        WARN("failed to initialize mutex\n" );
        goto fail;
    }

    if (!p_dbus_connection_set_watch_functions(connection, add_watch, remove_watch,
                                               toggle_watch, icon, NULL))
    {
        WARN("dbus_set_watch_functions() failed\n");
        goto fail;
    }

    if (icon_data->uFlags & NIF_ICON)
    {
        if (!get_icon_data(icon_data, icon))
        {
            WARN("failed to get icon info\n" );
            goto fail;
        }
    }
    if (icon_data->uFlags & NIF_MESSAGE)
    {
        icon->callback_message = icon_data->uCallbackMessage;
    }
    if (icon_data->uFlags & NIF_TIP)
        ntdll_wcstoumbs(icon_data->szTip, wcslen(icon_data->szTip) + 1, icon->tiptext, ARRAY_SIZE(icon->tiptext), FALSE);
    if (icon_data->uFlags & NIF_STATE)
        icon->state = (icon->state & ~icon_data->dwStateMask) | (icon_data->dwState & icon_data->dwStateMask);
    if (notifications_dst_path && notifications_dst_path[0] &&
        !(icon->state & NIS_HIDDEN) && (icon_data->uFlags & NIF_INFO) && icon_data->cbSize >= NOTIFYICONDATAA_V2_SIZE)
        send_notification(icon->connection,
                          icon->notification_id,
                          icon_data->szInfoTitle,
                          icon_data->szInfo,
                          icon_data->hBalloonIcon,
                          icon_data->dwInfoFlags,
                          icon_data->uTimeout,
                          &icon->notification_id);
    icon->version = icon_data->uVersion;
    if (!p_dbus_connection_try_register_object_path(connection, object_path, &notification_vtable, icon, &error))
    {
        WARN("failed register object %s: %s\n", error.name, error.message);
        p_dbus_error_free( &error );
        goto fail;
    }
    registered = true;
    /* don't register, if there is no SNWatcher available, it might be reinitializing */
    if (status_notifier_dst_path != NULL && status_notifier_dst_path[0] != '\0' &&
        !register_notification_item(connection))
    {
        WARN("failed to register item\n");
        p_dbus_connection_unregister_object_path(connection, object_path);
        goto fail;
    }
    list_add_tail(&sni_list, &icon->entry);
    return TRUE;
fail:
    if (icon && icon->hIcon) NtUserDestroyCursor(icon->hIcon, 0);
    if (icon && icon->icon_bitmap) free(icon->icon_bitmap);
    if (icon) pthread_mutex_destroy(&icon->mutex);
    free(icon);
    if (registered)
        p_dbus_connection_unregister_object_path(connection, object_path);
    if (connection)
        p_dbus_connection_close(connection);
    return FALSE;
}

static BOOL cleanup_icon(struct tray_icon* icon)
{
    pthread_mutex_lock(&icon->mutex);
    p_dbus_connection_flush(icon->connection);
    p_dbus_connection_close(icon->connection);
    p_dbus_connection_unref(icon->connection);
    pthread_mutex_unlock(&icon->mutex);

    if (icon->hIcon) NtUserDestroyCursor(icon->hIcon, 0);
    if (icon->icon_bitmap) free(icon->icon_bitmap);
    if (icon) pthread_mutex_destroy(&icon->mutex);

    free(icon);
    return TRUE;
}

BOOL snidrv_delete_notify_icon( HWND hwnd, UINT uID )
{
    struct tray_icon *icon = NULL, *this, *next;
    pthread_mutex_lock(&list_mutex);
    LIST_FOR_EACH_ENTRY_SAFE( this, next, &sni_list, struct tray_icon, entry )
    {
        if ((this->id == uID) && (this->owner == hwnd))
        {
            list_remove(&this->entry);
            icon = this;
            break;
        }
    }
    pthread_mutex_unlock(&list_mutex);
    if (!icon) return FALSE;
    return cleanup_icon(icon);
}

BOOL snidrv_modify_notify_icon( const NOTIFYICONDATAW* icon_data )
{
    struct tray_icon* icon;
    const char* pending_signals[4];
    UINT signal_count = 0;
    UINT new_state;
    UINT i;
    icon = get_icon(icon_data->hWnd, icon_data->uID);
    if (!icon)
        return FALSE;

    pthread_mutex_lock(&icon->mutex);

    if (icon_data->uFlags & NIF_ICON)
    {
        if (!get_icon_data(icon_data, icon))
            goto err;
        pending_signals[signal_count++] = "NewIcon";
    }

    if (icon_data->uFlags & NIF_MESSAGE)
    {
        icon->callback_message = icon_data->uCallbackMessage;
    }

    if (icon_data->uFlags & NIF_STATE)
    {
        new_state = (icon->state & ~icon_data->dwStateMask) | (icon_data->dwState & icon_data->dwStateMask);
        if (new_state != icon->state)
        {
            icon->state = new_state;
            pending_signals[signal_count++] = "NewStatus";
        }
    }

    if (icon_data->uFlags & NIF_TIP)
    {
        ntdll_wcstoumbs(icon_data->szTip, wcslen(icon_data->szTip) + 1, icon->tiptext, ARRAY_SIZE(icon->tiptext), FALSE);
        pending_signals[signal_count++] = "NewTitle";
    }

    pthread_mutex_unlock(&icon->mutex);

    /* send the signals */
    for (i = 0; i < signal_count; i++)
    {
        if (!send_signal_to_item(icon->connection, pending_signals[i]))
            goto err_post_unlock;
    }

    if (notifications_dst_path && notifications_dst_path[0])
    {
        if (!(icon->state & NIS_HIDDEN) && (icon_data->uFlags & NIF_INFO) && icon_data->cbSize >= NOTIFYICONDATAA_V2_SIZE)
            send_notification(icon->connection, icon->notification_id, icon_data->szInfoTitle, icon_data->szInfo, icon_data->hBalloonIcon, icon_data->dwInfoFlags, icon_data->uTimeout, &icon->notification_id);
        else if ((icon->state & NIS_HIDDEN) && icon->notification_id)
            close_notification(icon->connection, icon->notification_id);
    }
    return TRUE;
err:
    pthread_mutex_unlock(&icon->mutex);
err_post_unlock:
    return FALSE;
}

BOOL snidrv_set_notify_icon_version( HWND hwnd, UINT uID, UINT uVersion)
{
    struct tray_icon* icon;
    icon = get_icon(hwnd, uID);
    if (!icon)
        return FALSE;
    pthread_mutex_lock(&icon->mutex);
    icon->version = uVersion;
    pthread_mutex_unlock(&icon->mutex);

    return TRUE;
}

BOOL snidrv_cleanup_notify_icons(HWND owner)
{
    struct tray_icon *this, *next;

    pthread_mutex_lock(&list_mutex);
    LIST_FOR_EACH_ENTRY_SAFE( this, next, &sni_list, struct tray_icon, entry )
    {
        if (this->owner == owner)
        {
            list_remove(&this->entry);
            cleanup_icon(this);
        }
    }
    pthread_mutex_unlock(&list_mutex);
    return TRUE;
}

BOOL snidrv_show_balloon( HWND owner, UINT id, BOOL hidden, const struct systray_balloon* balloon )
{
    BOOL ret = TRUE;
    struct standalone_notification *found_notification = NULL, *this;

    if (!notifications_dst_path || !notifications_dst_path[0])
        return -1;
    pthread_mutex_lock(&standalone_notifications_mutex);

    LIST_FOR_EACH_ENTRY(this, &standalone_notification_list, struct standalone_notification, entry)
    {
        if (this->owner == owner && this->id == id)
        {
            found_notification = this;
            break;
        }
    }
    /* close existing notification anyway */
    if (!hidden)
    {
        if (!found_notification)
        {
            found_notification = malloc(sizeof(struct standalone_notification));
            if (!found_notification)
            {
                ret = FALSE;
                goto cleanup;
            }
            found_notification->owner = owner;
            found_notification->id = id;
            found_notification->notification_id = 0;
            list_add_tail(&standalone_notification_list, &found_notification->entry);
        }
        else
            TRACE("found existing notification %p %d\n", owner, id);
        ret = send_notification(global_connection,
                                found_notification->notification_id,
                                balloon->info_title,
                                balloon->info_text,
                                balloon->info_icon,
                                balloon->info_flags,
                                balloon->info_timeout,
                                &found_notification->notification_id);
    }
    else if (found_notification)
    {
        ret = close_notification(global_connection, found_notification->notification_id);
    }
cleanup:
    pthread_mutex_unlock(&standalone_notifications_mutex);
    return ret;
}
#endif
