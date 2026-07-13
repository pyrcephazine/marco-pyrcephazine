/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2026 Pierce Zhang
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/* Full-screen workspace overview used by Marco's workspace expo mode. */

#include <config.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <cairo-xlib.h>
#include <math.h>

#include "workspace-expo.h"
#include "compositor.h"
#include "errors.h"
#include "prefs.h"
#include "../core/display-private.h"
#include "../core/screen-private.h"
#include "../core/stack.h"
#include "../core/window-private.h"
#include "../core/workspace.h"

#define EXPO_MAX_SNAPSHOT_WIDTH  720
#define EXPO_MAX_SNAPSHOT_HEIGHT 480
#define EXPO_MAX_BACKGROUND_WIDTH  1920
#define EXPO_MAX_BACKGROUND_HEIGHT 1080
#define EXPO_BLURRED_BACKGROUND_WIDTH  240
#define EXPO_BLURRED_BACKGROUND_HEIGHT 180
#define EXPO_MIN_WINDOW_SIZE       8

typedef struct
{
  Window xwindow;
  int workspace_index;
  MetaRectangle rect;
  char *title;
  GdkPixbuf *icon;
  cairo_surface_t *snapshot;
  guint sticky : 1;
} MetaWorkspaceExpoWindow;

typedef struct
{
  PangoLayout *layout;
  int width;
  int height;
} MetaWorkspaceExpoLabel;

struct _MetaWorkspaceExpo
{
  MetaScreen *screen;
  GtkWidget *window;
  GtkWidget *drawing_area;
  GList *windows;
  cairo_surface_t *wallpaper;
  cairo_surface_t *blurred_wallpaper;

  int *workspace_grid;
  GdkRectangle *cards;
  MetaWorkspaceExpoLabel *labels;
  MetaWorkspaceExpoLayout layout;
  int grid_area;
  int n_workspaces;
  int selected_workspace;
  int drop_workspace;
  int scale;
  int popup_width;
  int popup_height;
  guint redraw_tick_id;

  MetaWorkspaceExpoWindow *pressed_window;
  int pressed_workspace;
  double press_x;
  double press_y;
  double pointer_x;
  double pointer_y;
  double drag_offset_x;
  double drag_offset_y;
  guint button_down : 1;
  guint dragging : 1;
};

static gboolean workspace_expo_draw (GtkWidget *widget,
                                     cairo_t   *cr,
                                     gpointer   data);

static gboolean
workspace_expo_redraw_tick (GtkWidget     *widget,
                            GdkFrameClock *frame_clock,
                            gpointer       data)
{
  MetaWorkspaceExpo *expo = data;

  (void) frame_clock;
  expo->redraw_tick_id = 0;
  gtk_widget_queue_draw (widget);

  return G_SOURCE_REMOVE;
}

static void
workspace_expo_queue_redraw (MetaWorkspaceExpo *expo)
{
  if (expo == NULL || expo->drawing_area == NULL ||
      expo->redraw_tick_id != 0)
    return;

  expo->redraw_tick_id = gtk_widget_add_tick_callback (
    expo->drawing_area, workspace_expo_redraw_tick, expo, NULL);
}

gboolean
meta_workspace_expo_calculate_layout (int                      width,
                                      int                      height,
                                      int                      screen_width,
                                      int                      screen_height,
                                      int                      rows,
                                      int                      cols,
                                      MetaWorkspaceExpoLayout *layout)
{
  double available_width;
  double available_height;
  double aspect;
  double cell_width;
  double cell_height;
  double grid_width;
  double grid_height;
  double margin;
  double min_dimension;

  g_return_val_if_fail (layout != NULL, FALSE);

  if (width <= 0 || height <= 0 ||
      screen_width <= 0 || screen_height <= 0 ||
      rows <= 0 || cols <= 0)
    return FALSE;

  min_dimension = MIN (width, height);
  margin = CLAMP (min_dimension / 28.0, 12.0, 32.0);
  layout->gap = CLAMP (MIN ((double) width / (cols * 12.0),
                            (double) height / (rows * 12.0)),
                       4.0, 20.0);
  layout->rows = rows;
  layout->cols = cols;

  available_width = MAX (1.0,
                         width - 2.0 * margin -
                         (cols - 1) * layout->gap);
  available_height = MAX (1.0,
                          height - 2.0 * margin -
                          (rows - 1) * layout->gap);
  aspect = (double) screen_width / screen_height;
  cell_width = available_width / cols;
  cell_height = available_height / rows;
  layout->card_width = MIN (cell_width, cell_height * aspect);
  layout->card_height = layout->card_width / aspect;

  grid_width = cols * layout->card_width + (cols - 1) * layout->gap;
  grid_height = rows * layout->card_height + (rows - 1) * layout->gap;
  layout->start_x = (width - grid_width) / 2.0;
  layout->start_y = (height - grid_height) / 2.0;

  return TRUE;
}

void
meta_workspace_expo_layout_cell_rect (const MetaWorkspaceExpoLayout *layout,
                                      int                            cell,
                                      GdkRectangle                  *rect)
{
  g_return_if_fail (layout != NULL);
  g_return_if_fail (rect != NULL);
  g_return_if_fail (cell >= 0 && cell < layout->rows * layout->cols);

  rect->x = (int) floor (layout->start_x +
                         (cell % layout->cols) *
                         (layout->card_width + layout->gap));
  rect->y = (int) floor (layout->start_y +
                         (cell / layout->cols) *
                         (layout->card_height + layout->gap));
  rect->width = MAX (1, (int) floor (layout->card_width));
  rect->height = MAX (1, (int) floor (layout->card_height));
}

