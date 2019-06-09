/*
 * (C)opyright 2007-2009 Robert Manea <rob dot manea at gmail dot com>
 * See LICENSE file for license details.
 *
 */

#include "action.h"
#include "nezd.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

Nezd nezd = {0};
static int last_cnt = 0;
typedef void sigfunc(int);

static void clean_up(void) {
  int i;

  free_event_list();
#ifndef NEZD_XFT
  if (nezd.font.set)
    XFreeFontSet(nezd.dpy, nezd.font.set);
  else
    XFreeFont(nezd.dpy, nezd.font.xfont);
#endif

  XFreePixmap(nezd.dpy, nezd.title_win.drawable);
  if (nezd.slave_win.max_lines) {
    for (i = 0; i < nezd.slave_win.max_lines; i++) {
      XFreePixmap(nezd.dpy, nezd.slave_win.drawable[i]);
      XDestroyWindow(nezd.dpy, nezd.slave_win.line[i]);
    }
    free(nezd.slave_win.line);
    XDestroyWindow(nezd.dpy, nezd.slave_win.win);
  }
  XFreeGC(nezd.dpy, nezd.gc);
  XFreeGC(nezd.dpy, nezd.rgc);
  XFreeGC(nezd.dpy, nezd.tgc);
  XDestroyWindow(nezd.dpy, nezd.title_win.win);
  XCloseDisplay(nezd.dpy);
}

static void catch_sigusr1(int s) {
  (void)s;
  do_action(sigusr1);
}

static void catch_sigusr2(int s) {
  (void)s;
  do_action(sigusr2);
}

static void catch_sigterm(int s) {
  (void)s;
  do_action(onexit);
}

static void catch_alrm(int s) {
  (void)s;
  do_action(onexit);
  clean_up();
  exit(0);
}

static sigfunc *setup_signal(int signr, sigfunc *shandler) {
  struct sigaction nh, oh;

  nh.sa_handler = shandler;
  sigemptyset(&nh.sa_mask);
  nh.sa_flags = 0;

  if (sigaction(signr, &nh, &oh) < 0)
    return SIG_ERR;

  return NULL;
}

char *rem = NULL;
static int chomp(char *inbuf, char *outbuf, int start, int len) {
  int i = 0;
  int off = start;

  if (rem) {
    strncpy(outbuf, rem, strlen(rem));
    i += strlen(rem);
    free(rem);
    rem = NULL;
  }
  while (off < len) {
    if (i >= MAX_LINE_LEN) {
      outbuf[MAX_LINE_LEN - 1] = '\0';
      return ++off;
    }
    if (inbuf[off] != '\n') {
      outbuf[i++] = inbuf[off++];
    } else if (inbuf[off] == '\n') {
      outbuf[i] = '\0';
      return ++off;
    }
  }

  outbuf[i] = '\0';
  rem = estrdup(outbuf);
  return 0;
}

void free_buffer(void) {
  int i;
  for (i = 0; i < nezd.slave_win.tcnt; i++) {
    free(nezd.slave_win.tbuf[i]);
    nezd.slave_win.tbuf[i] = NULL;
  }
  nezd.slave_win.tcnt = nezd.slave_win.last_line_vis = last_cnt = 0;
}

static int read_stdin(void) {
  char buf[MAX_LINE_LEN], retbuf[MAX_LINE_LEN];
  ssize_t n, n_off = 0;

  if (!(n = read(STDIN_FILENO, buf, sizeof buf))) {
    if (!nezd.ispersistent) {
      nezd.running = False;
      return -1;
    } else
      return -2;
  } else {
    while ((n_off = chomp(buf, retbuf, n_off, n))) {
      if (!nezd.slave_win.ishmenu && nezd.tsupdate &&
          nezd.slave_win.max_lines &&
          ((nezd.cur_line == 0) ||
           !(nezd.cur_line % (nezd.slave_win.max_lines + 1))))
        drawheader(retbuf);
      else if (!nezd.slave_win.ishmenu && !nezd.tsupdate &&
               ((nezd.cur_line == 0) || !nezd.slave_win.max_lines))
        drawheader(retbuf);
      else
        drawbody(retbuf);
      nezd.cur_line++;
    }
  }
  return 0;
}

static void x_hilight_line(int line) {
  drawtext(nezd.slave_win.tbuf[line + nezd.slave_win.first_line_vis], 1, line,
           nezd.slave_win.alignment);
  XCopyArea(nezd.dpy, nezd.slave_win.drawable[line], nezd.slave_win.line[line],
            nezd.gc, 0, 0, nezd.slave_win.width, nezd.line_height, 0, 0);
}

static void x_unhilight_line(int line) {
  drawtext(nezd.slave_win.tbuf[line + nezd.slave_win.first_line_vis], 0, line,
           nezd.slave_win.alignment);
  XCopyArea(nezd.dpy, nezd.slave_win.drawable[line], nezd.slave_win.line[line],
            nezd.rgc, 0, 0, nezd.slave_win.width, nezd.line_height, 0, 0);
}

