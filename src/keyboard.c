/*      $Id$

        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation; either version 2, or (at your option)
        any later version.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program; if not, write to the Free Software
        Foundation, Inc., Inc., 51 Franklin Street, Fifth Floor, Boston,
        MA 02110-1301, USA.


        oroborus      - (c) 2001 Ken Lynch
        xfwm4         - (c) 2002-2011 Olivier Fourdan,
                            2008 Jannis Pohlmann
        xfwm4-wayland - (c) 2018-2022 adlo

 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/Xlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libxfce4util/libxfce4util.h>
#include <xkbcommon/xkbcommon.h>
#include "keyboard.h"

#define ARRAY_SIZE(arr) ((sizeof(arr) / sizeof(*(arr))))

#define MODIFIER_MASK (GDK_SHIFT_MASK | \
                       GDK_CONTROL_MASK | \
                       GDK_MOD1_MASK | \
                       GDK_MOD2_MASK | \
                       GDK_MOD3_MASK | \
                       GDK_MOD4_MASK | \
                       GDK_MOD5_MASK)

#define IGNORE_MASK (0x2000 | \
                     GDK_LOCK_MASK | \
                     GDK_HYPER_MASK | \
                     GDK_SUPER_MASK | \
                     GDK_META_MASK | \
                     NumLockMask | \
                     ScrollLockMask)

unsigned int AltMask;
unsigned int MetaMask;
unsigned int NumLockMask;
unsigned int ScrollLockMask;
unsigned int SuperMask;
unsigned int HyperMask;

#define BUTTON_GRAB_MASK \
    ButtonPressMask | \
    ButtonReleaseMask | \
    PointerMotionMask | \
    PointerMotionHintMask

#define KEYCODE_GRAB_MASK \
    KeyPressMask | \
    KeyReleaseMask

static KeyCode
getKeycode (DisplayInfo *display_info, const char *str)
{
  Display *dpy = display_info->dpy;  
  GdkModifierType keysym;
  
  const char *rules = NULL;
    const char *model = NULL;
    const char *layout_ = NULL;
    const char *variant = NULL;
    const char *options = NULL;
    bool keysym_mode = false;
    int err = EXIT_FAILURE;
    struct xkb_context *ctx = NULL;
    char *endp;
    long val;
    uint32_t codepoint;
    xkb_keysym_t xkb_keysym;
    int ret;
    char name[200];
    struct xkb_keymap *keymap = NULL;  
    xkb_keycode_t min_keycode, max_keycode;
    xkb_mod_index_t num_mods;

  gtk_accelerator_parse (str, &keysym, NULL);
  g_print ("%d%s", keysym, "\n");
  if (GDK_IS_X11_DISPLAY (display_info->gdisplay))
  {
  return XKeysymToKeycode (dpy, keysym);
  }
  else
  {
    ret = xkb_keysym_get_name(keysym, name, sizeof(name));
    if (ret < 0 || (size_t) ret >= sizeof(name)) {
        fprintf(stderr, "Failed to get name of keysym\n");
        goto err;
    }
    
      ctx = display_info->xkb_context;
    if (!ctx) {
        fprintf(stderr, "Failed to create XKB context\n");
        goto err;
    }
    
    keymap = display_info->xkb_keymap;
    if (!keymap) {
        fprintf(stderr, "Failed to create XKB keymap\n");
        goto err;
    }

    printf("keysym: %s (%#x)\n", name, keysym);
    printf("%-8s %-9s %-8s %-20s %-7s\n",
           "KEYCODE", "KEY NAME", "LAYOUT", "LAYOUT NAME", "LEVEL#");

    min_keycode = xkb_keymap_min_keycode(keymap);
    max_keycode = xkb_keymap_max_keycode(keymap);
    num_mods = xkb_keymap_num_mods(keymap);
    for (xkb_keycode_t keycode = min_keycode; keycode <= max_keycode; keycode++) {
        const char *key_name;
        xkb_layout_index_t num_layouts;

        key_name = xkb_keymap_key_get_name(keymap, keycode);
        if (!key_name) {
            continue;
        }

        num_layouts = xkb_keymap_num_layouts_for_key(keymap, keycode);
        for (xkb_layout_index_t layout = 0; layout < num_layouts; layout++) {
            const char *layout_name;
            xkb_level_index_t num_levels;

            layout_name = xkb_keymap_layout_get_name(keymap, layout);
            if (!layout_name) {
                layout_name = "?";
            }

            num_levels = xkb_keymap_num_levels_for_key(keymap, keycode, layout);
            for (xkb_level_index_t level = 0; level < num_levels; level++) {
                int num_syms;
                const xkb_keysym_t *syms;
                size_t num_masks;
                xkb_mod_mask_t masks[100];

                num_syms = xkb_keymap_key_get_syms_by_level(
                    keymap, keycode, layout, level, &syms
                );
                if (num_syms != 1) {
                    continue;
                }
                if (syms[0] != keysym) {
                    continue;
                }

                    printf("%-8u %-9s %-8u %-20s %-7u",
                           keycode, key_name, layout + 1, layout_name, level + 1);
                    
                    return keycode;
            }
        }
    }
    
    err:    
    return 0;
  }
}

static gboolean
addModifierMap (guint *map, guint mask)
{
    if (!mask)
    {
        return FALSE;
    }

    *map |= mask;

    return TRUE;
}

gboolean
getModifierMap (const char *str, guint *map)
{
    gboolean ret;

    ret = TRUE;
    gtk_accelerator_parse (str, NULL, map);

    ret = TRUE;
    if ((*map & GDK_SUPER_MASK) == GDK_SUPER_MASK)
    {
        ret &= addModifierMap (map, SuperMask);
    }

    if ((*map & GDK_HYPER_MASK) == GDK_HYPER_MASK)
    {
        ret &= addModifierMap (map, HyperMask);
    }

    if ((*map & GDK_META_MASK) == GDK_META_MASK)
    {
        ret &= addModifierMap (map, MetaMask);
    }

    *map &= MODIFIER_MASK & ~IGNORE_MASK;

    return ret;
}

void
parseKeyString (DisplayInfo *display_info, MyKey * key, const char *str)
{
  Display *dpy = display_info->dpy;  
  g_return_if_fail (key != NULL);

    TRACE ("key string=%s", str);

    key->keycode = 0;
    key->modifier = 0;

    if (str == NULL)
    {
        return;
    }

    if (!g_ascii_strcasecmp (str, "none"))
    {
        return;
    }

    if (!getModifierMap (str, &key->modifier))
    {
        g_message (_("Unsupported keyboard modifier '%s'"), str);
        key->modifier = 0;
        return;
    }

    key->keycode = getKeycode (display_info, str);
  
    g_print ("\nkey->keycode: %d\n", key->keycode);
  
    g_print ("key->modifier: %b\n\n", key->modifier);
  
    TRACE ("keycode = 0x%x, modifier = 0x%x", key->keycode, key->modifier);
}

int
getModifierKeysyms (int modifier, xkb_keysym_t **syms_out)
{
  if (modifier & AltMask)
    {
      *syms_out = malloc (sizeof(xkb_keysym_t) * 2);
      (*syms_out)[0] = XKB_KEY_Alt_L;
      (*syms_out)[1] = XKB_KEY_Alt_R;
      return 2;
    }
  else
    {
      *syms_out = malloc (sizeof(xkb_keysym_t));
      (*syms_out)[0] = 0;
      return 0;
    }
}

gboolean
grabKey (XfwmDevices *devices, Display *dpy, MyKey *key, Window w)
{
    int status;

    TRACE ("window 0x%lx", w);

    status = GrabSuccess;
    if (key->keycode)
    {
        if (key->modifier != 0)
        {
            status |= xfwm_device_grab_keycode (devices, dpy, key->keycode,
                                                key->modifier,
                                                w, TRUE, KEYCODE_GRAB_MASK,
                                                GrabModeAsync, GrabModeSync);
        }

        /* Here we grab all combinations of well known modifiers */
        status |= xfwm_device_grab_keycode (devices, dpy, key->keycode,
                                            key->modifier | ScrollLockMask,
                                            w, TRUE, KEYCODE_GRAB_MASK,
                                            GrabModeAsync, GrabModeSync);
        status |= xfwm_device_grab_keycode (devices, dpy, key->keycode,
                                            key->modifier | NumLockMask,
                                            w, TRUE, KEYCODE_GRAB_MASK,
                                            GrabModeAsync, GrabModeSync);
        status |= xfwm_device_grab_keycode (devices, dpy, key->keycode,
                                            key->modifier | LockMask,
                                            w, TRUE, KEYCODE_GRAB_MASK,
                                            GrabModeAsync, GrabModeSync);
        status |= xfwm_device_grab_keycode (devices, dpy, key->keycode,
                                            key->modifier | ScrollLockMask | NumLockMask,
                                            w, TRUE, KEYCODE_GRAB_MASK,
                                            GrabModeAsync, GrabModeSync);
        status |= xfwm_device_grab_keycode (devices, dpy, key->keycode,
                                            key->modifier | ScrollLockMask | LockMask,
                                            w, TRUE, KEYCODE_GRAB_MASK,
                                            GrabModeAsync, GrabModeSync);
        status |= xfwm_device_grab_keycode (devices, dpy, key->keycode,
                                            key->modifier | LockMask | NumLockMask,
                                            w, TRUE, KEYCODE_GRAB_MASK,
                                            GrabModeAsync, GrabModeSync);
        status |= xfwm_device_grab_keycode (devices, dpy, key->keycode,
                                            key->modifier | ScrollLockMask | LockMask | NumLockMask,
                                            w, TRUE, KEYCODE_GRAB_MASK,
                                            GrabModeAsync, GrabModeSync);
    }

    return (status == GrabSuccess);
}

