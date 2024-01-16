/*
 * Wayland output handling
 *
 * Copyright (c) 2020 Alexandros Frantzis for Collabora Ltd
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

#include "waylanddrv.h"

#include "wine/debug.h"

#include <stdlib.h>

WINE_DEFAULT_DEBUG_CHANNEL(waylanddrv);

static const int32_t default_refresh = 60000;
static uint32_t next_output_id = 0;

#define WAYLAND_OUTPUT_CHANGED_MODES      0x01
#define WAYLAND_OUTPUT_CHANGED_NAME       0x02
#define WAYLAND_OUTPUT_CHANGED_LOGICAL_XY 0x04
#define WAYLAND_OUTPUT_CHANGED_LOGICAL_WH 0x08

static const struct { int32_t width; int32_t height; } common_modes[] = {
    { 320,  200}, /* CGA 16:10 */
    { 320,  240}, /* QVGA 4:3 */
    { 400,  300}, /* qSVGA 4:3 */
    { 480,  320}, /* HVGA 3:2 */
    { 512,  384}, /* MAC 4:3 */
    { 640,  360}, /* nHD 16:9 */
    { 640,  400}, /* VESA-0100h 16:10 */
    { 640,  480}, /* VGA 4:3 */
    { 720,  480}, /* WVGA 3:2 */
    { 720,  576}, /* PAL 5:4 */
    { 768,  480}, /* WVGA 16:10 */
    { 768,  576}, /* PAL* 4:3 */
    { 800,  600}, /* SVGA 4:3 */
    { 854,  480}, /* FWVGA 16:9 */
    { 960,  540}, /* qHD 16:9 */
    { 960,  640}, /* DVGA 3:2 */
    {1024,  576}, /* WSVGA 16:9 */
    {1024,  640}, /* WSVGA 16:10 */
    {1024,  768}, /* XGA 4:3 */
    {1152,  864}, /* XGA+ 4:3 */
    {1280,  720}, /* HD 16:9 */
    {1280,  768}, /* WXGA 5:3 */
    {1280,  800}, /* WXGA 16:10 */
    {1280,  960}, /* SXGA- 4:3 */
    {1280, 1024}, /* SXGA 5:4 */
    {1366,  768}, /* FWXGA 16:9 */
    {1400, 1050}, /* SXGA+ 4:3 */
    {1440,  900}, /* WSXGA 16:10 */
    {1600,  900}, /* HD+ 16:9 */
    {1600, 1200}, /* UXGA 4:3 */
    {1680, 1050}, /* WSXGA+ 16:10 */
    {1920, 1080}, /* FHD 16:9 */
    {1920, 1200}, /* WUXGA 16:10 */
    {2048, 1152}, /* QWXGA 16:9 */
    {2048, 1536}, /* QXGA 4:3 */
    {2560, 1440}, /* QHD 16:9 */
    {2560, 1600}, /* WQXGA 16:10 */
    {2560, 2048}, /* QSXGA 5:4 */
    {2880, 1620}, /* 3K 16:9 */
    {3200, 1800}, /* QHD+ 16:9 */
    {3200, 2400}, /* QUXGA 4:3 */
    {3840, 2160}, /* 4K 16:9 */
    {3840, 2400}, /* WQUXGA 16:10 */
    {5120, 2880}, /* 5K 16:9 */
    {7680, 4320}, /* 8K 16:9 */
};

/**********************************************************************
 *          Output handling
 */

/* Compare a mode rb_tree key with the provided mode rb_entry and return -1 if
 * the key compares less than the entry, 0 if the key compares equal to the
 * entry, and 1 if the key compares greater than the entry.
 *
 * The comparison is based on comparing the width, height and refresh in that
 * order. */