int
meta_workspace_expo_layout_workspace_at (const MetaWorkspaceExpoLayout *layout,
                                         const int                     *workspace_grid,
                                         int                            grid_area,
                                         double                         x,
                                         double                         y)
{
  int cell;

  g_return_val_if_fail (layout != NULL, -1);
  g_return_val_if_fail (workspace_grid != NULL, -1);

  for (cell = 0; cell < grid_area; cell++)
    {
      GdkRectangle rect;

      if (workspace_grid[cell] < 0)
        continue;

      meta_workspace_expo_layout_cell_rect (layout, cell, &rect);
      if (x >= rect.x && x < rect.x + rect.width &&
          y >= rect.y && y < rect.y + rect.height)
        return workspace_grid[cell];
    }

  return -1;
}

void
meta_workspace_expo_transform_window_rect (int                  screen_width,
                                           int                  screen_height,
                                           const MetaRectangle *window_rect,
                                           const GdkRectangle  *card_rect,
                                           GdkRectangle        *result)
{
  double width_ratio;
  double height_ratio;

  g_return_if_fail (screen_width > 0 && screen_height > 0);
  g_return_if_fail (window_rect != NULL);
  g_return_if_fail (card_rect != NULL);
  g_return_if_fail (result != NULL);

  width_ratio = (double) card_rect->width / screen_width;
  height_ratio = (double) card_rect->height / screen_height;
  result->x = card_rect->x + (int) floor (window_rect->x * width_ratio);
  result->y = card_rect->y + (int) floor (window_rect->y * height_ratio);
  result->width = MAX (EXPO_MIN_WINDOW_SIZE,
                       (int) floor (window_rect->width * width_ratio));
  result->height = MAX (EXPO_MIN_WINDOW_SIZE,
                        (int) floor (window_rect->height * height_ratio));
}

static gboolean
window_is_eligible (MetaWindow *window)
{
  return !window->unmanaging &&
         !window->minimized &&
         !window->skip_pager &&
         window->type != META_WINDOW_DESKTOP &&
         window->type != META_WINDOW_DOCK &&
         meta_window_showing_on_its_workspace (window);
}

static cairo_surface_t *
copy_xlib_surface (MetaDisplay     *display,
                   cairo_surface_t *source,
                   cairo_format_t   format,
                   int              max_width,
                   int              max_height)
{
  cairo_surface_t *copy;
  cairo_t *cr;
  int source_width;
  int source_height;
  int width;
  int height;
  double ratio;
  int error_code;

  if (source == NULL ||
      cairo_surface_status (source) != CAIRO_STATUS_SUCCESS ||
      cairo_surface_get_type (source) != CAIRO_SURFACE_TYPE_XLIB)
    {
      if (source != NULL)
        cairo_surface_destroy (source);
      return NULL;
    }

  source_width = cairo_xlib_surface_get_width (source);
  source_height = cairo_xlib_surface_get_height (source);
  if (source_width <= 0 || source_height <= 0)
    {
      cairo_surface_destroy (source);
      return NULL;
    }

  ratio = MIN (1.0,
               MIN ((double) max_width / source_width,
                    (double) max_height / source_height));
  width = MAX (1, (int) floor (source_width * ratio));
  height = MAX (1, (int) floor (source_height * ratio));

  copy = cairo_image_surface_create (format, width, height);
  if (cairo_surface_status (copy) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (copy);
      cairo_surface_destroy (source);
      return NULL;
    }

  meta_error_trap_push (display);
  cr = cairo_create (copy);
  cairo_scale (cr, ratio, ratio);
  cairo_set_source_surface (cr, source, 0, 0);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_flush (copy);
  error_code = meta_error_trap_pop_with_return (display, FALSE);

  cairo_surface_destroy (source);

  if (error_code != Success ||
      cairo_surface_status (copy) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (copy);
      return NULL;
    }

  return copy;
}

static cairo_surface_t *
copy_window_snapshot (MetaWindow *window,
                      int         max_width,
                      int         max_height)
{
  cairo_surface_t *source;

  if (window->display->compositor == NULL)
    return NULL;

  source = meta_compositor_get_window_surface (window->display->compositor,
                                               window);
  return copy_xlib_surface (window->display, source, CAIRO_FORMAT_ARGB32,
                            max_width, max_height);
}

static cairo_surface_t *
copy_desktop_background (MetaWindow *window,
                         int         max_width,
                         int         max_height)
{
  cairo_surface_t *source;

  if (window->display->compositor == NULL)
    return NULL;

  source = meta_compositor_get_window_surface (window->display->compositor,
                                               window);
  return copy_xlib_surface (window->display, source, CAIRO_FORMAT_RGB24,
                            max_width, max_height);
}

static Pixmap
get_root_background_pixmap (MetaScreen *screen)
{
  static const char *property_names[] = {
    "_XROOTPMAP_ID",
    "_XSETROOT_ID",
    "ESETROOT_PMAP_ID"
  };
  MetaDisplay *display;
  Display *xdisplay;
  Window xroot;
  Atom pixmap_atom;
  guint i;

  display = screen->display;
  xdisplay = display->xdisplay;
  xroot = screen->xroot;
  pixmap_atom = XInternAtom (xdisplay, "PIXMAP", False);

  for (i = 0; i < G_N_ELEMENTS (property_names); i++)
    {
      Atom property;
      Atom actual_type;
      int actual_format;
      unsigned long nitems;
      unsigned long bytes_after;
      unsigned char *data = NULL;
      int status;
      int error_code;
      Pixmap pixmap = None;

      property = XInternAtom (xdisplay, property_names[i], False);
      meta_error_trap_push (display);
      status = XGetWindowProperty (xdisplay, xroot, property,
                                   0, 1, False, pixmap_atom,
                                   &actual_type, &actual_format,
                                   &nitems, &bytes_after, &data);
      error_code = meta_error_trap_pop_with_return (display, FALSE);

      if (status == Success && error_code == Success &&
          actual_type == pixmap_atom && actual_format == 32 &&
          nitems == 1 && data != NULL)
        pixmap = (Pixmap) *((unsigned long *) data);

      if (data != NULL)
        XFree (data);

      if (pixmap != None)
        return pixmap;
    }

  return None;
}

