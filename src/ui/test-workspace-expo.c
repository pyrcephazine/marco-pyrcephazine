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

#include <glib.h>

#include "workspace-expo.h"

static gboolean
rectangles_overlap (const GdkRectangle *a,
                    const GdkRectangle *b)
{
  return a->x < b->x + b->width &&
         b->x < a->x + a->width &&
         a->y < b->y + b->height &&
         b->y < a->y + a->height;
}

static void
test_layout_four_workspaces (void)
{
  MetaWorkspaceExpoLayout layout;
  GdkRectangle rects[4];
  int grid[] = { 0, 1, 2, 3 };
  int i;
  int j;

  g_assert_true (meta_workspace_expo_calculate_layout (1280, 800,
                                                       1920, 1080,
                                                       2, 2, &layout));
  g_assert_cmpfloat_with_epsilon (layout.card_width / layout.card_height,
                                  1920.0 / 1080.0, 0.001);

  for (i = 0; i < 4; i++)
    {
      meta_workspace_expo_layout_cell_rect (&layout, i, &rects[i]);
      g_assert_cmpint (rects[i].x, >=, 0);
      g_assert_cmpint (rects[i].y, >=, 0);
      g_assert_cmpint (rects[i].x + rects[i].width, <=, 1280);
      g_assert_cmpint (rects[i].y + rects[i].height, <=, 800);

      g_assert_cmpint (
        meta_workspace_expo_layout_workspace_at (
          &layout, grid, G_N_ELEMENTS (grid),
          rects[i].x + rects[i].width / 2.0,
          rects[i].y + rects[i].height / 2.0),
        ==, i);

      for (j = 0; j < i; j++)
        g_assert_false (rectangles_overlap (&rects[i], &rects[j]));
    }
}

static void
test_layout_many_workspaces (void)
{
  MetaWorkspaceExpoLayout layout;
  GdkRectangle previous;
  GdkRectangle current;
  int i;

  g_assert_true (meta_workspace_expo_calculate_layout (1024, 768,
                                                       1024, 768,
                                                       6, 6, &layout));
  g_assert_cmpfloat (layout.card_width, >, 1.0);
  g_assert_cmpfloat (layout.card_height, >, 1.0);

  for (i = 0; i < 36; i++)
    {
      meta_workspace_expo_layout_cell_rect (&layout, i, &current);
      g_assert_cmpint (current.x, >=, 0);
      g_assert_cmpint (current.y, >=, 0);
      g_assert_cmpint (current.x + current.width, <=, 1024);
      g_assert_cmpint (current.y + current.height, <=, 768);

      if (i > 0)
        g_assert_false (rectangles_overlap (&current, &previous));
      previous = current;
    }
}

static void
test_layout_screen_aspects (void)
{
  MetaWorkspaceExpoLayout portrait;
  MetaWorkspaceExpoLayout wide;
  GdkRectangle last;

  g_assert_true (meta_workspace_expo_calculate_layout (800, 1280,
                                                       800, 1280,
                                                       2, 2, &portrait));
  g_assert_cmpfloat_with_epsilon (portrait.card_width /
                                  portrait.card_height,
                                  800.0 / 1280.0, 0.001);
  meta_workspace_expo_layout_cell_rect (&portrait, 3, &last);
  g_assert_cmpint (last.x + last.width, <=, 800);
  g_assert_cmpint (last.y + last.height, <=, 1280);

  g_assert_true (meta_workspace_expo_calculate_layout (1920, 1080,
                                                       3840, 1080,
                                                       2, 2, &wide));
  g_assert_cmpfloat_with_epsilon (wide.card_width / wide.card_height,
                                  3840.0 / 1080.0, 0.001);
  meta_workspace_expo_layout_cell_rect (&wide, 3, &last);
  g_assert_cmpint (last.x + last.width, <=, 1920);
  g_assert_cmpint (last.y + last.height, <=, 1080);
}

static void
test_layout_blanks_are_not_targets (void)
{
  MetaWorkspaceExpoLayout layout;
  GdkRectangle blank;
  GdkRectangle workspace;
  int grid[] = { 0, 1, 2, -1 };

  g_assert_true (meta_workspace_expo_calculate_layout (900, 700,
                                                       1600, 900,
                                                       2, 2, &layout));
  meta_workspace_expo_layout_cell_rect (&layout, 3, &blank);
  g_assert_cmpint (
    meta_workspace_expo_layout_workspace_at (
      &layout, grid, G_N_ELEMENTS (grid),
      blank.x + blank.width / 2.0,
      blank.y + blank.height / 2.0),
    ==, -1);

  meta_workspace_expo_layout_cell_rect (&layout, 2, &workspace);
  g_assert_cmpint (
    meta_workspace_expo_layout_workspace_at (
      &layout, grid, G_N_ELEMENTS (grid),
      workspace.x + workspace.width / 2.0,
      workspace.y + workspace.height / 2.0),
    ==, 2);
}

static void
test_window_transform (void)
{
  MetaRectangle window = { 960, 540, 480, 270 };
  GdkRectangle card = { 100, 50, 640, 360 };
  GdkRectangle result;

  meta_workspace_expo_transform_window_rect (1920, 1080,
                                             &window, &card, &result);

  g_assert_cmpint (result.x, ==, 420);
  g_assert_cmpint (result.y, ==, 230);
  g_assert_cmpint (result.width, ==, 160);
  g_assert_cmpint (result.height, ==, 90);
}

static void
test_invalid_layout (void)
{
  MetaWorkspaceExpoLayout layout;

  g_assert_false (meta_workspace_expo_calculate_layout (0, 600,
                                                        1920, 1080,
                                                        2, 2, &layout));
  g_assert_false (meta_workspace_expo_calculate_layout (800, 600,
                                                        1920, 1080,
                                                        0, 2, &layout));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/workspace-expo/layout/four",
                   test_layout_four_workspaces);
  g_test_add_func ("/workspace-expo/layout/many",
                   test_layout_many_workspaces);
  g_test_add_func ("/workspace-expo/layout/screen-aspects",
                   test_layout_screen_aspects);
  g_test_add_func ("/workspace-expo/layout/blanks",
                   test_layout_blanks_are_not_targets);
  g_test_add_func ("/workspace-expo/layout/window-transform",
                   test_window_transform);
  g_test_add_func ("/workspace-expo/layout/invalid",
                   test_invalid_layout);

  return g_test_run ();
}