static int wayland_output_mode_cmp_rb(const void *key,
                                      const struct rb_entry *entry)
{
    const struct wayland_output_mode *key_mode = key;
    const struct wayland_output_mode *entry_mode =
        RB_ENTRY_VALUE(entry, const struct wayland_output_mode, entry);

    if (key_mode->width < entry_mode->width) return -1;
    if (key_mode->width > entry_mode->width) return 1;
    if (key_mode->height < entry_mode->height) return -1;
    if (key_mode->height > entry_mode->height) return 1;
    if (key_mode->refresh < entry_mode->refresh) return -1;
    if (key_mode->refresh > entry_mode->refresh) return 1;

    return 0;
}

static void wayland_output_state_add_mode(struct wayland_output_state *state,
                                          int32_t width, int32_t height,
                                          int32_t refresh, BOOL current)
{
    struct rb_entry *mode_entry;
    struct wayland_output_mode *mode;
    struct wayland_output_mode key =
    {
        .width = width,
        .height = height,
        .refresh = refresh,
    };

    mode_entry = rb_get(&state->modes, &key);
    if (mode_entry)
    {
        mode = RB_ENTRY_VALUE(mode_entry, struct wayland_output_mode, entry);
    }
    else
    {
        mode = calloc(1, sizeof(*mode));
        if (!mode)
        {
            ERR("Failed to allocate space for wayland_output_mode\n");
            return;
        }
        mode->width = width;
        mode->height = height;
        mode->refresh = refresh;
        rb_put(&state->modes, mode, &mode->entry);
    }

    if (current) state->current_mode = mode;
}

static void wayland_output_state_add_common_modes(struct wayland_output_state *state)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(common_modes); i++)
    {
        int32_t width = common_modes[i].width;
        int32_t height = common_modes[i].height;

        /* Skip if this mode is larger than the current (native) mode. */
        if (width > state->current_mode->width ||
            height > state->current_mode->height)
        {
            TRACE("Skipping mode %dx%d (current: %dx%d)\n",
                  width, height, state->current_mode->width,
                  state->current_mode->height);
            continue;
        }

        wayland_output_state_add_mode(state, width, height,
                                      state->current_mode->refresh,
                                      FALSE);
    }
}

static void maybe_init_display_devices(void)
{
    DWORD desktop_pid = 0;
    HWND desktop_hwnd;

    /* Right after process init we initialize all the display devices, so there
     * is no need to react to each individual event at that time. This check
     * also helps us avoid calling NtUserGetDesktopWindow() (see below) at
     * process init time, since it may not be safe. */
    if (!process_wayland.initialized) return;

    desktop_hwnd = NtUserGetDesktopWindow();
    NtUserGetWindowThread(desktop_hwnd, &desktop_pid);

    /* We only update the display devices from the desktop process. */
    if (GetCurrentProcessId() != desktop_pid) return;

    NtUserPostMessage(desktop_hwnd, WM_WAYLAND_INIT_DISPLAY_DEVICES, 0, 0);
}

static void wayland_output_mode_free_rb(struct rb_entry *entry, void *ctx)
{
    free(RB_ENTRY_VALUE(entry, struct wayland_output_mode, entry));
}