static cairo_surface_t *
copy_root_background (MetaScreen *screen,
                      int         max_width,
                      int         max_height)
{
  MetaDisplay *display;
  Display *xdisplay;
  Pixmap pixmap;
  Window root_return;
  int x;
  int y;
  unsigned int pixmap_width;
  unsigned int pixmap_height;
  unsigned int border_width;
  unsigned int depth;
  cairo_surface_t *source;
  cairo_surface_t *copy;
  cairo_t *cr;
  cairo_pattern_t *pattern;
  double ratio;
  int width;
  int height;
  int error_code;
  Status geometry_status;

  display = screen->display;
  xdisplay = display->xdisplay;
  pixmap = get_root_background_pixmap (screen);
  if (pixmap == None)
    return NULL;

  meta_error_trap_push (display);
  geometry_status = XGetGeometry (xdisplay, pixmap, &root_return,
                                  &x, &y, &pixmap_width, &pixmap_height,
                                  &border_width, &depth);
  error_code = meta_error_trap_pop_with_return (display, FALSE);
  if (geometry_status == 0 || error_code != Success ||
      pixmap_width == 0 || pixmap_height == 0)
    return NULL;

  source = cairo_xlib_surface_create (xdisplay, pixmap,
                                      DefaultVisual (xdisplay,
                                                     screen->number),
                                      pixmap_width, pixmap_height);
  if (cairo_surface_status (source) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (source);
      return NULL;
    }

  ratio = MIN (1.0,
               MIN ((double) max_width / screen->rect.width,
                    (double) max_height / screen->rect.height));
  width = MAX (1, (int) floor (screen->rect.width * ratio));
  height = MAX (1, (int) floor (screen->rect.height * ratio));
  copy = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
  if (cairo_surface_status (copy) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (copy);
      cairo_surface_destroy (source);
      return NULL;
    }

  meta_error_trap_push (display);
  cr = cairo_create (copy);
  cairo_scale (cr,
               (double) width / screen->rect.width,
               (double) height / screen->rect.height);
  cairo_set_source_surface (cr, source, 0, 0);
  pattern = cairo_get_source (cr);
  cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);
  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BEST);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_flush (copy);
  error_code = meta_error_trap_pop_with_return (display, FALSE);

  cairo_surface_destroy (source);
  if (error_code != Success ||
      cairo_surface_status (copy) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (copy);
      return NULL;
    }

  return copy;
}

static cairo_surface_t *
create_blurred_background (cairo_surface_t *wallpaper)
{
  cairo_surface_t *blurred;
  cairo_t *cr;
  cairo_pattern_t *pattern;
  int source_width;
  int source_height;
  int width;
  int height;
  double ratio;

  if (wallpaper == NULL)
    return NULL;

  source_width = cairo_image_surface_get_width (wallpaper);
  source_height = cairo_image_surface_get_height (wallpaper);
  ratio = MIN (1.0,
               MIN ((double) EXPO_BLURRED_BACKGROUND_WIDTH / source_width,
                    (double) EXPO_BLURRED_BACKGROUND_HEIGHT / source_height));
  width = MAX (1, (int) floor (source_width * ratio));
  height = MAX (1, (int) floor (source_height * ratio));
  blurred = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
  if (cairo_surface_status (blurred) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (blurred);
      return NULL;
    }

  cr = cairo_create (blurred);
  cairo_scale (cr, ratio, ratio);
  cairo_set_source_surface (cr, wallpaper, 0, 0);
  pattern = cairo_get_source (cr);
  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BEST);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_destroy (cr);

  if (cairo_surface_status (blurred) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (blurred);
      return NULL;
    }

  return blurred;
}

static MetaWorkspaceExpoWindow *
workspace_expo_window_new (MetaWindow *window,
                           int         card_width,
                           int         card_height)
{
  MetaWorkspaceExpoWindow *entry;
  int snapshot_width;
  int snapshot_height;

  entry = g_new0 (MetaWorkspaceExpoWindow, 1);
  entry->xwindow = window->xwindow;
  entry->workspace_index = window->workspace != NULL ?
    meta_workspace_index (window->workspace) : -1;
  entry->sticky = window->on_all_workspaces || window->always_sticky;
  meta_window_get_outer_rect (window, &entry->rect);
  snapshot_width = CLAMP (
    (int) ceil ((double) entry->rect.width * card_width /
                window->screen->rect.width),
    EXPO_MIN_WINDOW_SIZE, card_width);
  snapshot_height = CLAMP (
    (int) ceil ((double) entry->rect.height * card_height /
                window->screen->rect.height),
    EXPO_MIN_WINDOW_SIZE, card_height);
  entry->snapshot = copy_window_snapshot (window,
                                          snapshot_width,
                                          snapshot_height);
  if (entry->snapshot == NULL)
    {
      entry->title = g_strdup (window->title != NULL ? window->title : "");
      entry->icon = window->icon != NULL ? g_object_ref (window->icon) : NULL;
    }

  return entry;
}

