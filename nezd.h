/*
 * (C)opyright 2007-2009 Robert Manea <rob dot manea at gmail dot com>
 * See LICENSE file for license details.
 *
 */

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xinerama.h>

#define FONT "-*-fixed-*-*-*-*-*-*-*-*-*-*-*-*"
#define BGCOLOR "#111111"
#define FGCOLOR "grey70"
#define ESC_CHAR '^'

#define ALIGNCENTER 0
#define ALIGNLEFT 1
#define ALIGNRIGHT 2

#define TOPWINDOW 0
#define SLAVEWINDOW 1

#define MIN_BUF_SIZE 1024
#define MAX_LINE_LEN 8192

#define MAX_CLICKABLE_AREAS 256

#ifndef Button6
#define Button6 6
#endif

#ifndef Button7
#define Button7 7
#endif

#define RELATIVE_X 0x1
#define RELATIVE_Y 0x2
#define RELATIVE_WIDTH 0x4
#define RELATIVE_HEIGHT 0x8
#define RELATIVE_TITLE_WIDTH 0x10

enum { ColFG, ColBG, ColLast };

/* exapansion directions */
enum { noexpand, left, right, both };

typedef struct NEZD Nezd;
typedef struct Geometry Geometry;
typedef struct Fnt Fnt;
typedef struct TW TWIN;
typedef struct SW SWIN;
typedef struct _Sline Sline;

struct Geometry {
  short x, y;
  unsigned short title_width, width, height;
  unsigned char relative_flags;
};

struct Fnt {
  XFontStruct *xfont;
  XFontSet set;
  int ascent;
  int descent;
  int height;
  XftFont *xftfont;
  XGlyphInfo *extents;
  int width;
};

/* clickable areas */
typedef struct _CLICK_A {
  int active;
  int button;
  int start_x;
  int end_x;
  int start_y;
  int end_y;
  Window win; //(line)window to which the action is attached
  char cmd[1024];
} click_a;

typedef struct _SENS_PER_WINDOW {
  click_a sens_areas[MAX_CLICKABLE_AREAS];
  int sens_areas_cnt;
} sens_w;

// 0: top window, 1: slave window
extern int xorig[2];
extern sens_w window_sens[2];

/* title window */
struct TW {
  int x, y, width, height;

  char *name;
  Window win;
  Drawable drawable;
  char alignment;
  int expand;
  int x_right_corner;
  Bool ishidden;
};

/* slave window */
struct SW {
  int x, y, width, height;

  char *name;
  Window win;
  Window *line;
  Drawable *drawable;

  /* input buffer */
  char **tbuf;
  int tsize;
  int tcnt;
  /* line fg colors */
  unsigned long *tcol;

  int max_lines;
  int first_line_vis;
  int last_line_vis;
  int sel_line;

  char alignment;
  Bool ismenu;
  Bool ishmenu;
  Bool issticky;
  Bool ismapped;
};

struct NEZD {
  int x, y, w, h;
  Bool running;
  unsigned long norm[ColLast];

  TWIN title_win;
  SWIN slave_win;

  /* sensitive areas */
  Window sa_win;

  const char *fnt;
  const char *bg;
  const char *fg;
  int line_height;

  Display *dpy;
  int screen;
  unsigned int depth;

  Visual *visual;
  GC gc, rgc, tgc;
  Fnt font;
  Fnt fnpl[64];

  Bool ispersistent;
  Bool colorize;
  unsigned long timeout;
  long cur_line;
  int ret_val;

  int xinescreen;
};

extern Nezd nezd;

void free_buffer(void);
void x_draw_body(void);

/* draw.c */
extern void drawtext(const char *text, int reverse, int line, int align);
extern char *parse_line(const char *text, int linenr, int align, int reverse,
                        int nodraw);
extern long getcolor(const char *colstr);    /* returns color of colstr */
extern void setfont(const char *fontstr);    /* sets global font */
extern unsigned int textw(const char *text); /* returns width of text in px */
extern void drawheader(const char *text);
extern void drawbody(char *text);

/* util.c */
extern void *emalloc(unsigned int size); /* allocates memory, exits on error */
extern void eprint(const char *errstr,
                   ...); /* prints errstr and exits with 1 */
extern char *
estrdup(const char *str); /* duplicates str, exits on allocation error */
extern void spawn(const char *arg); /* execute arg */