void
ungrabKeys (XfwmDevices *devices, Display *dpy, Window w)
{
    TRACE ("window 0x%lx", w);

    xfwm_device_ungrab_keycode (devices, dpy, AnyKey, AnyModifier, w);
}

gboolean
grabButton (XfwmDevices *devices, Display *dpy, guint button, guint modifier, Window w)
{
    gboolean result;

    TRACE ("window 0x%lx", w);

    result = TRUE;
    if (modifier == AnyModifier)
    {
        result &= xfwm_device_grab_button (devices, dpy, button,
                                           AnyModifier,
                                           w, FALSE, BUTTON_GRAB_MASK,
                                           GrabModeSync, GrabModeAsync, None, None);
    }
    else
    {
        /* Here we grab all combinations of well known modifiers */
        result &= xfwm_device_grab_button (devices, dpy, button,
                                           modifier,
                                           w, FALSE, BUTTON_GRAB_MASK,
                                           GrabModeSync, GrabModeAsync, None, None);
        result &= xfwm_device_grab_button (devices, dpy, button,
                                           modifier | ScrollLockMask,
                                           w, FALSE, BUTTON_GRAB_MASK,
                                           GrabModeSync, GrabModeAsync, None, None);
        result &= xfwm_device_grab_button (devices, dpy, button,
                                           modifier | NumLockMask,
                                           w, FALSE, BUTTON_GRAB_MASK,
                                           GrabModeSync, GrabModeAsync, None, None);
        result &= xfwm_device_grab_button (devices, dpy, button,
                                           modifier | LockMask,
                                           w, FALSE, BUTTON_GRAB_MASK,
                                           GrabModeSync, GrabModeAsync, None, None);
        result &= xfwm_device_grab_button (devices, dpy, button,
                                           modifier | ScrollLockMask | NumLockMask,
                                           w, FALSE, BUTTON_GRAB_MASK,
                                           GrabModeSync, GrabModeAsync, None, None);
        result &= xfwm_device_grab_button (devices, dpy, button,
                                           modifier | ScrollLockMask | LockMask,
                                           w, FALSE, BUTTON_GRAB_MASK,
                                           GrabModeSync, GrabModeAsync, None, None);
        result &= xfwm_device_grab_button (devices, dpy, button,
                                           modifier | LockMask | NumLockMask,
                                           w, FALSE, BUTTON_GRAB_MASK,
                                           GrabModeSync, GrabModeAsync, None, None);
        result &= xfwm_device_grab_button (devices, dpy, button,
                                           modifier | ScrollLockMask | LockMask | NumLockMask,
                                           w, FALSE, BUTTON_GRAB_MASK,
                                           GrabModeSync, GrabModeAsync, None, None);
    }

    return result;
}

