/****************************************************************************
 * apps/examples/oledtext/oledtext_main.c
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

/* The "oled" command: text on a 1 bpp framebuffer (an SSD1306 / SSD1309
 * OLED as /dev/fb0 through the LCD framebuffer).  The display is a grid
 * of text lines 8 pixel rows high (8 on a 64-row panel), 6 pixels a
 * character (21 on 128 columns); -b doubles both (4 lines of 10).  The
 * framebuffer keeps what is drawn, so a line can be replaced on its own.
 *
 *   oled text ...        clear, then a line per argument from the top
 *                        (a long one wraps onto the next lines)
 *   oled -l N text ...   line N (0 = the top) replaced by the text; the
 *                        others kept
 *   oled -b ...          double size (with either form)
 *   oled -c              clear
 *
 * Only the rows changed are sent (FBIO_UPDATE): over I2C at 100 kHz a full
 * screen is about a second.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <nuttx/video/fb.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_EXAMPLES_OLEDTEXT_DEVPATH
#  define CONFIG_EXAMPLES_OLEDTEXT_DEVPATH "/dev/fb0"
#endif

#define GLYPH_W    5     /* the font's columns */
#define CELL_W     6     /* a character's columns, the gap included */
#define CELL_H     8     /* a line's rows */

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* 5x7 ASCII 0x20-0x7e, a byte a column, bit 0 the top row (bit 7 the
 * descender row of g, j, p, q, y and the comma)
 */