void x_draw_body(void) {
  int i;
  nezd.x = 0;
  nezd.y = 0;
  nezd.w = nezd.slave_win.width;
  nezd.h = nezd.line_height;

  window_sens[SLAVEWINDOW].sens_areas_cnt = 0;

  if (!nezd.slave_win.last_line_vis) {
    if (nezd.slave_win.tcnt < nezd.slave_win.max_lines) {
      nezd.slave_win.first_line_vis = 0;
      nezd.slave_win.last_line_vis = nezd.slave_win.tcnt;
    } else {
      nezd.slave_win.first_line_vis =
          nezd.slave_win.tcnt - nezd.slave_win.max_lines;
      nezd.slave_win.last_line_vis = nezd.slave_win.tcnt;
    }
  }

  for (i = 0; i < nezd.slave_win.max_lines; i++) {
    if (i < nezd.slave_win.last_line_vis)
      drawtext(nezd.slave_win.tbuf[i + nezd.slave_win.first_line_vis], 0, i,
               nezd.slave_win.alignment);
  }
  for (i = 0; i < nezd.slave_win.max_lines; i++)
    XCopyArea(nezd.dpy, nezd.slave_win.drawable[i], nezd.slave_win.line[i],
              nezd.gc, 0, 0, nezd.slave_win.width, nezd.line_height, 0, 0);
}

static void x_check_geometry(Geometry *geometry, XRectangle scr) {
  TWIN *t = &nezd.title_win;
  SWIN *s = &nezd.slave_win;

  if (geometry->relative_flags & RELATIVE_X)
    t->x = s->x = geometry->x * scr.width / 100;
  else
    t->x = s->x = geometry->x;

  if (geometry->relative_flags & RELATIVE_Y)
    t->y = geometry->y * scr.height / 100;
  else
    t->y = geometry->y;

  if (geometry->relative_flags & RELATIVE_WIDTH)
    s->width = geometry->y * scr.height / 100;
  else
    s->width = geometry->y;

  if (geometry->relative_flags & RELATIVE_HEIGHT)
    nezd.line_height = geometry->height * scr.height / 100;
  else
    nezd.line_height = geometry->height;

  if (geometry->relative_flags & RELATIVE_TITLE_WIDTH)
    t->width = geometry->title_width * scr.width / 100;
  else
    t->width = geometry->title_width;

  t->x = t->x < 0 ? scr.width + t->x + scr.x : t->x + scr.x;
  t->y = t->y < 0 ? scr.height + t->y + scr.y : t->y + scr.y;

  if (t->x < scr.x || scr.x + scr.width < t->x)
    t->x = scr.x;

  if (!t->width)
    t->width = scr.width;

  if ((t->x + t->width) > (scr.x + scr.width) && (t->expand != left))
    t->width = scr.width - (t->x - scr.x);

  if (t->expand == left) {
    t->x_right_corner = t->x + t->width;
    t->x = t->width ? t->x_right_corner - t->width : scr.x;
  }

  if (!s->width) {
    s->x = scr.x;
    s->width = scr.width;
  }
  if (t->width == s->width)
    s->x = t->x;

  if (s->width != scr.width) {
    s->x = t->x + (t->width - s->width) / 2;
    if (s->x < scr.x)
      s->x = scr.x;
    if (s->x + s->width > scr.x + scr.width)
      s->x = scr.x + (scr.width - s->width);
  }

  if (!nezd.line_height)
    nezd.line_height = nezd.font.height + 2;

  if (t->y + nezd.line_height > scr.y + scr.height)
    t->y = scr.y + scr.height - nezd.line_height;
}

static void qsi_no_xinerama(Display *dpy, XRectangle *rect) {
  rect->x = 0;
  rect->y = 0;
  rect->width = DisplayWidth(dpy, DefaultScreen(dpy));
  rect->height = DisplayHeight(dpy, DefaultScreen(dpy));
}

#ifdef NEZD_XINERAMA
static void queryscreeninfo(Display *dpy, XRectangle *rect, int screen) {
  XineramaScreenInfo *xsi = NULL;
  int nscreens = 1;

  if (XineramaIsActive(dpy))
    xsi = XineramaQueryScreens(dpy, &nscreens);

  if (xsi == NULL || screen > nscreens || screen <= 0) {
    qsi_no_xinerama(dpy, rect);
  } else {
    rect->x = xsi[screen - 1].x_org;
    rect->y = xsi[screen - 1].y_org;
    rect->width = xsi[screen - 1].width;
    rect->height = xsi[screen - 1].height;
  }
}
#endif

static void set_docking_ewmh_info(Display *dpy, Window w, int dock) {
  unsigned long strut[12] = {0};
  XWindowAttributes wa;
  Atom type;
  unsigned int desktop;
  pid_t cur_pid;
  char *host_name;
  XTextProperty txt_prop;
  XRectangle si;
#ifdef NEZD_XINERAMA
  XineramaScreenInfo *xsi;
  int screen_count, i, max_height;
#endif

  host_name = emalloc(HOST_NAME_MAX);
  if ((gethostname(host_name, HOST_NAME_MAX) > -1) && (cur_pid = getpid())) {

    XStringListToTextProperty(&host_name, 1, &txt_prop);
    XSetWMClientMachine(dpy, w, &txt_prop);
    XFree(txt_prop.value);

    XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_PID", False),
                    XInternAtom(dpy, "CARDINAL", False), 32, PropModeReplace,
                    (unsigned char *)&cur_pid, 1);
  }
  free(host_name);

  XGetWindowAttributes(dpy, w, &wa);