static void wayland_output_done(struct wayland_output *output)
{
    struct wayland_output_mode *mode;

    /* Update current state from pending state. */
    pthread_mutex_lock(&process_wayland.output_mutex);

    if (output->pending_flags & WAYLAND_OUTPUT_CHANGED_MODES)
    {
        rb_destroy(&output->current.modes, wayland_output_mode_free_rb, NULL);
        output->current.modes = output->pending.modes;
        output->current.current_mode = output->pending.current_mode;
        if (!output->current.current_mode)
            WARN("No current mode reported by compositor\n");
        else
            wayland_output_state_add_common_modes(&output->current);
        rb_init(&output->pending.modes, wayland_output_mode_cmp_rb);
        output->pending.current_mode = NULL;
    }

    if (output->pending_flags & WAYLAND_OUTPUT_CHANGED_NAME)
    {
        free(output->current.name);
        output->current.name = output->pending.name;
        output->pending.name = NULL;
    }

    if (output->pending_flags & WAYLAND_OUTPUT_CHANGED_LOGICAL_XY)
    {
        output->current.logical_x = output->pending.logical_x;
        output->current.logical_y = output->pending.logical_y;
    }

    if (output->pending_flags & WAYLAND_OUTPUT_CHANGED_LOGICAL_WH)
    {
        output->current.logical_w = output->pending.logical_w;
        output->current.logical_h = output->pending.logical_h;
    }

    output->pending_flags = 0;

    /* Ensure the logical dimensions have sane values. */
    if ((!output->current.logical_w || !output->current.logical_h) &&
        output->current.current_mode)
    {
        output->current.logical_w = output->current.current_mode->width;
        output->current.logical_h = output->current.current_mode->height;
    }

    pthread_mutex_unlock(&process_wayland.output_mutex);

    TRACE("name=%s logical=%d,%d+%dx%d\n",
          output->current.name, output->current.logical_x, output->current.logical_y,
          output->current.logical_w, output->current.logical_h);

    RB_FOR_EACH_ENTRY(mode, &output->current.modes, struct wayland_output_mode, entry)
    {
        TRACE("mode %dx%d @ %d %s\n",
              mode->width, mode->height, mode->refresh,
              output->current.current_mode == mode ? "*" : "");
    }

    maybe_init_display_devices();
}

static void output_handle_geometry(void *data, struct wl_output *wl_output,
                                   int32_t x, int32_t y,
                                   int32_t physical_width, int32_t physical_height,
                                   int32_t subpixel,
                                   const char *make, const char *model,
                                   int32_t output_transform)
{
}

static void output_handle_mode(void *data, struct wl_output *wl_output,
                               uint32_t flags, int32_t width, int32_t height,
                               int32_t refresh)
{
    struct wayland_output *output = data;

    /* Non-current mode information is deprecated. */
    if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;

    /* Windows apps don't expect a zero refresh rate, so use a default value. */
    if (refresh == 0) refresh = default_refresh;

    wayland_output_state_add_mode(&output->pending, width, height, refresh, TRUE);

    output->pending_flags |= WAYLAND_OUTPUT_CHANGED_MODES;
}

static void output_handle_done(void *data, struct wl_output *wl_output)
{
    struct wayland_output *output = data;

    if (!output->zxdg_output_v1 ||
        zxdg_output_v1_get_version(output->zxdg_output_v1) >= 3)
    {
        wayland_output_done(output);
    }
}

static void output_handle_scale(void *data, struct wl_output *wl_output,
                                int32_t scale)
{
}

static const struct wl_output_listener output_listener = {
    output_handle_geometry,
    output_handle_mode,
    output_handle_done,
    output_handle_scale
};

static void zxdg_output_v1_handle_logical_position(void *data,
                                                   struct zxdg_output_v1 *zxdg_output_v1,
                                                   int32_t x,
                                                   int32_t y)
{
    struct wayland_output *output = data;
    TRACE("logical_x=%d logical_y=%d\n", x, y);
    output->pending.logical_x = x;
    output->pending.logical_y = y;
    output->pending_flags |= WAYLAND_OUTPUT_CHANGED_LOGICAL_XY;
}

static void zxdg_output_v1_handle_logical_size(void *data,
                                               struct zxdg_output_v1 *zxdg_output_v1,
                                               int32_t width,
                                               int32_t height)
{
    struct wayland_output *output = data;
    TRACE("logical_w=%d logical_h=%d\n", width, height);
    output->pending.logical_w = width;
    output->pending.logical_h = height;
    output->pending_flags |= WAYLAND_OUTPUT_CHANGED_LOGICAL_WH;
}

static void zxdg_output_v1_handle_done(void *data,
                                       struct zxdg_output_v1 *zxdg_output_v1)
{
    if (zxdg_output_v1_get_version(zxdg_output_v1) < 3)
    {
        struct wayland_output *output = data;
        wayland_output_done(output);
    }
}

