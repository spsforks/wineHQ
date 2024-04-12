/* X11DRV Vulkan implementation
 *
 * Copyright 2017 Roderick Colenbrander
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

/* NOTE: If making changes here, consider whether they should be reflected in
 * the other drivers. */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <dlfcn.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"

#include "wine/debug.h"
#include "x11drv.h"

#define VK_NO_PROTOTYPES
#define WINE_VK_HOST

#include "wine/vulkan.h"
#include "wine/vulkan_driver.h"

WINE_DEFAULT_DEBUG_CHANNEL(vulkan);

#ifdef SONAME_LIBVULKAN

static pthread_mutex_t vulkan_mutex;

#define VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR 1000004000

static struct list surface_list = LIST_INIT( surface_list );

struct wine_vk_surface
{
    LONG ref;
    struct list entry;
    Window window;
    VkSurfaceKHR host_surface;
    HWND hwnd;
    DWORD hwnd_thread_id;
};

typedef struct VkXlibSurfaceCreateInfoKHR
{
    VkStructureType sType;
    const void *pNext;
    VkXlibSurfaceCreateFlagsKHR flags;
    Display *dpy;
    Window window;
} VkXlibSurfaceCreateInfoKHR;

static VkResult (*pvkCreateXlibSurfaceKHR)(VkInstance, const VkXlibSurfaceCreateInfoKHR *, const VkAllocationCallbacks *, VkSurfaceKHR *);
static void (*pvkDestroySurfaceKHR)(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks *);
static VkBool32 (*pvkGetPhysicalDeviceXlibPresentationSupportKHR)(VkPhysicalDevice, uint32_t, Display *, VisualID);

static const struct vulkan_driver_funcs x11drv_vulkan_driver_funcs;

static inline struct wine_vk_surface *surface_from_handle(VkSurfaceKHR handle)
{
    return (struct wine_vk_surface *)(uintptr_t)handle;
}

static void wine_vk_surface_release( struct wine_vk_surface *surface )
{
    if (InterlockedDecrement(&surface->ref))
        return;

    if (surface->entry.next)
    {
        pthread_mutex_lock(&vulkan_mutex);
        list_remove(&surface->entry);
        pthread_mutex_unlock(&vulkan_mutex);
    }

    destroy_client_window( surface->hwnd, surface->window );
    free(surface);
}

void destroy_vk_surface( HWND hwnd )
{
    struct wine_vk_surface *surface, *next;

    pthread_mutex_lock( &vulkan_mutex );
    LIST_FOR_EACH_ENTRY_SAFE( surface, next, &surface_list, struct wine_vk_surface, entry )
    {
        if (surface->hwnd != hwnd) continue;
        surface->hwnd_thread_id = 0;
        surface->hwnd = NULL;
    }
    pthread_mutex_unlock( &vulkan_mutex );
}

void vulkan_thread_detach(void)
{
    struct wine_vk_surface *surface, *next;
    DWORD thread_id = GetCurrentThreadId();

    pthread_mutex_lock(&vulkan_mutex);
    LIST_FOR_EACH_ENTRY_SAFE(surface, next, &surface_list, struct wine_vk_surface, entry)
    {
        if (surface->hwnd_thread_id != thread_id)
            continue;

        TRACE("Detaching surface %p, hwnd %p.\n", surface, surface->hwnd);
        XReparentWindow(gdi_display, surface->window, get_dummy_parent(), 0, 0);
        XSync(gdi_display, False);
    }
    pthread_mutex_unlock(&vulkan_mutex);
}