#ifdef NEZD_XINERAMA
  queryscreeninfo(dpy, &si, nezd.xinescreen);
#else
  qsi_no_xinerama(dpy, &si);
#endif
  if (wa.y - si.y == 0) {
    strut[2] = si.y + wa.height;
    strut[8] = wa.x;
    strut[9] = wa.x + wa.width - 1;

  } else if ((wa.y - si.y + wa.height) == si.height) {
#ifdef NEZD_XINERAMA
    max_height = si.height;
    xsi = XineramaQueryScreens(dpy, &screen_count);
    for (i = 0; i < screen_count; i++) {
      if (xsi[i].height > max_height)
        max_height = xsi[i].height;
    }
    XFree(xsi);
    /* Adjust strut value if there is a larger screen */
    strut[3] = max_height - (si.height + si.y) + wa.height;
#else
    strut[3] = wa.height;
#endif
    strut[10] = wa.x;
    strut[11] = wa.x + wa.width - 1;

  }

  if (strut[2] != 0 || strut[3] != 0) {
    XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False),
                    XInternAtom(dpy, "CARDINAL", False), 32, PropModeReplace,
                    (unsigned char *)&strut, 12);
    XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_STRUT", False),
                    XInternAtom(dpy, "CARDINAL", False), 32, PropModeReplace,
                    (unsigned char *)&strut, 4);
  }

  if (dock) {
    type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False),
                    XInternAtom(dpy, "ATOM", False), 32, PropModeReplace,
                    (unsigned char *)&type, 1);

    /* some window managers honor this properties*/
    type = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_STATE", False),
                    XInternAtom(dpy, "ATOM", False), 32, PropModeReplace,
                    (unsigned char *)&type, 1);

    type = XInternAtom(dpy, "_NET_WM_STATE_STICKY", False);
    XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_STATE", False),
                    XInternAtom(dpy, "ATOM", False), 32, PropModeAppend,
                    (unsigned char *)&type, 1);

    desktop = 0xffffffff;
    XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_DESKTOP", False),
                    XInternAtom(dpy, "CARDINAL", False), 32, PropModeReplace,
                    (unsigned char *)&desktop, 1);
  }
}

static void x_create_gcs(void) {
  XGCValues gcv;
  gcv.graphics_exposures = 0;

  /* normal GC */
  nezd.gc = XCreateGC(nezd.dpy, RootWindow(nezd.dpy, nezd.screen),
                      GCGraphicsExposures, &gcv);
  XSetForeground(nezd.dpy, nezd.gc, nezd.norm[ColFG]);
  XSetBackground(nezd.dpy, nezd.gc, nezd.norm[ColBG]);
  /* reverse color GC */
  nezd.rgc = XCreateGC(nezd.dpy, RootWindow(nezd.dpy, nezd.screen),
                       GCGraphicsExposures, &gcv);
  XSetForeground(nezd.dpy, nezd.rgc, nezd.norm[ColBG]);
  XSetBackground(nezd.dpy, nezd.rgc, nezd.norm[ColFG]);
  /* temporary GC */
  nezd.tgc = XCreateGC(nezd.dpy, RootWindow(nezd.dpy, nezd.screen),
                       GCGraphicsExposures, &gcv);
}

static void x_connect(void) {
  nezd.dpy = XOpenDisplay(0);
  if (!nezd.dpy)
    eprint("nezd: cannot open display\n");
  nezd.screen = DefaultScreen(nezd.dpy);
}

/* Read display styles from X resources. */
static void x_read_resources(void) {
  XrmDatabase xdb;
  char *xrm;
  char *datatype[20];
  XrmValue xvalue;

  XrmInitialize();
  xrm = XResourceManagerString(nezd.dpy);
  if (xrm != NULL) {
    xdb = XrmGetStringDatabase(xrm);
    if (XrmGetResource(xdb, "nezd.font", "*", datatype, &xvalue) == True)
      nezd.fnt = estrdup(xvalue.addr);
    if (XrmGetResource(xdb, "nezd.foreground", "*", datatype, &xvalue) == True)
      nezd.fg = estrdup(xvalue.addr);
    if (XrmGetResource(xdb, "nezd.background", "*", datatype, &xvalue) == True)
      nezd.bg = estrdup(xvalue.addr);
    if (XrmGetResource(xdb, "nezd.titlename", "*", datatype, &xvalue) == True)
      nezd.title_win.name = estrdup(xvalue.addr);
    if (XrmGetResource(xdb, "nezd.slavename", "*", datatype, &xvalue) == True)
      nezd.slave_win.name = estrdup(xvalue.addr);
    XrmDestroyDatabase(xdb);
  }
}