static void zxdg_output_v1_handle_name(void *data,
                                       struct zxdg_output_v1 *zxdg_output_v1,
                                       const char *name)
{
    struct wayland_output *output = data;

    free(output->pending.name);
    output->pending.name = strdup(name);
    output->pending_flags |= WAYLAND_OUTPUT_CHANGED_NAME;
}

static void zxdg_output_v1_handle_description(void *data,
                                              struct zxdg_output_v1 *zxdg_output_v1,
                                              const char *description)
{
}

static const struct zxdg_output_v1_listener zxdg_output_v1_listener = {
    zxdg_output_v1_handle_logical_position,
    zxdg_output_v1_handle_logical_size,
    zxdg_output_v1_handle_done,
    zxdg_output_v1_handle_name,
    zxdg_output_v1_handle_description,
};

/**********************************************************************
 *          wayland_output_create
 *
 *  Creates a wayland_output and adds it to the output list.
 */
BOOL wayland_output_create(uint32_t id, uint32_t version)
{
    struct wayland_output *output = calloc(1, sizeof(*output));
    int name_len;

    if (!output)
    {
        ERR("Failed to allocate space for wayland_output\n");
        goto err;
    }

    output->wl_output = wl_registry_bind(process_wayland.wl_registry, id,
                                         &wl_output_interface,
                                         version < 2 ? version : 2);
    output->global_id = id;
    wl_output_add_listener(output->wl_output, &output_listener, output);

    wl_list_init(&output->link);
    rb_init(&output->pending.modes, wayland_output_mode_cmp_rb);
    rb_init(&output->current.modes, wayland_output_mode_cmp_rb);

    /* Have a fallback while we don't have compositor given name. */
    name_len = snprintf(NULL, 0, "WaylandOutput%d", next_output_id);
    output->current.name = malloc(name_len + 1);
    if (output->current.name)
    {
        snprintf(output->current.name, name_len + 1, "WaylandOutput%d", next_output_id++);
    }
    else
    {
        ERR("Couldn't allocate space for output name\n");
        goto err;
    }

    if (process_wayland.zxdg_output_manager_v1)
        wayland_output_use_xdg_extension(output);

    pthread_mutex_lock(&process_wayland.output_mutex);
    wl_list_insert(process_wayland.output_list.prev, &output->link);
    pthread_mutex_unlock(&process_wayland.output_mutex);

    return TRUE;

err:
    if (output) wayland_output_destroy(output);
    return FALSE;
}

static void wayland_output_state_deinit(struct wayland_output_state *state)
{
    rb_destroy(&state->modes, wayland_output_mode_free_rb, NULL);
    free(state->name);
}

/**********************************************************************
 *          wayland_output_destroy
 *
 *  Destroys a wayland_output.
 */
void wayland_output_destroy(struct wayland_output *output)
{
    pthread_mutex_lock(&process_wayland.output_mutex);
    wl_list_remove(&output->link);
    pthread_mutex_unlock(&process_wayland.output_mutex);

    wayland_output_state_deinit(&output->pending);
    wayland_output_state_deinit(&output->current);
    if (output->zxdg_output_v1)
        zxdg_output_v1_destroy(output->zxdg_output_v1);
    wl_output_destroy(output->wl_output);
    free(output);

    maybe_init_display_devices();
}

/**********************************************************************
 *          wayland_output_use_xdg_extension
 *
 *  Use the zxdg_output_v1 extension to get output information.
 */
void wayland_output_use_xdg_extension(struct wayland_output *output)
{
    output->zxdg_output_v1 =
        zxdg_output_manager_v1_get_xdg_output(process_wayland.zxdg_output_manager_v1,
                                              output->wl_output);
    zxdg_output_v1_add_listener(output->zxdg_output_v1, &zxdg_output_v1_listener,
                                output);
}