static VkResult X11DRV_vulkan_surface_create( HWND hwnd, VkInstance instance, VkSurfaceKHR *surface )
{
    VkResult res;
    VkXlibSurfaceCreateInfoKHR create_info_host;
    struct wine_vk_surface *x11_surface;

    TRACE( "%p %p %p\n", hwnd, instance, surface );

    /* TODO: support child window rendering. */
    if (NtUserGetAncestor( hwnd, GA_PARENT ) != NtUserGetDesktopWindow())
    {
        FIXME("Application requires child window rendering, which is not implemented yet!\n");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    x11_surface = calloc(1, sizeof(*x11_surface));
    if (!x11_surface)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    x11_surface->ref = 1;
    x11_surface->hwnd = hwnd;
    x11_surface->window = create_client_window( hwnd, &default_visual, default_colormap );
    x11_surface->hwnd_thread_id = NtUserGetWindowThread( x11_surface->hwnd, NULL );

    if (!x11_surface->window)
    {
        ERR( "Failed to allocate client window for hwnd=%p\n", hwnd );

        /* VK_KHR_win32_surface only allows out of host and device memory as errors. */
        free(x11_surface);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    create_info_host.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    create_info_host.pNext = NULL;
    create_info_host.flags = 0; /* reserved */
    create_info_host.dpy = gdi_display;
    create_info_host.window = x11_surface->window;

    res = pvkCreateXlibSurfaceKHR( instance, &create_info_host, NULL /* allocator */, &x11_surface->host_surface );
    if (res != VK_SUCCESS)
    {
        ERR("Failed to create Xlib surface, res=%d\n", res);
        destroy_client_window( x11_surface->hwnd, x11_surface->window );
        free(x11_surface);
        return res;
    }

    pthread_mutex_lock(&vulkan_mutex);
    list_add_tail(&surface_list, &x11_surface->entry);
    pthread_mutex_unlock(&vulkan_mutex);

    *surface = (uintptr_t)x11_surface;

    TRACE("Created surface=0x%s\n", wine_dbgstr_longlong(*surface));
    return VK_SUCCESS;
}

static void X11DRV_vulkan_surface_destroy( HWND hwnd, VkInstance instance, VkSurfaceKHR surface )
{
    struct wine_vk_surface *x11_surface = surface_from_handle(surface);

    TRACE( "%p %p 0x%s\n", hwnd, instance, wine_dbgstr_longlong(surface) );

    pvkDestroySurfaceKHR( instance, x11_surface->host_surface, NULL /* allocator */ );
    wine_vk_surface_release(x11_surface);
}

static void X11DRV_vulkan_surface_presented(HWND hwnd, VkResult result)
{
}

static VkBool32 X11DRV_vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice phys_dev,
        uint32_t index)
{
    TRACE("%p %u\n", phys_dev, index);

    return pvkGetPhysicalDeviceXlibPresentationSupportKHR(phys_dev, index, gdi_display,
            default_visual.visual->visualid);
}

static const char *X11DRV_get_host_surface_extension(void)
{
    return "VK_KHR_xlib_surface";
}

static VkSurfaceKHR X11DRV_wine_get_host_surface( VkSurfaceKHR surface )
{
    struct wine_vk_surface *x11_surface = surface_from_handle(surface);

    TRACE("0x%s\n", wine_dbgstr_longlong(surface));

    return x11_surface->host_surface;
}

static const struct vulkan_driver_funcs x11drv_vulkan_driver_funcs =
{
    .p_vulkan_surface_create = X11DRV_vulkan_surface_create,
    .p_vulkan_surface_destroy = X11DRV_vulkan_surface_destroy,
    .p_vulkan_surface_presented = X11DRV_vulkan_surface_presented,

    .p_vkGetPhysicalDeviceWin32PresentationSupportKHR = X11DRV_vkGetPhysicalDeviceWin32PresentationSupportKHR,
    .p_get_host_surface_extension = X11DRV_get_host_surface_extension,
    .p_wine_get_host_surface = X11DRV_wine_get_host_surface,
};

UINT X11DRV_VulkanInit( UINT version, void *vulkan_handle, struct vulkan_driver_funcs *driver_funcs )
{
    if (version != WINE_VULKAN_DRIVER_VERSION)
    {
        ERR( "version mismatch, win32u wants %u but driver has %u\n", version, WINE_VULKAN_DRIVER_VERSION );
        return STATUS_INVALID_PARAMETER;
    }

    init_recursive_mutex( &vulkan_mutex );

#define LOAD_FUNCPTR( f ) if (!(p##f = dlsym( vulkan_handle, #f ))) return STATUS_PROCEDURE_NOT_FOUND;
    LOAD_FUNCPTR( vkCreateXlibSurfaceKHR );
    LOAD_FUNCPTR( vkDestroySurfaceKHR );
    LOAD_FUNCPTR( vkGetPhysicalDeviceXlibPresentationSupportKHR );
#undef LOAD_FUNCPTR

    *driver_funcs = x11drv_vulkan_driver_funcs;
    return STATUS_SUCCESS;
}

#else /* No vulkan */

UINT X11DRV_VulkanInit( UINT version, void *vulkan_handle, struct vulkan_driver_funcs *driver_funcs )
{
    ERR( "Wine was built without Vulkan support.\n" );
    return STATUS_NOT_IMPLEMENTED;
}

void destroy_vk_surface( HWND hwnd )
{
}

void vulkan_thread_detach(void)
{
}

#endif /* SONAME_LIBVULKAN */