void
ungrabButton (XfwmDevices *devices, Display *dpy, guint button, guint modifier, Window w)
{
    TRACE ("window 0x%lx", w);

    if (modifier == AnyModifier)
    {
        xfwm_device_ungrab_button (devices, dpy, button, AnyModifier, w);
    }
    else
    {
        /* Here we ungrab all combinations of well known modifiers */
        xfwm_device_ungrab_button (devices, dpy, button, modifier, w);
        xfwm_device_ungrab_button (devices, dpy, button, modifier | ScrollLockMask, w);
        xfwm_device_ungrab_button (devices, dpy, button, modifier | NumLockMask, w);
        xfwm_device_ungrab_button (devices, dpy, button, modifier | LockMask, w);
        xfwm_device_ungrab_button (devices, dpy, button, modifier | ScrollLockMask | NumLockMask, w);
        xfwm_device_ungrab_button (devices, dpy, button, modifier | ScrollLockMask | LockMask, w);
        xfwm_device_ungrab_button (devices, dpy, button, modifier | LockMask | NumLockMask, w);
        xfwm_device_ungrab_button (devices, dpy, button, modifier | ScrollLockMask | LockMask | NumLockMask, w);
    }
}

void
initModifiersWayland ()
{
  AltMask = Mod1Mask;
}