static const uint8_t g_font[95][GLYPH_W] =
{
  {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5f, 0x00, 0x00},
  {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7f, 0x14, 0x7f, 0x14},
  {0x24, 0x2a, 0x7f, 0x2a, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
  {0x36, 0x49, 0x56, 0x20, 0x50}, {0x00, 0x08, 0x07, 0x03, 0x00},
  {0x00, 0x1c, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1c, 0x00},
  {0x2a, 0x1c, 0x7f, 0x1c, 0x2a}, {0x08, 0x08, 0x3e, 0x08, 0x08},
  {0x00, 0x80, 0x70, 0x30, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
  {0x00, 0x00, 0x60, 0x60, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
  {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
  {0x72, 0x49, 0x49, 0x49, 0x46}, {0x21, 0x41, 0x49, 0x4d, 0x33},
  {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
  {0x3c, 0x4a, 0x49, 0x49, 0x31}, {0x41, 0x21, 0x11, 0x09, 0x07},
  {0x36, 0x49, 0x49, 0x49, 0x36}, {0x46, 0x49, 0x49, 0x29, 0x1e},
  {0x00, 0x00, 0x14, 0x00, 0x00}, {0x00, 0x40, 0x34, 0x00, 0x00},
  {0x00, 0x08, 0x14, 0x22, 0x41}, {0x14, 0x14, 0x14, 0x14, 0x14},
  {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x59, 0x09, 0x06},
  {0x3e, 0x41, 0x5d, 0x59, 0x4e}, {0x7c, 0x12, 0x11, 0x12, 0x7c},
  {0x7f, 0x49, 0x49, 0x49, 0x36}, {0x3e, 0x41, 0x41, 0x41, 0x22},
  {0x7f, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x49, 0x49, 0x49, 0x41},
  {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x41, 0x51, 0x73},
  {0x7f, 0x08, 0x08, 0x08, 0x7f}, {0x00, 0x41, 0x7f, 0x41, 0x00},
  {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
  {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x1c, 0x02, 0x7f},
  {0x7f, 0x04, 0x08, 0x10, 0x7f}, {0x3e, 0x41, 0x41, 0x41, 0x3e},
  {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
  {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x26, 0x49, 0x49, 0x49, 0x32},
  {0x03, 0x01, 0x7f, 0x01, 0x03}, {0x3f, 0x40, 0x40, 0x40, 0x3f},
  {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
  {0x63, 0x14, 0x08, 0x14, 0x63}, {0x03, 0x04, 0x78, 0x04, 0x03},
  {0x61, 0x59, 0x49, 0x4d, 0x43}, {0x00, 0x7f, 0x41, 0x41, 0x41},
  {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x41, 0x7f},
  {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
  {0x00, 0x03, 0x07, 0x08, 0x00}, {0x20, 0x54, 0x54, 0x78, 0x40},
  {0x7f, 0x28, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x28},
  {0x38, 0x44, 0x44, 0x28, 0x7f}, {0x38, 0x54, 0x54, 0x54, 0x18},
  {0x00, 0x08, 0x7e, 0x09, 0x02}, {0x18, 0xa4, 0xa4, 0x9c, 0x78},
  {0x7f, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7d, 0x40, 0x00},
  {0x20, 0x40, 0x40, 0x3d, 0x00}, {0x7f, 0x10, 0x28, 0x44, 0x00},
  {0x00, 0x41, 0x7f, 0x40, 0x00}, {0x7c, 0x04, 0x78, 0x04, 0x78},
  {0x7c, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
  {0xfc, 0x18, 0x24, 0x24, 0x18}, {0x18, 0x24, 0x24, 0x18, 0xfc},
  {0x7c, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x24},
  {0x04, 0x04, 0x3f, 0x44, 0x24}, {0x3c, 0x40, 0x40, 0x20, 0x7c},
  {0x1c, 0x20, 0x40, 0x20, 0x1c}, {0x3c, 0x40, 0x30, 0x40, 0x3c},
  {0x44, 0x28, 0x10, 0x28, 0x44}, {0x4c, 0x90, 0x90, 0x90, 0x7c},
  {0x44, 0x64, 0x54, 0x4c, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00},
  {0x00, 0x00, 0x77, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00},
  {0x02, 0x01, 0x02, 0x04, 0x02}
};

struct oled_s
{
  FAR uint8_t *fb;           /* the framebuffer, mmapped */
  uint16_t xres;
  uint16_t yres;
  uint16_t stride;           /* bytes a row */
  uint8_t scale;             /* 1, or 2 for -b */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* A pixel: 1 bpp, MSB first in a byte (LCD_PACKEDMSFIRST) */

static void oled_pixel(FAR struct oled_s *o, uint16_t x, uint16_t y,
                       bool on)
{
  FAR uint8_t *p;
  uint8_t bit;

  if (x >= o->xres || y >= o->yres)
    {
      return;
    }

  p   = &o->fb[(uint32_t)y * o->stride + (x >> 3)];
  bit = (uint8_t)(0x80 >> (x & 7));
  if (on)
    {
      *p |= bit;
    }
  else
    {
      *p &= (uint8_t)~bit;
    }
}

/* The rows of text line n blank */

static void oled_clearline(FAR struct oled_s *o, uint16_t n)
{
  uint16_t h = CELL_H * o->scale;
  uint16_t y0 = n * h;
  uint16_t y;

  for (y = y0; y < y0 + h && y < o->yres; y++)
    {
      memset(&o->fb[(uint32_t)y * o->stride], 0, o->stride);
    }
}

/* A character at column cx of text line n */

static void oled_char(FAR struct oled_s *o, uint16_t cx, uint16_t n,
                      char ch)
{
  FAR const uint8_t *g;
  uint16_t x0 = cx * CELL_W * o->scale;
  uint16_t y0 = n * CELL_H * o->scale;
  uint8_t col;
  uint8_t row;
  uint8_t sx;
  uint8_t sy;

  if (ch < 0x20 || ch > 0x7e)
    {
      ch = '?';
    }

  g = g_font[ch - 0x20];
  for (col = 0; col < GLYPH_W; col++)
    {
      for (row = 0; row < CELL_H; row++)
        {
          bool on = (g[col] >> row) & 1;

          for (sx = 0; sx < o->scale; sx++)
            {
              for (sy = 0; sy < o->scale; sy++)
                {
                  oled_pixel(o, x0 + col * o->scale + sx,
                             y0 + row * o->scale + sy, on);
                }
            }
        }
    }
}

/* A string from text line n on, wrapping; the lines it used, the last
 * one cleared to its end.  Returns the line after the last.
 */

static uint16_t oled_text(FAR struct oled_s *o, uint16_t n,
                          FAR const char *s)
{
  uint16_t cols  = o->xres / (CELL_W * o->scale);
  uint16_t lines = o->yres / (CELL_H * o->scale);
  uint16_t cx = 0;

  if (n >= lines)
    {
      return n;
    }

  oled_clearline(o, n);
  for (; *s != '\0'; s++)
    {
      if (cx == cols || *s == '\n')
        {
          cx = 0;
          if (++n >= lines)
            {
              return n;
            }

          oled_clearline(o, n);
          if (*s == '\n')
            {
              continue;
            }
        }

      oled_char(o, cx++, n, *s);
    }

  return n + 1;
}

/* Rows y0 .. y1 - 1 to the display */

static void oled_update(int fd, FAR struct oled_s *o, uint16_t y0,
                        uint16_t y1)
{
#ifdef CONFIG_FB_UPDATE
  struct fb_area_s area;

  if (y1 > o->yres)
    {
      y1 = o->yres;
    }

  if (y1 <= y0)
    {
      return;
    }

  area.x = 0;
  area.y = y0;
  area.w = o->xres;
  area.h = y1 - y0;
  if (ioctl(fd, FBIO_UPDATE, (unsigned long)((uintptr_t)&area)) < 0)
    {
      printf("oled: FBIO_UPDATE failed: %d\n", errno);
    }
#else
  UNUSED(fd);
  UNUSED(o);
  UNUSED(y0);
  UNUSED(y1);
#endif
}

static void oled_usage(FAR const char *prog)
{
  printf("usage: %s [-b] text ...      clear, a line per argument\n"
         "       %s [-b] -l N text ... replace line N (0 = the top)\n"
         "       %s -c                 clear\n"
         "  -b: double size\n", prog, prog, prog);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  struct oled_s o;
  bool clear = false;
  int line = -1;
  uint16_t first;
  uint16_t n;
  int fd;
  int i;

  o.scale = 1;
  for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
    {
      if (strcmp(argv[i], "-b") == 0)
        {
          o.scale = 2;
        }
      else if (strcmp(argv[i], "-c") == 0)
        {
          clear = true;
        }
      else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
        {
          line = atoi(argv[++i]);
        }
      else
        {
          oled_usage(argv[0]);
          return EXIT_FAILURE;
        }
    }

  if (!clear && i >= argc)
    {
      oled_usage(argv[0]);
      return EXIT_FAILURE;
    }

  fd = open(CONFIG_EXAMPLES_OLEDTEXT_DEVPATH, O_RDWR);
  if (fd < 0)
    {
      printf("oled: cannot open %s: %d\n",
             CONFIG_EXAMPLES_OLEDTEXT_DEVPATH, errno);
      return EXIT_FAILURE;
    }

  if (ioctl(fd, FBIOGET_VIDEOINFO, (unsigned long)((uintptr_t)&vinfo)) < 0 ||
      ioctl(fd, FBIOGET_PLANEINFO, (unsigned long)((uintptr_t)&pinfo)) < 0)
    {
      printf("oled: no video / plane info: %d\n", errno);
      close(fd);
      return EXIT_FAILURE;
    }

  if (pinfo.bpp != 1)
    {
      printf("oled: %d bits a pixel; only 1 is supported\n", pinfo.bpp);
      close(fd);
      return EXIT_FAILURE;
    }

  o.xres   = vinfo.xres;
  o.yres   = vinfo.yres;
  o.stride = pinfo.stride;
  o.fb     = (FAR uint8_t *)mmap(NULL, pinfo.fblen, PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_FILE, fd, 0);
  if (o.fb == MAP_FAILED)
    {
      printf("oled: mmap failed: %d\n", errno);
      close(fd);
      return EXIT_FAILURE;
    }

  if (clear || line < 0)
    {
      memset(o.fb, 0, pinfo.fblen);
    }

  if (line >= 0)
    {
      /* one line (and what it wraps onto) replaced, the rest kept */

      first = (uint16_t)line;
      n = first;
      for (; i < argc; i++)
        {
          n = oled_text(&o, n, argv[i]);
        }

      oled_update(fd, &o, first * CELL_H * o.scale, n * CELL_H * o.scale);
    }
  else
    {
      n = 0;
      for (; i < argc; i++)
        {
          n = oled_text(&o, n, argv[i]);
        }

      oled_update(fd, &o, 0, o.yres);
    }

  munmap(o.fb, pinfo.fblen);
  close(fd);
  return EXIT_SUCCESS;
}
