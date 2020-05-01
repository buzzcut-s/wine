/*
 * Common implementation of IVideoWindow
 *
 * Copyright 2012 Aric Stewart, CodeWeavers
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

#include "quartz_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

static const WCHAR class_name[] = L"wine_quartz_window";

static inline struct video_window *impl_from_IVideoWindow(IVideoWindow *iface)
{
    return CONTAINING_RECORD(iface, struct video_window, IVideoWindow_iface);
}

static LRESULT CALLBACK WndProcW(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct video_window *window = (struct video_window *)GetWindowLongPtrW(hwnd, 0);

    if (!window)
        return DefWindowProcW(hwnd, message, wparam, lparam);

    switch (message)
    {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEACTIVATE:
    case WM_MOUSEMOVE:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCMBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN:
    case WM_NCMBUTTONUP:
    case WM_NCMOUSEMOVE:
    case WM_NCRBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        if (window->hwndDrain)
        {
            PostMessageW(window->hwndDrain, message, wparam, lparam);
            return 0;
        }
        break;
    case WM_SIZE:
        if (window->ops->resize)
            return window->ops->resize(window, LOWORD(lparam), HIWORD(lparam));
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

HRESULT video_window_create_window(struct video_window *window)
{
    WNDCLASSW winclass = {0};

    winclass.lpfnWndProc = WndProcW;
    winclass.cbWndExtra = sizeof(window);
    winclass.hbrBackground = GetStockObject(BLACK_BRUSH);
    winclass.lpszClassName = class_name;
    if (!RegisterClassW(&winclass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        ERR("Unable to register window class: %u\n", GetLastError());
        return E_FAIL;
    }

    if (!(window->hwnd = CreateWindowExW(0, class_name, L"ActiveMovie Window",
            WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            NULL, NULL, NULL, NULL)))
    {
        ERR("Unable to create window\n");
        return E_FAIL;
    }

    SetWindowLongPtrW(window->hwnd, 0, (LONG_PTR)window);

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_QueryInterface(IVideoWindow *iface, REFIID iid, void **out)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    return IUnknown_QueryInterface(window->pFilter->outer_unk, iid, out);
}

ULONG WINAPI BaseControlWindowImpl_AddRef(IVideoWindow *iface)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    return IUnknown_AddRef(window->pFilter->outer_unk);
}

ULONG WINAPI BaseControlWindowImpl_Release(IVideoWindow *iface)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    return IUnknown_Release(window->pFilter->outer_unk);
}

HRESULT WINAPI BaseControlWindowImpl_GetTypeInfoCount(IVideoWindow *iface, UINT *count)
{
    TRACE("iface %p, count %p.\n", iface, count);
    *count = 1;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_GetTypeInfo(IVideoWindow *iface, UINT index,
        LCID lcid, ITypeInfo **typeinfo)
{
    TRACE("iface %p, index %u, lcid %#x, typeinfo %p.\n", iface, index, lcid, typeinfo);
    return strmbase_get_typeinfo(IVideoWindow_tid, typeinfo);
}

HRESULT WINAPI BaseControlWindowImpl_GetIDsOfNames(IVideoWindow *iface, REFIID iid,
        LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    ITypeInfo *typeinfo;
    HRESULT hr;

    TRACE("iface %p, iid %s, names %p, count %u, lcid %#x, ids %p.\n",
            iface, debugstr_guid(iid), names, count, lcid, ids);

    if (SUCCEEDED(hr = strmbase_get_typeinfo(IVideoWindow_tid, &typeinfo)))
    {
        hr = ITypeInfo_GetIDsOfNames(typeinfo, names, count, ids);
        ITypeInfo_Release(typeinfo);
    }
    return hr;
}

HRESULT WINAPI BaseControlWindowImpl_Invoke(IVideoWindow *iface, DISPID id, REFIID iid, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *excepinfo, UINT *error_arg)
{
    ITypeInfo *typeinfo;
    HRESULT hr;

    TRACE("iface %p, id %d, iid %s, lcid %#x, flags %#x, params %p, result %p, excepinfo %p, error_arg %p.\n",
            iface, id, debugstr_guid(iid), lcid, flags, params, result, excepinfo, error_arg);

    if (SUCCEEDED(hr = strmbase_get_typeinfo(IVideoWindow_tid, &typeinfo)))
    {
        hr = ITypeInfo_Invoke(typeinfo, iface, id, flags, params, result, excepinfo, error_arg);
        ITypeInfo_Release(typeinfo);
    }
    return hr;
}

HRESULT WINAPI BaseControlWindowImpl_put_Caption(IVideoWindow *iface, BSTR caption)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, caption %s.\n", window, debugstr_w(caption));

    if (!SetWindowTextW(window->hwnd, caption))
        return E_FAIL;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_Caption(IVideoWindow *iface, BSTR *caption)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    WCHAR *str;
    int len;

    TRACE("window %p, caption %p.\n", window, caption);

    *caption = NULL;

    len = GetWindowTextLengthW(window->hwnd) + 1;
    if (!(str = heap_alloc(len * sizeof(WCHAR))))
        return E_OUTOFMEMORY;

    GetWindowTextW(window->hwnd, str, len);
    *caption = SysAllocString(str);
    heap_free(str);
    return *caption ? S_OK : E_OUTOFMEMORY;
}

HRESULT WINAPI BaseControlWindowImpl_put_WindowStyle(IVideoWindow *iface, LONG style)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, style %#x.\n", window, style);

    if (style & (WS_DISABLED|WS_HSCROLL|WS_MAXIMIZE|WS_MINIMIZE|WS_VSCROLL))
        return E_INVALIDARG;

    SetWindowLongW(window->hwnd, GWL_STYLE, style);
    SetWindowPos(window->hwnd, 0, 0, 0, 0, 0,
            SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_WindowStyle(IVideoWindow *iface, LONG *style)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, style %p.\n", window, style);

    *style = GetWindowLongW(window->hwnd, GWL_STYLE);
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_WindowStyleEx(IVideoWindow *iface, LONG style)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, style %#x.\n", window, style);

    if (!SetWindowLongW(window->hwnd, GWL_EXSTYLE, style))
        return E_FAIL;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_WindowStyleEx(IVideoWindow *iface, LONG *style)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, style %p.\n", window, style);

    *style = GetWindowLongW(window->hwnd, GWL_EXSTYLE);
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_AutoShow(IVideoWindow *iface, LONG AutoShow)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    TRACE("(%p/%p)->(%d)\n", This, iface, AutoShow);

    This->AutoShow = AutoShow;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_AutoShow(IVideoWindow *iface, LONG *AutoShow)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    TRACE("(%p/%p)->(%p)\n", This, iface, AutoShow);

    *AutoShow = This->AutoShow;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_WindowState(IVideoWindow *iface, LONG state)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, state %#x.\n", window, state);

    ShowWindow(window->hwnd, state);
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_WindowState(IVideoWindow *iface, LONG *state)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    DWORD style;

    TRACE("window %p, state %p.\n", window, state);

    style = GetWindowLongPtrW(window->hwnd, GWL_STYLE);
    if (!(style & WS_VISIBLE))
        *state = SW_HIDE;
    else if (style & WS_MINIMIZE)
        *state = SW_MINIMIZE;
    else if (style & WS_MAXIMIZE)
        *state = SW_MAXIMIZE;
    else
        *state = SW_SHOW;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_BackgroundPalette(IVideoWindow *iface, LONG BackgroundPalette)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    FIXME("(%p/%p)->(%d): stub !!!\n", This, iface, BackgroundPalette);

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_BackgroundPalette(IVideoWindow *iface, LONG *pBackgroundPalette)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    FIXME("(%p/%p)->(%p): stub !!!\n", This, iface, pBackgroundPalette);

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_Visible(IVideoWindow *iface, LONG visible)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, visible %d.\n", window, visible);

    ShowWindow(window->hwnd, visible ? SW_SHOW : SW_HIDE);
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_Visible(IVideoWindow *iface, LONG *visible)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, visible %p.\n", window, visible);

    *visible = IsWindowVisible(window->hwnd) ? OATRUE : OAFALSE;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_Left(IVideoWindow *iface, LONG left)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, left %d.\n", window, left);

    GetWindowRect(window->hwnd, &rect);
    if (!SetWindowPos(window->hwnd, NULL, left, rect.top, 0, 0,
            SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE))
        return E_FAIL;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_Left(IVideoWindow *iface, LONG *left)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, left %p.\n", window, left);

    GetWindowRect(window->hwnd, &rect);
    *left = rect.left;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_Width(IVideoWindow *iface, LONG width)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, width %d.\n", window, width);

    GetWindowRect(window->hwnd, &rect);
    if (!SetWindowPos(window->hwnd, NULL, 0, 0, width, rect.bottom - rect.top,
            SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE))
        return E_FAIL;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_Width(IVideoWindow *iface, LONG *width)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, width %p.\n", window, width);

    GetWindowRect(window->hwnd, &rect);
    *width = rect.right - rect.left;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_Top(IVideoWindow *iface, LONG top)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, top %d.\n", window, top);

    GetWindowRect(window->hwnd, &rect);
    if (!SetWindowPos(window->hwnd, NULL, rect.left, top, 0, 0,
            SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE))
        return E_FAIL;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_Top(IVideoWindow *iface, LONG *top)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, top %p.\n", window, top);

    GetWindowRect(window->hwnd, &rect);
    *top = rect.top;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_Height(IVideoWindow *iface, LONG height)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, height %d.\n", window, height);

    GetWindowRect(window->hwnd, &rect);
    if (!SetWindowPos(window->hwnd, NULL, 0, 0, rect.right - rect.left,
            height, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE))
        return E_FAIL;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_Height(IVideoWindow *iface, LONG *height)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, height %p.\n", window, height);

    GetWindowRect(window->hwnd, &rect);
    *height = rect.bottom - rect.top;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_Owner(IVideoWindow *iface, OAHWND owner)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    HWND hwnd = window->hwnd;

    TRACE("window %p, owner %#lx.\n", window, owner);

    /* Make sure we are marked as WS_CHILD before reparenting ourselves, so that
     * we do not steal focus. LEGO Island depends on this. */

    window->hwndOwner = (HWND)owner;
    if (owner)
        SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_CHILD);
    else
        SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) & ~WS_CHILD);
    SetParent(hwnd, (HWND)owner);

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_Owner(IVideoWindow *iface, OAHWND *Owner)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    TRACE("(%p/%p)->(%p)\n", This, iface, Owner);

    *(HWND*)Owner = This->hwndOwner;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_MessageDrain(IVideoWindow *iface, OAHWND Drain)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    TRACE("(%p/%p)->(%08x)\n", This, iface, (DWORD) Drain);

    This->hwndDrain = (HWND)Drain;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_MessageDrain(IVideoWindow *iface, OAHWND *Drain)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    TRACE("(%p/%p)->(%p)\n", This, iface, Drain);

    *Drain = (OAHWND)This->hwndDrain;

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_BorderColor(IVideoWindow *iface, LONG *Color)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    FIXME("(%p/%p)->(%p): stub !!!\n", This, iface, Color);

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_put_BorderColor(IVideoWindow *iface, LONG Color)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    FIXME("(%p/%p)->(%d): stub !!!\n", This, iface, Color);

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_get_FullScreenMode(IVideoWindow *iface, LONG *FullScreenMode)
{
    TRACE("(%p)->(%p)\n", iface, FullScreenMode);

    return E_NOTIMPL;
}