static void x_create_windows(Geometry *geometry, int use_ewmh_dock) {
  XSetWindowAttributes wa;
  Window root;
  int i;
  XRectangle si;
  XClassHint *class_hint;

  root = RootWindow(nezd.dpy, nezd.screen);

  /* style */
  if ((nezd.norm[ColBG] = getcolor(nezd.bg)) == ~0lu)
    eprint("nezd: error, cannot allocate color '%s'\n", nezd.bg);
  if ((nezd.norm[ColFG] = getcolor(nezd.fg)) == ~0lu)
    eprint("nezd: error, cannot allocate color '%s'\n", nezd.fg);
  setfont(nezd.fnt);

  x_create_gcs();

  /* window attributes */
  wa.override_redirect = (use_ewmh_dock ? 0 : 1);
  wa.background_pixmap = ParentRelative;
  wa.event_mask = ExposureMask | ButtonReleaseMask | ButtonPressMask |
                  ButtonMotionMask | EnterWindowMask | LeaveWindowMask |
                  KeyPressMask;

#ifdef NEZD_XINERAMA
  queryscreeninfo(nezd.dpy, &si, nezd.xinescreen);
#else
  qsi_no_xinerama(nezd.dpy, &si);
#endif
  x_check_geometry(geometry, si);

  /* title window */
  nezd.title_win.win = XCreateWindow(
      nezd.dpy, root, nezd.title_win.x, nezd.title_win.y, nezd.title_win.width,
      nezd.line_height, 0, DefaultDepth(nezd.dpy, nezd.screen), CopyFromParent,
      DefaultVisual(nezd.dpy, nezd.screen),
      CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
  /* set class property */
  class_hint = XAllocClassHint();
  class_hint->res_name = "nezd";
  class_hint->res_class = "Nezd";
  XSetClassHint(nezd.dpy, nezd.title_win.win, class_hint);
  XFree(class_hint);

  /* title */
  XStoreName(nezd.dpy, nezd.title_win.win, nezd.title_win.name);

  nezd.title_win.drawable =
      XCreatePixmap(nezd.dpy, root, nezd.title_win.width, nezd.line_height,
                    DefaultDepth(nezd.dpy, nezd.screen));
  XFillRectangle(nezd.dpy, nezd.title_win.drawable, nezd.rgc, 0, 0,
                 nezd.title_win.width, nezd.line_height);

  /* set some hints for windowmanagers*/
  set_docking_ewmh_info(nezd.dpy, nezd.title_win.win, use_ewmh_dock);

  /* TODO: Smarter approach to window creation so we can reduce the
   *       size of this function.
   */

  if (nezd.slave_win.max_lines) {
    nezd.slave_win.first_line_vis = 0;
    nezd.slave_win.last_line_vis = 0;
    nezd.slave_win.line = emalloc(sizeof(Window) * nezd.slave_win.max_lines);
    nezd.slave_win.drawable =
        emalloc(sizeof(Drawable) * nezd.slave_win.max_lines);

    /* horizontal menu mode */
    if (nezd.slave_win.ishmenu) {
      /* calculate width of menuentries - this is a very simple
       * approach but works well for general cases.
       */
      int ew = nezd.slave_win.width / nezd.slave_win.max_lines;
      int r = nezd.slave_win.width - ew * nezd.slave_win.max_lines;
      nezd.slave_win.issticky = True;
      nezd.slave_win.y = nezd.title_win.y;

      nezd.slave_win.win =
          XCreateWindow(nezd.dpy, root, nezd.slave_win.x, nezd.slave_win.y,
                        nezd.slave_win.width, nezd.line_height, 0,
                        DefaultDepth(nezd.dpy, nezd.screen), CopyFromParent,
                        DefaultVisual(nezd.dpy, nezd.screen),
                        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
      XStoreName(nezd.dpy, nezd.slave_win.win, nezd.slave_win.name);

      for (i = 0; i < nezd.slave_win.max_lines; i++) {
        nezd.slave_win.drawable[i] =
            XCreatePixmap(nezd.dpy, root, ew + r, nezd.line_height,
                          DefaultDepth(nezd.dpy, nezd.screen));
        XFillRectangle(nezd.dpy, nezd.slave_win.drawable[i], nezd.rgc, 0, 0,
                       ew + r, nezd.line_height);
      }

      /* windows holding the lines */
      for (i = 0; i < nezd.slave_win.max_lines; i++)
        nezd.slave_win.line[i] = XCreateWindow(
            nezd.dpy, nezd.slave_win.win, i * ew, 0,
            (i == nezd.slave_win.max_lines - 1) ? ew + r : ew, nezd.line_height,
            0, DefaultDepth(nezd.dpy, nezd.screen), CopyFromParent,
            DefaultVisual(nezd.dpy, nezd.screen),
            CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

      /* As we don't use the title window in this mode,
       * we reuse its width value
       */
      nezd.title_win.width = nezd.slave_win.width;
      nezd.slave_win.width = ew + r;
    }

    /* vertical slave window */
    else {
      nezd.slave_win.issticky = False;
      nezd.slave_win.y = nezd.title_win.y + nezd.line_height;

      if (nezd.title_win.y + nezd.line_height * nezd.slave_win.max_lines >
          si.y + si.height)
        nezd.slave_win.y = (nezd.title_win.y - nezd.line_height) -
                           nezd.line_height * (nezd.slave_win.max_lines) +
                           nezd.line_height;

      nezd.slave_win.win = XCreateWindow(
          nezd.dpy, root, nezd.slave_win.x, nezd.slave_win.y,
          nezd.slave_win.width, nezd.slave_win.max_lines * nezd.line_height, 0,
          DefaultDepth(nezd.dpy, nezd.screen), CopyFromParent,
          DefaultVisual(nezd.dpy, nezd.screen),
          CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
      XStoreName(nezd.dpy, nezd.slave_win.win, nezd.slave_win.name);

      for (i = 0; i < nezd.slave_win.max_lines; i++) {
        nezd.slave_win.drawable[i] = XCreatePixmap(
            nezd.dpy, root, nezd.slave_win.width, nezd.line_height,
            DefaultDepth(nezd.dpy, nezd.screen));
        XFillRectangle(nezd.dpy, nezd.slave_win.drawable[i], nezd.rgc, 0, 0,
                       nezd.slave_win.width, nezd.line_height);
      }

      /* windows holding the lines */
      for (i = 0; i < nezd.slave_win.max_lines; i++)
        nezd.slave_win.line[i] =
            XCreateWindow(nezd.dpy, nezd.slave_win.win, 0, i * nezd.line_height,
                          nezd.slave_win.width, nezd.line_height, 0,
                          DefaultDepth(nezd.dpy, nezd.screen), CopyFromParent,
                          DefaultVisual(nezd.dpy, nezd.screen),
                          CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    }
  }
}

static void x_map_window(Window win) {
  XMapRaised(nezd.dpy, win);
  XSync(nezd.dpy, False);
}

static void x_redraw(Window w) {
  int i;

  if (!nezd.slave_win.ishmenu && w == nezd.title_win.win)
    drawheader(NULL);
  if (!nezd.tsupdate && w == nezd.slave_win.win) {
    for (i = 0; i < nezd.slave_win.max_lines; i++)
      XCopyArea(nezd.dpy, nezd.slave_win.drawable[i], nezd.slave_win.line[i],
                nezd.gc, 0, 0, nezd.slave_win.width, nezd.line_height, 0, 0);
  } else {
    for (i = 0; i < nezd.slave_win.max_lines; i++)
      if (w == nezd.slave_win.line[i]) {
        XCopyArea(nezd.dpy, nezd.slave_win.drawable[i], nezd.slave_win.line[i],
                  nezd.gc, 0, 0, nezd.slave_win.width, nezd.line_height, 0, 0);
      }
  }
}

static void handle_xev(void) {
  XEvent ev;
  int i, sa_clicked = 0;
  char buf[32];
  KeySym ksym;

  XNextEvent(nezd.dpy, &ev);
  switch (ev.type) {
  case Expose:
    if (ev.xexpose.count == 0)
      x_redraw(ev.xexpose.window);
    break;
  case EnterNotify:
    if (nezd.slave_win.ismenu) {
      for (i = 0; i < nezd.slave_win.max_lines; i++)
        if (ev.xcrossing.window == nezd.slave_win.line[i])
          x_hilight_line(i);
    }
    if (!nezd.slave_win.ishmenu && ev.xcrossing.window == nezd.title_win.win)
      do_action(entertitle);
    if (ev.xcrossing.window == nezd.slave_win.win)
      do_action(enterslave);
    break;
  case LeaveNotify:
    if (nezd.slave_win.ismenu) {
      for (i = 0; i < nezd.slave_win.max_lines; i++)
        if (ev.xcrossing.window == nezd.slave_win.line[i])
          x_unhilight_line(i);
    }
    if (!nezd.slave_win.ishmenu && ev.xcrossing.window == nezd.title_win.win)
      do_action(leavetitle);
    if (ev.xcrossing.window == nezd.slave_win.win) {
      do_action(leaveslave);
    }
    break;
  case ButtonRelease:
    if (nezd.slave_win.ismenu) {
      for (i = 0; i < nezd.slave_win.max_lines; i++)
        if (ev.xbutton.window == nezd.slave_win.line[i])
          nezd.slave_win.sel_line = i;
    }

    /* clickable areas */
    int w_id = ev.xbutton.window == nezd.title_win.win ? 0 : 1;
    sens_w w = window_sens[w_id];
    for (i = w.sens_areas_cnt; i >= 0; i--) {
      if (ev.xbutton.window == w.sens_areas[i].win &&
          ev.xbutton.button == w.sens_areas[i].button &&
          (ev.xbutton.x >= w.sens_areas[i].start_x + xorig[w_id] &&
           ev.xbutton.x <= w.sens_areas[i].end_x + xorig[w_id]) &&
          (ev.xbutton.y >= w.sens_areas[i].start_y &&
           ev.xbutton.y <= w.sens_areas[i].end_y) &&
          w.sens_areas[i].active) {
        spawn(w.sens_areas[i].cmd);
        sa_clicked++;
        break;
      }
    }
    if (!sa_clicked) {
      switch (ev.xbutton.button) {
      case Button1:
        do_action(button1);
        break;
      case Button2:
        do_action(button2);
        break;
      case Button3:
        do_action(button3);
        break;
      case Button4:
        do_action(button4);
        break;
      case Button5:
        do_action(button5);
        break;
      case Button6:
        do_action(button6);
        break;
      case Button7:
        do_action(button7);
        break;
      }
    }
    break;
  case KeyPress:
    XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);
    do_action(ksym + keymarker);
    break;

    /* TODO: XRandR rotation and size  */
  }
}

static void handle_newl(void) {
  XWindowAttributes wa;

  if (nezd.slave_win.max_lines && (nezd.slave_win.tcnt > last_cnt)) {
    do_action(onnewinput);

    if (XGetWindowAttributes(nezd.dpy, nezd.slave_win.win, &wa),
        wa.map_state != IsUnmapped
            /* autoscroll and redraw only if  we're
             * currently viewing the last line of input
             */
            && (nezd.slave_win.last_line_vis == last_cnt)) {
      nezd.slave_win.first_line_vis = 0;
      nezd.slave_win.last_line_vis = 0;
      x_draw_body();
    }
    /* needed for a_scrollhome */
    else if (wa.map_state != IsUnmapped &&
             nezd.slave_win.last_line_vis == nezd.slave_win.max_lines)
      x_draw_body();
    /* forget state if window was unmapped */
    else if (wa.map_state == IsUnmapped || !nezd.slave_win.last_line_vis) {
      nezd.slave_win.first_line_vis = 0;
      nezd.slave_win.last_line_vis = 0;
      x_draw_body();
    }
    last_cnt = nezd.slave_win.tcnt;
  }
}

static void event_loop(void) {
  int xfd, ret, dr = 0;
  fd_set rmask;

  xfd = ConnectionNumber(nezd.dpy);
  while (nezd.running) {
    FD_ZERO(&rmask);
    FD_SET(xfd, &rmask);
    if (dr != -2)
      FD_SET(STDIN_FILENO, &rmask);

    while (XPending(nezd.dpy))
      handle_xev();

    ret = select(xfd + 1, &rmask, NULL, NULL, NULL);
    if (ret) {
      if (dr != -2 && FD_ISSET(STDIN_FILENO, &rmask)) {
        if ((dr = read_stdin()) == -1)
          return;
        handle_newl();
      }
      if (dr == -2 && nezd.timeout > 0) {
        /* set an alarm to kill us after the timeout */
        struct itimerval t;
        memset(&t, 0, sizeof t);
        t.it_value.tv_sec = nezd.timeout;
        t.it_value.tv_usec = 0;
        setitimer(ITIMER_REAL, &t, NULL);
      }
      if (FD_ISSET(xfd, &rmask))
        handle_xev();
    }
  }
  return;
}

static void x_preload(const char *fontstr, int p) {
  char *def, **missing;
  int i, n;

  missing = NULL;

  nezd.fnpl[p].set = XCreateFontSet(nezd.dpy, fontstr, &missing, &n, &def);
  if (missing)
    XFreeStringList(missing);

  if (nezd.fnpl[p].set) {
    XFontStruct **xfonts;
    char **font_names;
    nezd.fnpl[p].ascent = nezd.fnpl[p].descent = 0;
    n = XFontsOfFontSet(nezd.fnpl[p].set, &xfonts, &font_names);
    for (i = 0, nezd.fnpl[p].ascent = 0, nezd.fnpl[p].descent = 0; i < n; i++) {
      if (nezd.fnpl[p].ascent < (*xfonts)->ascent)
        nezd.fnpl[p].ascent = (*xfonts)->ascent;
      if (nezd.fnpl[p].descent < (*xfonts)->descent)
        nezd.fnpl[p].descent = (*xfonts)->descent;
      xfonts++;
    }
  } else {
    if (nezd.fnpl[p].xfont)
      XFreeFont(nezd.dpy, nezd.fnpl[p].xfont);
    nezd.fnpl[p].xfont = NULL;
    if (!(nezd.fnpl[p].xfont = XLoadQueryFont(nezd.dpy, fontstr)))
      eprint("nezd: error, cannot load font: '%s'\n", fontstr);
    nezd.fnpl[p].ascent = nezd.fnpl[p].xfont->ascent;
    nezd.fnpl[p].descent = nezd.fnpl[p].xfont->descent;
  }
  nezd.fnpl[p].height = nezd.fnpl[p].ascent + nezd.fnpl[p].descent;
}

static void font_preload(char *s) {
  int k = 0;
  char *buf = strtok(s, ",");
  while (buf != NULL) {
    if (k < 64)
      x_preload(buf, k++);
    buf = strtok(NULL, ",");
  }
}

/* Get alignment from character 'l'eft, 'r'ight and 'c'enter */
static char alignment_from_char(char align) {
  switch (align) {
  case 'l':
    return ALIGNLEFT;
  case 'r':
    return ALIGNRIGHT;
  case 'c':
    return ALIGNCENTER;
  default:
    return ALIGNCENTER;
  }
}

static void init_input_buffer(void) {
  if (MIN_BUF_SIZE % nezd.slave_win.max_lines)
    nezd.slave_win.tsize =
        MIN_BUF_SIZE +
        (nezd.slave_win.max_lines - (MIN_BUF_SIZE % nezd.slave_win.max_lines));
  else
    nezd.slave_win.tsize = MIN_BUF_SIZE;

  nezd.slave_win.tbuf = emalloc(nezd.slave_win.tsize * sizeof(char *));
}

int main(int argc, char *argv[]) {
  int i, use_ewmh_dock = 0;
  char *action_string = NULL;
  char *endptr, *fnpre = NULL;
  Geometry geometry;

  /* default values */
  geometry.x = 0;
  geometry.y = 0;
  geometry.title_width = geometry.width = 0;
  geometry.height = 0;
  geometry.relative_flags = 0;

  nezd.title_win.name = "nezd title";
  nezd.slave_win.name = "nezd slave";
  nezd.cur_line = 0;
  nezd.ret_val = 0;
  nezd.title_win.alignment = ALIGNCENTER;
  nezd.slave_win.alignment = ALIGNLEFT;
  nezd.fnt = FONT;
  nezd.bg = BGCOLOR;
  nezd.fg = FGCOLOR;
  nezd.slave_win.max_lines = 0;
  nezd.running = True;
  nezd.xinescreen = 0;
  nezd.tsupdate = 0;
  nezd.line_height = 0;
  nezd.title_win.expand = noexpand;

  /* Connect to X server */
  x_connect();
  x_read_resources();

  /* cmdline args */
  for (i = 1; i < argc; i++)
    if (!strncmp(argv[i], "-l", 3)) {
      if (++i < argc) {
        nezd.slave_win.max_lines = atoi(argv[i]);
        if (nezd.slave_win.max_lines)
          init_input_buffer();
      }
    } else if (!strncmp(argv[i], "-geometry", 10)) {
      if (++i < argc) {
        int t;
        int tx, ty;
        unsigned int tw, th;

        t = XParseGeometry(argv[i], &tx, &ty, &tw, &th);

        if (t & XValue)
          geometry.x = tx;
        if (t & YValue) {
          geometry.y = ty;
          if (!ty && (t & YNegative))
            /* -0 != +0 */
            geometry.y = -1;
        }
        if (t & WidthValue)
          geometry.title_width = tw;
        if (t & HeightValue)
          geometry.height = th;
      }
    } else if (!strncmp(argv[i], "-u", 3)) {
      nezd.tsupdate = True;
    } else if (!strncmp(argv[i], "-expand", 8)) {
      if (++i < argc) {
        switch (argv[i][0]) {
        case 'l':
          nezd.title_win.expand = left;
          break;
        case 'c':
          nezd.title_win.expand = both;
          break;
        case 'r':
          nezd.title_win.expand = right;
          break;
        default:
          nezd.title_win.expand = noexpand;
        }
      }
    } else if (!strncmp(argv[i], "-p", 3)) {
      nezd.ispersistent = True;
      if (i + 1 < argc) {
        nezd.timeout = strtoul(argv[i + 1], &endptr, 10);
        if (*endptr)
          nezd.timeout = 0;
        else
          i++;
      }
    } else if (!strncmp(argv[i], "-ta", 4)) {
      if (++i < argc)
        nezd.title_win.alignment = alignment_from_char(argv[i][0]);
    } else if (!strncmp(argv[i], "-sa", 4)) {
      if (++i < argc)
        nezd.slave_win.alignment = alignment_from_char(argv[i][0]);
    } else if (!strncmp(argv[i], "-m", 3)) {
      nezd.slave_win.ismenu = True;
      if (i + 1 < argc) {
        if (argv[i + 1][0] == 'v') {
          ++i;
          break;
        }
        nezd.slave_win.ishmenu = (argv[i + 1][0] == 'h') ? ++i, True : False;
      }
    } else if (!strncmp(argv[i], "-fn", 4)) {
      if (++i < argc)
        nezd.fnt = argv[i];
    } else if (!strncmp(argv[i], "-e", 3)) {
      if (++i < argc)
        action_string = argv[i];
    } else if (!strncmp(argv[i], "-title-name", 12)) {
      if (++i < argc)
        nezd.title_win.name = argv[i];
    } else if (!strncmp(argv[i], "-slave-name", 12)) {
      if (++i < argc)
        nezd.slave_win.name = argv[i];
    } else if (!strncmp(argv[i], "-bg", 4)) {
      if (++i < argc)
        nezd.bg = argv[i];
    } else if (!strncmp(argv[i], "-fg", 4)) {
      if (++i < argc)
        nezd.fg = argv[i];
    } else if (!strncmp(argv[i], "-x", 3)) {
      if (++i < argc) {
        geometry.x = atoi(argv[i]);
        if (strchr(argv[i], '%'))
          geometry.relative_flags |= RELATIVE_X;
        else
          geometry.relative_flags &= ~RELATIVE_X;
      }
    } else if (!strncmp(argv[i], "-y", 3)) {
      if (++i < argc) {
        geometry.y = atoi(argv[i]);
        if (strchr(argv[i], '%'))
          geometry.relative_flags |= RELATIVE_Y;
        else
          geometry.relative_flags &= ~RELATIVE_Y;
      }
    } else if (!strncmp(argv[i], "-w", 3)) {
      if (++i < argc) {
        geometry.width = atoi(argv[i]);
        if (strchr(argv[i], '%'))
          geometry.relative_flags |= RELATIVE_WIDTH;
        else
          geometry.relative_flags &= ~RELATIVE_WIDTH;
      }
    } else if (!strncmp(argv[i], "-h", 3)) {
      if (++i < argc) {
        geometry.height = atoi(argv[i]);
        if (strchr(argv[i], '%'))
          geometry.relative_flags |= RELATIVE_HEIGHT;
        else
          geometry.relative_flags &= ~RELATIVE_HEIGHT;
      }
    } else if (!strncmp(argv[i], "-tw", 4)) {
      if (++i < argc) {
        geometry.title_width = atoi(argv[i]);
        if (strchr(argv[i], '%'))
          geometry.relative_flags |= RELATIVE_TITLE_WIDTH;
        else
          geometry.relative_flags &= ~RELATIVE_TITLE_WIDTH;
      }
    } else if (!strncmp(argv[i], "-fn-preload", 12)) {
      if (++i < argc) {
        fnpre = estrdup(argv[i]);
      }
    }
#ifdef NEZD_XINERAMA
    else if (!strncmp(argv[i], "-xs", 4)) {
      if (++i < argc)
        nezd.xinescreen = atoi(argv[i]);
    }
#endif
    else if (!strncmp(argv[i], "-dock", 6))
      use_ewmh_dock = 1;
    else if (!strncmp(argv[i], "-v", 3)) {
      printf("nezd-" VERSION ", (C)opyright 2007-2009 Robert Manea\n");
      printf("Enabled optional features: "
#ifdef NEZD_XPM
             "XPM "
#endif
#ifdef NEZD_XFT
             "XFT "
#endif
#ifdef NEZD_XINERAMA
             "XINERAMA "
#endif
             "\n");
      return EXIT_SUCCESS;
    } else
      eprint("usage: nezd [-v] [-p [seconds]] [-m [v|h]] [-ta <l|c|r>] [-sa "
             "<l|c|r>]\n"
             "             [-x <pixel|percent%>] [-y <pixel|percent%>] [-w "
             "<pixel|percent%>]\n"
             "             [-h <pixel|percent%>] [-tw <pixel|percent%>] [-u]\n"
             "             [-e <string>] [-l <lines>]  [-fn <font>] [-bg "
             "<color>] [-fg <color>]\n"
             "             [-geometry <geometry string>] [-expand "
             "<left|right>] [-dock]\n"
             "             [-title-name <string>] [-slave-name <string>]\n"
#ifdef NEZD_XINERAMA
             "             [-xs <screen>]\n"
#endif
      );

  if (nezd.tsupdate && !nezd.slave_win.max_lines)
    nezd.tsupdate = False;

  if (!geometry.title_width) {
    geometry.title_width = geometry.width;
    if (geometry.relative_flags & RELATIVE_WIDTH)
      geometry.relative_flags |= RELATIVE_TITLE_WIDTH;
  }

  if (!setlocale(LC_ALL, "") || !XSupportsLocale())
    puts("nezd: locale not available, expect problems with fonts.\n");

  if (action_string)
    fill_ev_table(action_string);
  else {
    if (!nezd.slave_win.max_lines) {
      char edef[] = "button3=exit:13";
      fill_ev_table(edef);
    } else if (nezd.slave_win.ishmenu) {
      char edef[] = "enterslave=grabkeys;leaveslave=ungrabkeys;"
                    "button4=scrollup;button5=scrolldown;"
                    "key_Left=scrollup;key_Right=scrolldown;"
                    "button1=menuexec;button3=exit:13;"
                    "key_Escape=ungrabkeys,exit";
      fill_ev_table(edef);
    } else {
      char edef[] = "entertitle=uncollapse,grabkeys;"
                    "enterslave=grabkeys;leaveslave=collapse,ungrabkeys;"
                    "button1=menuexec;button2=togglestick;button3=exit:13;"
                    "button4=scrollup;button5=scrolldown;"
                    "key_Up=scrollup;key_Down=scrolldown;"
                    "key_Escape=ungrabkeys,exit";
      fill_ev_table(edef);
    }
  }

  if ((find_event(onexit) != -1) &&
      (setup_signal(SIGTERM, catch_sigterm) == SIG_ERR))
    fprintf(stderr, "nezd: error hooking SIGTERM\n");

  if ((find_event(sigusr1) != -1) &&
      (setup_signal(SIGUSR1, catch_sigusr1) == SIG_ERR))
    fprintf(stderr, "nezd: error hooking SIGUSR1\n");

  if ((find_event(sigusr2) != -1) &&
      (setup_signal(SIGUSR2, catch_sigusr2) == SIG_ERR))
    fprintf(stderr, "nezd: error hooking SIGUSR2\n");

  if (setup_signal(SIGALRM, catch_alrm) == SIG_ERR)
    fprintf(stderr, "nezd: error hooking SIGALARM\n");

  if (nezd.slave_win.ishmenu && !nezd.slave_win.max_lines)
    nezd.slave_win.max_lines = 1;

  x_create_windows(&geometry, use_ewmh_dock);

  if (!nezd.slave_win.ishmenu)
    x_map_window(nezd.title_win.win);
  else {
    XMapRaised(nezd.dpy, nezd.slave_win.win);
    for (i = 0; i < nezd.slave_win.max_lines; i++)
      XMapWindow(nezd.dpy, nezd.slave_win.line[i]);
  }

  if (fnpre != NULL)
    font_preload(fnpre);

  do_action(onstart);

  /* main loop */
  event_loop();

  do_action(onexit);
  clean_up();

  if (nezd.ret_val)
    return nezd.ret_val;

  return EXIT_SUCCESS;
}
