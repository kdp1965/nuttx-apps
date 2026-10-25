/****************************************************************************
 * apps/examples/lisasplash/splash_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* The "splash" command: a NuttX splash screen for the LISA chip on Tiny
 * Tapeout, drawn through NX on a 1 bpp display (the Arty board's SSD1309
 * 128x64 OLED).  The Tiny Tapeout logo (or Calvin, from the LISA tile's
 * art, in dark ink on a lit pane) on the left; a rule; "NuttX" in Sans 17x23 bold and "on LISA",
 * "Tiny", "Tapeout" in the X11 6x10 font on the right.
 *
 *   splash [tt|calvin] [seconds]
 *
 * The picture stays for the seconds (0, the default: until the task is
 * killed - run it as "splash &"); then the background is released.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/boardctl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

#include <nuttx/nx/nx.h>
#include <nuttx/nx/nxglib.h>
#include <nuttx/nx/nxfonts.h>

#include "splash_images.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_NX_BLOCKING
#  error "The splash screen depends on CONFIG_NX_BLOCKING"
#endif

#if CONFIG_NX_NPLANES > 1
#  error "One plane only"
#endif

#define LIT        1     /* the colours at 1 bpp */
#define DARK       0
#define PANEL_X    66    /* the text panel: from the rule to the edge */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct splash_s
{
  NXHANDLE hnx;
  NXWINDOW hbkgd;
  nxgl_coord_t xres;
  nxgl_coord_t yres;
  volatile bool havepos;
  volatile bool connected;
  sem_t eventsem;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void splash_redraw(NXWINDOW hwnd, FAR const struct nxgl_rect_s *rect,
                          bool more, FAR void *arg);
static void splash_position(NXWINDOW hwnd,
                            FAR const struct nxgl_size_s *size,
                            FAR const struct nxgl_point_s *pos,
                            FAR const struct nxgl_rect_s *bounds,
                            FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct splash_s g_splash;

static const struct nx_callback_s g_splashcb =
{
  splash_redraw,
  splash_position
#ifdef CONFIG_NX_XYINPUT
  , NULL
#endif
#ifdef CONFIG_NX_KBD
  , NULL
#endif
  , NULL
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void splash_redraw(NXWINDOW hwnd, FAR const struct nxgl_rect_s *rect,
                          bool more, FAR void *arg)
{
  UNUSED(hwnd);
  UNUSED(rect);
  UNUSED(more);
  UNUSED(arg);
}

static void splash_position(NXWINDOW hwnd,
                            FAR const struct nxgl_size_s *size,
                            FAR const struct nxgl_point_s *pos,
                            FAR const struct nxgl_rect_s *bounds,
                            FAR void *arg)
{
  UNUSED(size);
  UNUSED(pos);
  UNUSED(arg);

  if (!g_splash.havepos)
    {
      g_splash.hbkgd   = hwnd;
      g_splash.xres    = bounds->pt2.x + 1;
      g_splash.yres    = bounds->pt2.y + 1;
      g_splash.havepos = true;
      sem_post(&g_splash.eventsem);
    }
}

/* The server's events, on a thread of their own (as examples/nxhello) */

static FAR void *splash_listener(FAR void *arg)
{
  UNUSED(arg);

  for (; ; )
    {
      if (nx_eventhandler(g_splash.hnx) < 0)
        {
          printf("splash: lost the NX server: %d\n", errno);
          exit(EXIT_FAILURE);
        }

      if (!g_splash.connected)
        {
          g_splash.connected = true;
          sem_post(&g_splash.eventsem);
        }
    }

  return NULL;
}

/* A filled rectangle: x0, y0 to x1, y1 inclusive */

static void splash_fill(nxgl_coord_t x0, nxgl_coord_t y0,
                        nxgl_coord_t x1, nxgl_coord_t y1,
                        nxgl_mxpixel_t color)
{
  struct nxgl_rect_s rect;
  nxgl_mxpixel_t c[CONFIG_NX_NPLANES];

  rect.pt1.x = x0;
  rect.pt1.y = y0;
  rect.pt2.x = x1;
  rect.pt2.y = y1;
  c[0] = color;
  nx_fill(g_splash.hbkgd, &rect, c);
}

/* A 1 bpp image (rows MSB first) with its top left at x, y, its bits
 * inverted with invert (dark ink on a lit ground).  The rows are copied to
 * RAM first: the NX server task reads them.
 */

static void splash_image(nxgl_coord_t x, nxgl_coord_t y,
                         FAR const uint8_t *bits, nxgl_coord_t w,
                         nxgl_coord_t h, unsigned int stride, bool invert)
{
  struct nxgl_rect_s dest;
  struct nxgl_point_s origin;
  FAR const void *src[CONFIG_NX_NPLANES];
  FAR uint8_t *ram;
  size_t size = (size_t)stride * h;

  ram = (FAR uint8_t *)malloc(size);
  if (ram == NULL)
    {
      return;
    }

  memcpy(ram, bits, size);
  if (invert)
    {
      size_t i;

      for (i = 0; i < size; i++)
        {
          ram[i] = (uint8_t)~ram[i];
        }
    }

  dest.pt1.x = x;
  dest.pt1.y = y;
  dest.pt2.x = x + w - 1;
  dest.pt2.y = y + h - 1;
  origin.x   = x;
  origin.y   = y;
  src[0]     = ram;
  nx_bitmap(g_splash.hbkgd, &dest, src, &origin, stride);
  free(ram);
}

/* The width of a string in a font */

static nxgl_coord_t splash_textwidth(NXHANDLE hfont, FAR const char *s)
{
  FAR const struct nx_font_s *fontset = nxf_getfontset(hfont);
  FAR const struct nx_fontbitmap_s *fbm;
  nxgl_coord_t w = 0;

  for (; *s != '\0'; s++)
    {
      fbm = nxf_getbitmap(hfont, *s);
      w += fbm ? fbm->metric.width + fbm->metric.xoffset : fontset->spwidth;
    }

  return w;
}

/* A string with its top left at x, y: each glyph rendered to a buffer
 * and sent as a bitmap (as examples/nxhello does)
 */

static void splash_text(NXHANDLE hfont, nxgl_coord_t x, nxgl_coord_t y,
                        FAR const char *s)
{
  FAR const struct nx_font_s *fontset = nxf_getfontset(hfont);
  FAR const struct nx_fontbitmap_s *fbm;
  FAR const void *src[CONFIG_NX_NPLANES];
  struct nxgl_rect_s dest;
  struct nxgl_point_s pos;
  FAR uint8_t *glyph;
  unsigned int mxstride = (fontset->mxwidth + 7) >> 3;
  uint8_t fwidth;
  uint8_t fheight;
  uint8_t fstride;

  glyph = (FAR uint8_t *)malloc((size_t)fontset->mxheight * mxstride);
  if (glyph == NULL)
    {
      return;
    }

  pos.x = x;
  pos.y = y;
  for (; *s != '\0'; s++)
    {
      fbm = nxf_getbitmap(hfont, *s);
      if (fbm == NULL)
        {
          pos.x += fontset->spwidth;
          continue;
        }

      fwidth  = fbm->metric.width + fbm->metric.xoffset;
      fheight = fbm->metric.height + fbm->metric.yoffset;
      fstride = (fwidth + 7) >> 3;
      memset(glyph, 0, (size_t)fheight * fstride);
      nxf_convert_1bpp(glyph, fheight, fwidth, fstride, fbm, LIT);

      dest.pt1.x = pos.x;
      dest.pt1.y = pos.y;
      dest.pt2.x = pos.x + fwidth - 1;
      dest.pt2.y = pos.y + fheight - 1;
      src[0]     = glyph;
      nx_bitmap(g_splash.hbkgd, &dest, src, &pos, fstride);
      pos.x += fwidth;
    }

  free(glyph);
}

/* A string centred in the text panel */

static void splash_centre(NXHANDLE hfont, nxgl_coord_t y, FAR const char *s)
{
  nxgl_coord_t w = splash_textwidth(hfont, s);
  nxgl_coord_t panel = g_splash.xres - PANEL_X;

  splash_text(hfont, PANEL_X + (panel > w ? (panel - w) / 2 : 0), y, s);
}

static void splash_draw(bool calvin)
{
  NXHANDLE big = nxf_getfonthandle(FONTID_SANS17X23B);
  NXHANDLE small = nxf_getfonthandle(FONTID_X11_MISC_FIXED_6X10);
  nxgl_coord_t right = g_splash.xres - 1;
  nxgl_coord_t bottom = g_splash.yres - 1;

  splash_fill(0, 0, right, bottom, DARK);

  /* The picture, centred in the left 64 columns */

  if (calvin)
    {
      /* Calvin as he is drawn: dark ink on a lit pane */

      splash_fill(0, 0, 63, bottom, LIT);
      splash_image((64 - CALVIN_WIDTH) / 2, (g_splash.yres - CALVIN_HEIGHT) / 2,
                   g_calvin, CALVIN_WIDTH, CALVIN_HEIGHT, CALVIN_STRIDE, true);
    }
  else
    {
      splash_image((64 - TTLOGO_WIDTH) / 2, (g_splash.yres - TTLOGO_HEIGHT) / 2,
                   g_ttlogo, TTLOGO_WIDTH, TTLOGO_HEIGHT, TTLOGO_STRIDE,
                   false);
    }

  /* The rule between, and one under "NuttX" */

  splash_fill(PANEL_X - 2, 2, PANEL_X - 2, bottom - 2, LIT);
  splash_fill(PANEL_X + 3, 29, right - 3, 29, LIT);

  if (big != NULL)
    {
      splash_centre(big, 3, "NuttX");
    }

  if (small != NULL)
    {
      splash_centre(small, 32, "on LISA");
      splash_centre(small, 42, "Tiny");
      splash_centre(small, 52, "Tapeout");
    }
}

static int splash_connect(void)
{
  struct sched_param param;
  pthread_attr_t attr;
  pthread_t thread;
  nxgl_mxpixel_t color = DARK;

  if (boardctl(BOARDIOC_NX_START, 0) < 0)
    {
      printf("splash: the NX server did not start: %d\n", errno);
      return ERROR;
    }

  g_splash.hnx = nx_connect();
  if (g_splash.hnx == NULL)
    {
      printf("splash: nx_connect failed: %d\n", errno);
      return ERROR;
    }

  pthread_attr_init(&attr);
  param.sched_priority = CONFIG_EXAMPLES_LISASPLASH_PRIORITY;
  pthread_attr_setschedparam(&attr, &param);
  pthread_attr_setstacksize(&attr, CONFIG_EXAMPLES_LISASPLASH_STACKSIZE);
  if (pthread_create(&thread, &attr, splash_listener, NULL) != 0)
    {
      printf("splash: no listener thread\n");
      nx_disconnect(g_splash.hnx);
      return ERROR;
    }

  while (!g_splash.connected)
    {
      sem_wait(&g_splash.eventsem);
    }

  nx_setbgcolor(g_splash.hnx, &color);
  if (nx_requestbkgd(g_splash.hnx, &g_splashcb, NULL) < 0)
    {
      printf("splash: nx_requestbkgd failed: %d\n", errno);
      nx_disconnect(g_splash.hnx);
      return ERROR;
    }

  while (!g_splash.havepos)
    {
      sem_wait(&g_splash.eventsem);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  bool calvin = false;
  int seconds = 0;
  int i;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "calvin") == 0)
        {
          calvin = true;
        }
      else if (strcmp(argv[i], "tt") == 0)
        {
          calvin = false;
        }
      else
        {
          seconds = atoi(argv[i]);
        }
    }

  memset(&g_splash, 0, sizeof(g_splash));
  sem_init(&g_splash.eventsem, 0, 0);
  if (splash_connect() < 0)
    {
      return EXIT_FAILURE;
    }

  splash_draw(calvin);

  if (seconds > 0)
    {
      sleep(seconds);
      nx_releasebkgd(g_splash.hbkgd);
      nx_disconnect(g_splash.hnx);
    }
  else
    {
      for (; ; )
        {
          sleep(3600);
        }
    }

  return EXIT_SUCCESS;
}