static void
workspace_expo_window_free (gpointer data)
{
  MetaWorkspaceExpoWindow *entry = data;

  g_free (entry->title);
  if (entry->icon != NULL)
    g_object_unref (entry->icon);
  if (entry->snapshot != NULL)
    cairo_surface_destroy (entry->snapshot);
  g_free (entry);
}

static gboolean
workspace_expo_get_card_rect (MetaWorkspaceExpo *expo,
                              int                workspace_index,
                              GdkRectangle      *rect)
{
  if (expo == NULL || rect == NULL || expo->cards == NULL ||
      workspace_index < 0 || workspace_index >= expo->n_workspaces)
    return FALSE;

  *rect = expo->cards[workspace_index];
  return TRUE;
}

static int
workspace_expo_workspace_at_point (MetaWorkspaceExpo *expo,
                                   double             x,
                                   double             y)
{
  if (expo == NULL || expo->workspace_grid == NULL)
    return -1;

  return meta_workspace_expo_layout_workspace_at (&expo->layout,
                                                   expo->workspace_grid,
                                                   expo->grid_area,
                                                   x, y);
}

static void
workspace_expo_get_window_rect (MetaWorkspaceExpo       *expo,
                                MetaWorkspaceExpoWindow *entry,
                                int                      workspace_index,
                                GdkRectangle            *rect)
{
  GdkRectangle card;

  if (!workspace_expo_get_card_rect (expo, workspace_index, &card))
    {
      rect->x = rect->y = rect->width = rect->height = 0;
      return;
    }

  meta_workspace_expo_transform_window_rect (expo->screen->rect.width,
                                             expo->screen->rect.height,
                                             &entry->rect,
                                             &card,
                                             rect);
}

static gboolean
workspace_expo_get_drag_rect_at (MetaWorkspaceExpo *expo,
                                 double             x,
                                 double             y,
                                 GdkRectangle      *rect)
{
  if (expo == NULL || expo->pressed_window == NULL ||
      expo->pressed_workspace < 0 || rect == NULL)
    return FALSE;

  workspace_expo_get_window_rect (expo, expo->pressed_window,
                                  expo->pressed_workspace, rect);
  rect->x = (int) floor (x - expo->drag_offset_x);
  rect->y = (int) floor (y - expo->drag_offset_y);

  return rect->width > 0 && rect->height > 0;
}

static gboolean
entry_is_on_workspace (MetaWorkspaceExpoWindow *entry,
                       int                      workspace_index)
{
  return entry->sticky || entry->workspace_index == workspace_index;
}

static MetaWorkspaceExpoWindow *
workspace_expo_window_at_point (MetaWorkspaceExpo *expo,
                                int                workspace_index,
                                double             x,
                                double             y)
{
  GList *link;
  GdkRectangle card;

  if (workspace_expo_get_card_rect (expo, workspace_index, &card) &&
      card.height >= 36 && y < card.y + 32)
    return NULL;

  link = g_list_last (expo->windows);
  while (link != NULL)
    {
      MetaWorkspaceExpoWindow *entry = link->data;
      GdkRectangle rect;

      if (entry_is_on_workspace (entry, workspace_index))
        {
          workspace_expo_get_window_rect (expo, entry, workspace_index, &rect);
          if (x >= rect.x && x < rect.x + rect.width &&
              y >= rect.y && y < rect.y + rect.height)
            return entry;
        }

      link = link->prev;
    }

  return NULL;
}

static void
draw_fallback_window (cairo_t                 *cr,
                      MetaWorkspaceExpoWindow *entry,
                      const GdkRectangle      *rect,
                      double                   alpha)
{
  double icon_scale;
  double icon_width;
  double icon_height;
  double icon_x;
  double icon_y;

  cairo_set_source_rgba (cr, 0.20, 0.22, 0.26, alpha);
  cairo_rectangle (cr, rect->x, rect->y, rect->width, rect->height);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 0.38, 0.42, 0.50, alpha);
  cairo_rectangle (cr, rect->x, rect->y, rect->width,
                   MIN (18, rect->height));
  cairo_fill (cr);

  if (entry->title[0] != '\0' && rect->width >= 45 && rect->height >= 18)
    {
      cairo_save (cr);
      cairo_rectangle (cr, rect->x + 2, rect->y + 1,
                       MAX (1, rect->width - 4), 16);
      cairo_clip (cr);
      cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                              CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size (cr, 8.0);
      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.90 * alpha);
      cairo_move_to (cr, rect->x + 3, rect->y + 11);
      cairo_show_text (cr, entry->title);
      cairo_restore (cr);
    }

  if (entry->icon == NULL)
    return;

  icon_scale = MIN ((double) MAX (1, rect->width - 8) /
                    gdk_pixbuf_get_width (entry->icon),
                    (double) MAX (1, rect->height - 8) /
                    gdk_pixbuf_get_height (entry->icon));
  icon_scale = MIN (icon_scale, 1.0);
  icon_width = gdk_pixbuf_get_width (entry->icon) * icon_scale;
  icon_height = gdk_pixbuf_get_height (entry->icon) * icon_scale;
  icon_x = rect->x + (rect->width - icon_width) / 2.0;
  icon_y = rect->y + (rect->height - icon_height) / 2.0;

  cairo_save (cr);
  cairo_translate (cr, icon_x, icon_y);
  cairo_scale (cr, icon_scale, icon_scale);
  gdk_cairo_set_source_pixbuf (cr, entry->icon, 0, 0);
  cairo_paint_with_alpha (cr, alpha);
  cairo_restore (cr);
}