HRESULT WINAPI BaseControlWindowImpl_put_FullScreenMode(IVideoWindow *iface, LONG FullScreenMode)
{
    TRACE("(%p)->(%d)\n", iface, FullScreenMode);
    return E_NOTIMPL;
}

HRESULT WINAPI BaseControlWindowImpl_SetWindowForeground(IVideoWindow *iface, LONG focus)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    UINT flags = SWP_NOMOVE | SWP_NOSIZE;

    TRACE("window %p, focus %d.\n", window, focus);

    if (focus != OAFALSE && focus != OATRUE)
        return E_INVALIDARG;

    if (!window->pPin->peer)
        return VFW_E_NOT_CONNECTED;

    if (!focus)
        flags |= SWP_NOACTIVATE;
    SetWindowPos(window->hwnd, HWND_TOP, 0, 0, 0, 0, flags);

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_SetWindowPosition(IVideoWindow *iface,
        LONG left, LONG top, LONG width, LONG height)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, left %d, top %d, width %d, height %d.\n", window, left, top, width, height);

    if (!SetWindowPos(window->hwnd, NULL, left, top, width, height, SWP_NOACTIVATE | SWP_NOZORDER))
        return E_FAIL;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_GetWindowPosition(IVideoWindow *iface,
        LONG *left, LONG *top, LONG *width, LONG *height)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, left %p, top %p, width %p, height %p.\n", window, left, top, width, height);

    GetWindowRect(window->hwnd, &rect);
    *left = rect.left;
    *top = rect.top;
    *width = rect.right - rect.left;
    *height = rect.bottom - rect.top;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_NotifyOwnerMessage(IVideoWindow *iface,
        OAHWND hwnd, LONG message, LONG_PTR wparam, LONG_PTR lparam)
{
    struct video_window *window = impl_from_IVideoWindow(iface);

    TRACE("window %p, hwnd %#lx, message %#x, wparam %#lx, lparam %#lx.\n",
            window, hwnd, message, wparam, lparam);

    /* That these messages are forwarded, and no others, is stated by the
     * DirectX documentation, and supported by manual testing. */
    switch (message)
    {
    case WM_ACTIVATEAPP:
    case WM_DEVMODECHANGE:
    case WM_DISPLAYCHANGE:
    case WM_PALETTECHANGED:
    case WM_PALETTEISCHANGING:
    case WM_QUERYNEWPALETTE:
    case WM_SYSCOLORCHANGE:
        SendMessageW(window->hwnd, message, wparam, lparam);
        break;
    }

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_GetMinIdealImageSize(IVideoWindow *iface, LONG *width, LONG *height)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, width %p, height %p.\n", window, width, height);

    rect = window->ops->get_default_rect(window);
    *width = rect.right - rect.left;
    *height = rect.bottom - rect.top;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_GetMaxIdealImageSize(IVideoWindow *iface, LONG *width, LONG *height)
{
    struct video_window *window = impl_from_IVideoWindow(iface);
    RECT rect;

    TRACE("window %p, width %p, height %p.\n", window, width, height);

    rect = window->ops->get_default_rect(window);
    *width = rect.right - rect.left;
    *height = rect.bottom - rect.top;
    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_GetRestorePosition(IVideoWindow *iface, LONG *pLeft, LONG *pTop, LONG *pWidth, LONG *pHeight)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    FIXME("(%p/%p)->(%p, %p, %p, %p): stub !!!\n", This, iface, pLeft, pTop, pWidth, pHeight);

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_HideCursor(IVideoWindow *iface, LONG HideCursor)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    FIXME("(%p/%p)->(%d): stub !!!\n", This, iface, HideCursor);

    return S_OK;
}

HRESULT WINAPI BaseControlWindowImpl_IsCursorHidden(IVideoWindow *iface, LONG *CursorHidden)
{
    struct video_window *This = impl_from_IVideoWindow(iface);

    FIXME("(%p/%p)->(%p): stub !!!\n", This, iface, CursorHidden);

    return S_OK;
}

void video_window_unregister_class(void)
{
    if (!UnregisterClassW(class_name, NULL) && GetLastError() != ERROR_CLASS_DOES_NOT_EXIST)
        ERR("Failed to unregister class, error %u.\n", GetLastError());
}

void video_window_init(struct video_window *window, const IVideoWindowVtbl *vtbl,
        struct strmbase_filter *owner, struct strmbase_pin *pin, const struct video_window_ops *ops)
{
    memset(window, 0, sizeof(*window));
    window->ops = ops;
    window->IVideoWindow_iface.lpVtbl = vtbl;
    window->AutoShow = OATRUE;
    window->pFilter = owner;
    window->pPin = pin;
}

void video_window_cleanup(struct video_window *window)
{
    if (window->hwnd)
    {
        /* Media Player Classic deadlocks if WM_PARENTNOTIFY is sent, so clear
         * the child style first. Just like Windows, we don't actually unparent
         * the window, to prevent extra focus events from being generated since
         * it would become top-level for a brief period before being destroyed. */
        SetWindowLongW(window->hwnd, GWL_STYLE, GetWindowLongW(window->hwnd, GWL_STYLE) & ~WS_CHILD);

        SendMessageW(window->hwnd, WM_CLOSE, 0, 0);
        window->hwnd = NULL;
    }
}