void
initModifiers (Display * dpy)
{
    XModifierKeymap *modmap;
    KeySym *keymap;
    int min_keycode, max_keycode, keycode;
    int keysyms_per_keycode;
    int i;

    AltMask = 0;
    MetaMask = 0;
    NumLockMask = 0;
    ScrollLockMask = 0;
    SuperMask = 0;
    HyperMask = 0;
    keysyms_per_keycode = 0;
    min_keycode = 0;
    max_keycode = 0;

    XDisplayKeycodes (dpy, &min_keycode, &max_keycode);
    modmap = XGetModifierMapping (dpy);
    keymap = XGetKeyboardMapping (dpy, min_keycode, max_keycode - min_keycode + 1, &keysyms_per_keycode);

    if (modmap && keymap)
    {
        for (i = 3 * modmap->max_keypermod; i < 8 * modmap->max_keypermod; i++)
        {
            keycode = modmap->modifiermap[i];
            if ((keycode >= min_keycode) && (keycode <= max_keycode))
            {
                int j;
                KeySym *syms = keymap + (keycode - min_keycode) * keysyms_per_keycode;

                for (j = 0; j < keysyms_per_keycode; j++)
                {
                    if (!NumLockMask && (syms[j] == XK_Num_Lock))
                    {
                        NumLockMask = (1 << (i / modmap->max_keypermod));
                    }
                    else if (!ScrollLockMask && (syms[j] == XK_Scroll_Lock))
                    {
                        ScrollLockMask = (1 << (i / modmap->max_keypermod));
                    }
                    else if (!AltMask && ((syms[j] == XK_Alt_L) || (syms[j] == XK_Alt_R)))
                    {
                        AltMask = (1 << (i / modmap->max_keypermod));
                    }
                    else if (!SuperMask && ((syms[j] == XK_Super_L) || (syms[j] == XK_Super_R)))
                    {
                        SuperMask = (1 << (i / modmap->max_keypermod));
                    }
                    else if (!HyperMask && ((syms[j] == XK_Hyper_L) || (syms[j] == XK_Hyper_R)))
                    {
                        HyperMask = (1 << (i / modmap->max_keypermod));
                    }
                    else if (!MetaMask && ((syms[j] == XK_Meta_L) || (syms[j] == XK_Meta_R)))
                    {
                        MetaMask = (1 << (i / modmap->max_keypermod));
                    }
                }
            }
        }
    }

    /* Cleanup memory */
    if (modmap)
    {
        XFreeModifiermap (modmap);
    }

    if (keymap)
    {
        XFree (keymap);
    }

    /* In case we didn't find AltMask, use Mod1Mask */
    if (AltMask == 0)
    {
        AltMask = Mod1Mask;
    }
}