static void
draw_window_entry (cairo_t                 *cr,
                   MetaWorkspaceExpoWindow *entry,
                   const GdkRectangle      *rect,
                   double                   alpha)
{
  int surface_width;
  int surface_height;

  cairo_save (cr);
  cairo_rectangle (cr, rect->x, rect->y, rect->width, rect->height);
  cairo_clip (cr);

  if (entry->snapshot != NULL)
    {
      surface_width = cairo_image_surface_get_width (entry->snapshot);
      surface_height = cairo_image_surface_get_height (entry->snapshot);
      cairo_translate (cr, rect->x, rect->y);
      cairo_scale (cr,
                   (double) rect->width / surface_width,
                   (double) rect->height / surface_height);
      cairo_set_source_surface (cr, entry->snapshot, 0, 0);
      cairo_paint_with_alpha (cr, alpha);
    }
  else
    {
      draw_fallback_window (cr, entry, rect, alpha);
    }

  cairo_restore (cr);

  cairo_set_line_width (cr, 1.0);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.34 * alpha);
  cairo_rectangle (cr, rect->x + 0.5, rect->y + 0.5,
                   MAX (0, rect->width - 1), MAX (0, rect->height - 1));
  cairo_stroke (cr);

  if (entry->sticky && rect->width >= 16 && rect->height >= 16)
    {
      cairo_set_source_rgba (cr, 0.95, 0.78, 0.24, alpha);
      cairo_arc (cr, rect->x + rect->width - 7,
                 rect->y + 7, 4, 0, 2 * G_PI);
      cairo_fill (cr);
    }
}

static void
draw_workspace_label (MetaWorkspaceExpo  *expo,
                      cairo_t            *cr,
                      int                 workspace_index,
                      const GdkRectangle *card)
{
  MetaWorkspaceExpoLabel *label;
  double badge_width;
  double badge_height;

  if (expo->labels == NULL || card->height < 36 || card->width < 40)
    return;

  label = &expo->labels[workspace_index];
  badge_width = MIN (card->width, label->width + 16.0);
  badge_height = MIN (card->height, label->height + 8.0);

  cairo_set_source_rgba (cr, 0.04, 0.05, 0.07, 0.72);
  cairo_rectangle (cr, card->x, card->y, badge_width, badge_height);
  cairo_fill (cr);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.94);
  cairo_move_to (cr, card->x + 8, card->y + 4);
  pango_cairo_show_layout (cr, label->layout);
}

static void
paint_surface_in_rect (cairo_t                *cr,
                       cairo_surface_t        *surface,
                       const GdkRectangle     *rect)
{
  cairo_pattern_t *pattern;
  int surface_width;
  int surface_height;

  g_return_if_fail (surface != NULL);
  g_return_if_fail (rect != NULL);

  surface_width = cairo_image_surface_get_width (surface);
  surface_height = cairo_image_surface_get_height (surface);

  cairo_save (cr);
  cairo_rectangle (cr, rect->x, rect->y, rect->width, rect->height);
  cairo_clip (cr);
  cairo_translate (cr, rect->x, rect->y);
  cairo_scale (cr,
               (double) rect->width / surface_width,
               (double) rect->height / surface_height);
  cairo_set_source_surface (cr, surface, 0, 0);
  pattern = cairo_get_source (cr);
  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BILINEAR);
  cairo_paint (cr);
  cairo_restore (cr);
}

static void
draw_workspace (MetaWorkspaceExpo *expo,
                cairo_t           *cr,
                int                workspace_index)
{
  GdkRectangle card;
  GList *link;
  gboolean drop_target;

  if (!workspace_expo_get_card_rect (expo, workspace_index, &card))
    return;

  drop_target = expo->dragging &&
                expo->drop_workspace == workspace_index;

  cairo_save (cr);
  cairo_rectangle (cr, card.x, card.y, card.width, card.height);
  cairo_clip (cr);
  if (expo->wallpaper != NULL)
    paint_surface_in_rect (cr, expo->wallpaper, &card);
  else
    {
      cairo_set_source_rgb (cr, 0.10, 0.115, 0.14);
      cairo_paint (cr);
    }
  cairo_set_source_rgba (cr, 0.015, 0.02, 0.03, 0.20);
  cairo_paint (cr);

  link = expo->windows;
  while (link != NULL)
    {
      MetaWorkspaceExpoWindow *entry = link->data;

      if (entry_is_on_workspace (entry, workspace_index))
        {
          GdkRectangle rect;
          double alpha = (expo->dragging && entry == expo->pressed_window &&
                          (!entry->sticky &&
                           entry->workspace_index == workspace_index)) ?
                         0.28 : 1.0;

          workspace_expo_get_window_rect (expo, entry, workspace_index, &rect);
          draw_window_entry (cr, entry, &rect, alpha);
        }

      link = link->next;
    }

  draw_workspace_label (expo, cr, workspace_index, &card);
  cairo_restore (cr);

  cairo_set_line_width (cr, 1.0);
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_rectangle (cr, card.x + 0.5, card.y + 0.5,
                   card.width - 1, card.height - 1);
  cairo_stroke (cr);

  if (workspace_index == expo->selected_workspace || drop_target)
    {
      double outline_width = drop_target ? 3.0 : 2.0;
      double inset = outline_width / 2.0;

      cairo_set_line_width (cr, outline_width);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_rectangle (cr, card.x + inset, card.y + inset,
                       card.width - outline_width,
                       card.height - outline_width);
      cairo_stroke (cr);
    }
}

