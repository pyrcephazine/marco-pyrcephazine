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

#ifndef META_WORKSPACE_EXPO_H
#define META_WORKSPACE_EXPO_H

#include <glib.h>
#include <gdk/gdk.h>
#include <X11/Xlib.h>

#include "boxes.h"
#include "types.h"

typedef struct _MetaWorkspaceExpo MetaWorkspaceExpo;

typedef enum
{
  META_WORKSPACE_EXPO_ACTION_NONE,
  META_WORKSPACE_EXPO_ACTION_ACTIVATE_WORKSPACE,
  META_WORKSPACE_EXPO_ACTION_ACTIVATE_WINDOW,
  META_WORKSPACE_EXPO_ACTION_MOVE_WINDOW
} MetaWorkspaceExpoActionType;

typedef struct
{
  MetaWorkspaceExpoActionType type;
  Window xwindow;
  int workspace_index;
} MetaWorkspaceExpoAction;

typedef struct
{
  double margin;
  double header_height;
  double gap;
  double card_width;
  double card_height;
  double start_x;
  double start_y;
  int rows;
  int cols;
} MetaWorkspaceExpoLayout;

gboolean           meta_workspace_expo_calculate_layout        (int                  width,
                                                                 int                  height,
                                                                 int                  screen_width,
                                                                 int                  screen_height,
                                                                 int                  rows,
                                                                 int                  cols,
                                                                 MetaWorkspaceExpoLayout *layout);
void               meta_workspace_expo_layout_cell_rect        (const MetaWorkspaceExpoLayout *layout,
                                                                 int                  cell,
                                                                 GdkRectangle        *rect);
int                meta_workspace_expo_layout_workspace_at      (const MetaWorkspaceExpoLayout *layout,
                                                                 const int           *workspace_grid,
                                                                 int                  grid_area,
                                                                 double               x,
                                                                 double               y);
void               meta_workspace_expo_transform_window_rect   (int                  screen_width,
                                                                 int                  screen_height,
                                                                 const MetaRectangle *window_rect,
                                                                 const GdkRectangle  *card_rect,
                                                                 GdkRectangle        *result);

MetaWorkspaceExpo *meta_workspace_expo_new                   (MetaScreen          *screen);
void               meta_workspace_expo_free                  (MetaWorkspaceExpo   *expo);
void               meta_workspace_expo_show                  (MetaWorkspaceExpo   *expo);
void               meta_workspace_expo_raise                 (MetaWorkspaceExpo   *expo);

void               meta_workspace_expo_button_press          (MetaWorkspaceExpo   *expo,
                                                               int                  root_x,
                                                               int                  root_y,
                                                               unsigned int         button);
void               meta_workspace_expo_motion                (MetaWorkspaceExpo   *expo,
                                                               int                  root_x,
                                                               int                  root_y);
gboolean           meta_workspace_expo_button_release        (MetaWorkspaceExpo   *expo,
                                                               int                  root_x,
                                                               int                  root_y,
                                                               unsigned int         button,
                                                               MetaWorkspaceExpoAction *action);

void               meta_workspace_expo_select_workspace      (MetaWorkspaceExpo   *expo,
                                                               int                  workspace_index);
int                meta_workspace_expo_get_selected_workspace (MetaWorkspaceExpo  *expo);
void               meta_workspace_expo_cancel_drag           (MetaWorkspaceExpo   *expo);
void               meta_workspace_expo_remove_window         (MetaWorkspaceExpo   *expo,
                                                               Window               xwindow);
void               meta_workspace_expo_refresh_windows       (MetaWorkspaceExpo   *expo);

#endif