static gboolean
workspace_expo_draw (GtkWidget *widget,
                     cairo_t   *cr,
                     gpointer   data)
{
  MetaWorkspaceExpo *expo = data;
  GtkAllocation allocation;
  GdkRectangle background_rect;
  cairo_surface_t *background;
  int i;

  gtk_widget_get_allocation (widget, &allocation);
  background_rect.x = 0;
  background_rect.y = 0;
  background_rect.width = allocation.width;
  background_rect.height = allocation.height;
  background = expo->blurred_wallpaper != NULL ?
    expo->blurred_wallpaper : expo->wallpaper;

  if (background != NULL)
    paint_surface_in_rect (cr, background, &background_rect);
  else
    {
      cairo_set_source_rgb (cr, 0.035, 0.042, 0.055);
      cairo_paint (cr);
    }
  cairo_set_source_rgba (cr, 0.01, 0.015, 0.025, 0.42);
  cairo_paint (cr);

  for (i = 0; i < expo->n_workspaces; i++)
    draw_workspace (expo, cr, i);

  if (expo->dragging && expo->pressed_window != NULL)
    {
      GdkRectangle rect;

      if (workspace_expo_get_drag_rect_at (expo,
                                           expo->pointer_x,
                                           expo->pointer_y,
                                           &rect))
        {
          cairo_save (cr);
          cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.45);
          cairo_rectangle (cr, rect.x + 6, rect.y + 8,
                           rect.width, rect.height);
          cairo_fill (cr);
          draw_window_entry (cr, expo->pressed_window, &rect, 0.92);
          cairo_restore (cr);
        }
    }

  return TRUE;
}

MetaWorkspaceExpo *
meta_workspace_expo_new (MetaScreen *screen)
{
  MetaWorkspaceExpo *expo;
  MetaWorkspaceLayout workspace_layout;
  GdkDisplay *gdk_display;
  GdkScreen *gdk_screen;
  GList *windows;
  GList *link;
  int rows;
  int cols;
  int cell;
  int card_pixel_width;
  int card_pixel_height;
  int background_width;
  int background_height;
  int current_workspace;

  g_return_val_if_fail (screen != NULL, NULL);

  expo = g_new0 (MetaWorkspaceExpo, 1);
  expo->screen = screen;
  expo->n_workspaces = meta_screen_get_n_workspaces (screen);
  current_workspace = meta_workspace_index (screen->active_workspace);
  expo->selected_workspace = current_workspace;
  expo->pressed_workspace = -1;
  expo->drop_workspace = -1;

  meta_screen_calc_workspace_layout (screen, expo->n_workspaces,
                                     current_workspace, &workspace_layout);
  rows = workspace_layout.rows;
  cols = workspace_layout.cols;
  expo->grid_area = workspace_layout.grid_area;
#if GLIB_CHECK_VERSION(2, 68, 0)
  expo->workspace_grid = g_memdup2 (workspace_layout.grid,
                                    sizeof (int) * workspace_layout.grid_area);
#else
  expo->workspace_grid = g_memdup (workspace_layout.grid,
                                   sizeof (int) * workspace_layout.grid_area);
#endif
  meta_screen_free_workspace_layout (&workspace_layout);

  gdk_display = gdk_x11_lookup_xdisplay (screen->display->xdisplay);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  gdk_screen = gdk_display_get_screen (gdk_display, screen->number);
G_GNUC_END_IGNORE_DEPRECATIONS
  expo->window = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_window_set_screen (GTK_WINDOW (expo->window), gdk_screen);
  gtk_window_set_decorated (GTK_WINDOW (expo->window), FALSE);
  gtk_window_set_resizable (GTK_WINDOW (expo->window), FALSE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (expo->window), TRUE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (expo->window), TRUE);
  gtk_widget_set_app_paintable (expo->window, TRUE);

  expo->drawing_area = gtk_drawing_area_new ();
  gtk_container_add (GTK_CONTAINER (expo->window), expo->drawing_area);
  g_signal_connect (expo->drawing_area, "draw",
                    G_CALLBACK (workspace_expo_draw), expo);

  gtk_widget_realize (expo->window);
  expo->scale = MAX (1, gtk_widget_get_scale_factor (expo->window));
  expo->popup_width = MAX (1, screen->rect.width / expo->scale);
  expo->popup_height = MAX (1, screen->rect.height / expo->scale);
  gtk_widget_set_size_request (expo->drawing_area,
                               expo->popup_width,
                               expo->popup_height);
  gtk_window_set_default_size (GTK_WINDOW (expo->window),
                               expo->popup_width,
                               expo->popup_height);
  gtk_window_move (GTK_WINDOW (expo->window), 0, 0);
  gtk_window_resize (GTK_WINDOW (expo->window),
                     expo->popup_width,
                     expo->popup_height);

  if (!meta_workspace_expo_calculate_layout (expo->popup_width,
                                             expo->popup_height,
                                             screen->rect.width,
                                             screen->rect.height,
                                             rows, cols, &expo->layout))
    {
      meta_workspace_expo_free (expo);
      return NULL;
    }

  expo->cards = g_new0 (GdkRectangle, expo->n_workspaces);
  for (cell = 0; cell < expo->grid_area; cell++)
    {
      int workspace_index = expo->workspace_grid[cell];

      if (workspace_index >= 0 && workspace_index < expo->n_workspaces)
        meta_workspace_expo_layout_cell_rect (&expo->layout, cell,
                                              &expo->cards[workspace_index]);
    }

  expo->labels = g_new0 (MetaWorkspaceExpoLabel, expo->n_workspaces);
  for (cell = 0; cell < expo->n_workspaces; cell++)
    {
      const char *name = meta_prefs_get_workspace_name (cell);

      expo->labels[cell].layout = gtk_widget_create_pango_layout (
        expo->drawing_area, NULL);
      pango_layout_set_markup (expo->labels[cell].layout, name, -1);
      pango_layout_set_ellipsize (expo->labels[cell].layout,
                                  PANGO_ELLIPSIZE_END);
      pango_layout_set_width (expo->labels[cell].layout,
                              MAX (1, expo->cards[cell].width - 16) *
                              PANGO_SCALE);
      pango_layout_get_pixel_size (expo->labels[cell].layout,
                                   &expo->labels[cell].width,
                                   &expo->labels[cell].height);
    }

  card_pixel_width = CLAMP (
    (int) ceil (expo->layout.card_width * expo->scale),
    EXPO_MIN_WINDOW_SIZE, EXPO_MAX_SNAPSHOT_WIDTH);
  card_pixel_height = CLAMP (
    (int) ceil (expo->layout.card_height * expo->scale),
    EXPO_MIN_WINDOW_SIZE, EXPO_MAX_SNAPSHOT_HEIGHT);
  background_width = MIN (EXPO_MAX_BACKGROUND_WIDTH,
                          MAX (EXPO_BLURRED_BACKGROUND_WIDTH,
                               (int) ceil (expo->layout.card_width *
                                           expo->scale)));
  background_height = MIN (EXPO_MAX_BACKGROUND_HEIGHT,
                           MAX (EXPO_BLURRED_BACKGROUND_HEIGHT,
                                (int) ceil (expo->layout.card_height *
                                            expo->scale)));

  expo->wallpaper = copy_root_background (screen,
                                          background_width,
                                          background_height);
  windows = meta_stack_list_windows (screen->stack, NULL);
  for (link = windows; link != NULL; link = link->next)
    {
      MetaWindow *window = link->data;

      if (expo->wallpaper == NULL &&
          window->type == META_WINDOW_DESKTOP)
        expo->wallpaper = copy_desktop_background (window,
                                                   background_width,
                                                   background_height);

      if (window_is_eligible (window))
        expo->windows = g_list_prepend (
          expo->windows,
          workspace_expo_window_new (window,
                                     card_pixel_width,
                                     card_pixel_height));
    }
  g_list_free (windows);
  expo->windows = g_list_reverse (expo->windows);
  expo->blurred_wallpaper = create_blurred_background (expo->wallpaper);

  return expo;
}

void
meta_workspace_expo_free (MetaWorkspaceExpo *expo)
{
  int i;

  if (expo == NULL)
    return;

  if (expo->redraw_tick_id != 0 && expo->drawing_area != NULL)
    gtk_widget_remove_tick_callback (expo->drawing_area,
                                     expo->redraw_tick_id);
  if (expo->labels != NULL)
    {
      for (i = 0; i < expo->n_workspaces; i++)
        g_clear_object (&expo->labels[i].layout);
      g_free (expo->labels);
    }
  if (expo->window != NULL)
    gtk_widget_destroy (expo->window);
  g_list_free_full (expo->windows, workspace_expo_window_free);
  if (expo->blurred_wallpaper != NULL)
    cairo_surface_destroy (expo->blurred_wallpaper);
  if (expo->wallpaper != NULL)
    cairo_surface_destroy (expo->wallpaper);
  g_free (expo->cards);
  g_free (expo->workspace_grid);
  g_free (expo);
}

void
meta_workspace_expo_show (MetaWorkspaceExpo *expo)
{
  g_return_if_fail (expo != NULL);

  gtk_widget_show_all (expo->window);
  gtk_window_move (GTK_WINDOW (expo->window), 0, 0);
  gtk_window_resize (GTK_WINDOW (expo->window),
                     expo->popup_width,
                     expo->popup_height);
  meta_workspace_expo_raise (expo);
}

void
meta_workspace_expo_raise (MetaWorkspaceExpo *expo)
{
  GdkWindow *window;

  if (expo == NULL || !gtk_widget_get_realized (expo->window))
    return;

  window = gtk_widget_get_window (expo->window);
  if (window != NULL)
    {
      XRaiseWindow (expo->screen->display->xdisplay,
                    gdk_x11_window_get_xid (window));
      XFlush (expo->screen->display->xdisplay);
    }
}

void
meta_workspace_expo_button_press (MetaWorkspaceExpo *expo,
                                  int                root_x,
                                  int                root_y,
                                  unsigned int       button)
{
  int workspace_index;
  int old_selected_workspace;
  GdkRectangle rect;

  if (expo == NULL || button != Button1)
    return;

  expo->press_x = (double) root_x / expo->scale;
  expo->press_y = (double) root_y / expo->scale;
  expo->pointer_x = expo->press_x;
  expo->pointer_y = expo->press_y;
  workspace_index = workspace_expo_workspace_at_point (expo,
                                                       expo->press_x,
                                                       expo->press_y);
  if (workspace_index < 0)
    return;

  old_selected_workspace = expo->selected_workspace;
  expo->button_down = TRUE;
  expo->pressed_workspace = workspace_index;
  expo->pressed_window = workspace_expo_window_at_point (expo,
                                                         workspace_index,
                                                         expo->press_x,
                                                         expo->press_y);
  expo->selected_workspace = workspace_index;

  if (expo->pressed_window != NULL)
    {
      workspace_expo_get_window_rect (expo, expo->pressed_window,
                                      workspace_index, &rect);
      expo->drag_offset_x = expo->press_x - rect.x;
      expo->drag_offset_y = expo->press_y - rect.y;
    }

  if (old_selected_workspace != workspace_index)
    workspace_expo_queue_redraw (expo);
}

void
meta_workspace_expo_motion (MetaWorkspaceExpo *expo,
                            int                root_x,
                            int                root_y)
{
  if (expo == NULL || !expo->button_down)
    return;

  expo->pointer_x = (double) root_x / expo->scale;
  expo->pointer_y = (double) root_y / expo->scale;

  if (!expo->dragging && expo->pressed_window != NULL &&
      !expo->pressed_window->sticky &&
      gtk_drag_check_threshold (expo->drawing_area,
                                (int) expo->press_x, (int) expo->press_y,
                                (int) expo->pointer_x, (int) expo->pointer_y))
    expo->dragging = TRUE;

  if (!expo->dragging)
    return;

  expo->drop_workspace = workspace_expo_workspace_at_point (
    expo, expo->pointer_x, expo->pointer_y);
  workspace_expo_queue_redraw (expo);
}

gboolean
meta_workspace_expo_button_release (MetaWorkspaceExpo       *expo,
                                    int                      root_x,
                                    int                      root_y,
                                    unsigned int             button,
                                    MetaWorkspaceExpoAction *action)
{
  int target_workspace;

  g_return_val_if_fail (action != NULL, FALSE);

  action->type = META_WORKSPACE_EXPO_ACTION_NONE;
  action->xwindow = None;
  action->workspace_index = -1;

  if (expo == NULL || button != Button1 || !expo->button_down)
    return FALSE;

  expo->pointer_x = (double) root_x / expo->scale;
  expo->pointer_y = (double) root_y / expo->scale;
  target_workspace = workspace_expo_workspace_at_point (expo,
                                                        expo->pointer_x,
                                                        expo->pointer_y);

  if (expo->dragging && expo->pressed_window != NULL)
    {
      if (target_workspace >= 0 &&
          target_workspace != expo->pressed_window->workspace_index)
        {
          action->type = META_WORKSPACE_EXPO_ACTION_MOVE_WINDOW;
          action->xwindow = expo->pressed_window->xwindow;
          action->workspace_index = target_workspace;
        }
    }
  else if (expo->pressed_window != NULL)
    {
      action->type = META_WORKSPACE_EXPO_ACTION_ACTIVATE_WINDOW;
      action->xwindow = expo->pressed_window->xwindow;
      action->workspace_index = expo->pressed_workspace;
    }
  else if (expo->pressed_workspace >= 0)
    {
      action->type = META_WORKSPACE_EXPO_ACTION_ACTIVATE_WORKSPACE;
      action->workspace_index = expo->pressed_workspace;
    }

  if (expo->dragging)
    workspace_expo_queue_redraw (expo);

  expo->button_down = FALSE;
  expo->dragging = FALSE;
  expo->pressed_window = NULL;
  expo->pressed_workspace = -1;
  expo->drop_workspace = -1;

  return action->type != META_WORKSPACE_EXPO_ACTION_NONE;
}

void
meta_workspace_expo_select_workspace (MetaWorkspaceExpo *expo,
                                      int                workspace_index)
{
  int old_workspace;

  if (expo == NULL ||
      workspace_index < 0 || workspace_index >= expo->n_workspaces)
    return;

  old_workspace = expo->selected_workspace;
  if (old_workspace == workspace_index)
    return;

  expo->selected_workspace = workspace_index;
  workspace_expo_queue_redraw (expo);
}

int
meta_workspace_expo_get_selected_workspace (MetaWorkspaceExpo *expo)
{
  return expo != NULL ? expo->selected_workspace : -1;
}

void
meta_workspace_expo_cancel_drag (MetaWorkspaceExpo *expo)
{
  if (expo == NULL)
    return;

  if (expo->dragging)
    workspace_expo_queue_redraw (expo);

  expo->button_down = FALSE;
  expo->dragging = FALSE;
  expo->pressed_window = NULL;
  expo->pressed_workspace = -1;
  expo->drop_workspace = -1;
}

void
meta_workspace_expo_remove_window (MetaWorkspaceExpo *expo,
                                   Window             xwindow)
{
  GList *link;

  if (expo == NULL)
    return;

  link = expo->windows;
  while (link != NULL)
    {
      GList *next = link->next;
      MetaWorkspaceExpoWindow *entry = link->data;

      if (entry->xwindow == xwindow)
        {
          if (entry == expo->pressed_window)
            meta_workspace_expo_cancel_drag (expo);
          expo->windows = g_list_delete_link (expo->windows, link);
          workspace_expo_window_free (entry);
          workspace_expo_queue_redraw (expo);
          return;
        }
      link = next;
    }
}

void
meta_workspace_expo_refresh_windows (MetaWorkspaceExpo *expo)
{
  GList *link;

  if (expo == NULL)
    return;

  link = expo->windows;
  while (link != NULL)
    {
      GList *next = link->next;
      MetaWorkspaceExpoWindow *entry = link->data;
      MetaWindow *window;

      window = meta_display_lookup_x_window (expo->screen->display,
                                             entry->xwindow);
      if (window == NULL || window->screen != expo->screen ||
          !window_is_eligible (window))
        {
          if (entry == expo->pressed_window)
            meta_workspace_expo_cancel_drag (expo);
          expo->windows = g_list_delete_link (expo->windows, link);
          workspace_expo_window_free (entry);
        }
      else
        {
          entry->workspace_index = window->workspace != NULL ?
            meta_workspace_index (window->workspace) : -1;
          entry->sticky = window->on_all_workspaces || window->always_sticky;
          meta_window_get_outer_rect (window, &entry->rect);
        }

      link = next;
    }

  workspace_expo_queue_redraw (expo);
}
