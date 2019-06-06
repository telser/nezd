
/*
* (C)opyright 2007-2009 Robert Manea <rob dot manea at gmail dot com>
* See LICENSE file for license details.
*
*/

#include "nezd.h"
#include "action.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NEZD_XPM
#include <X11/xpm.h>
#endif

#define ARGLEN 256
#define MAX_ICON_CACHE 32

#define MAX(a,b) ((a)>(b)?(a):(b))
#define LNR2WINDOW(lnr) lnr==-1?0:1

typedef struct ICON_C {
	char name[ARGLEN];
	Pixmap p;

	int w, h;
} icon_c;

icon_c icons[MAX_ICON_CACHE];
int icon_cnt;
int otx;

int xorig[2];
sens_w window_sens[2];

/* command types for the in-text parser */
enum ctype  {bg, fg, icon, rect, recto, circle, circleo, pos, abspos, titlewin, ibg, fn, fixpos, ca, ba};

struct command_lookup {
	const char *name;
	int id;
	int off;
};

struct command_lookup cmd_lookup_table[] = {
	{ "fg(",        fg,			3},
	{ "bg(",        bg,			3},
	{ "i(",			icon,		2},
	{ "r(",	        rect,		2},
	{ "ro(",        recto,		3},
	{ "c(",	        circle,		2},
	{ "co(",        circleo,	3},
	{ "p(",	        pos,		2},
	{ "pa(",        abspos,		3},
	{ "tw(",        titlewin,	3},
	{ "ib(",        ibg,		3},
	{ "fn(",        fn,			3},
	{ "ca(",        ca,			3},
	{ "ba(",		ba,			3},
	{ 0,			0,			0}
};


/* positioning helpers */
enum sctype {LOCK_X, UNLOCK_X, TOP, BOTTOM, CENTER, LEFT, RIGHT};

int get_tokval(const char* line, char **retdata);
int get_token(const char*  line, int * t, char **tval);

static unsigned int
textnw(Fnt *font, const char *text, unsigned int len) {
#ifndef NEZD_XFT
	XRectangle r;

	if(font->set) {
		XmbTextExtents(font->set, text, len, NULL, &r);
		return r.width;
	}
	return XTextWidth(font->xfont, text, len);
#else
	XftTextExtentsUtf8(nezd.dpy, nezd.font.xftfont, (unsigned const char *) text, strlen(text), nezd.font.extents);
	if(nezd.font.extents->height > nezd.font.height)
		nezd.font.height = nezd.font.extents->height;
	return nezd.font.extents->xOff;
#endif
}


void
drawtext(const char *text, int reverse, int line, int align) {
	if(!reverse) {
		XSetForeground(nezd.dpy, nezd.gc, nezd.norm[ColBG]);
		XFillRectangle(nezd.dpy, nezd.slave_win.drawable[line], nezd.gc, 0, 0, nezd.w, nezd.h);
		XSetForeground(nezd.dpy, nezd.gc, nezd.norm[ColFG]);
	}
	else {
		XSetForeground(nezd.dpy, nezd.rgc, nezd.norm[ColFG]);
		XFillRectangle(nezd.dpy, nezd.slave_win.drawable[line], nezd.rgc, 0, 0, nezd.w, nezd.h);
		XSetForeground(nezd.dpy, nezd.rgc, nezd.norm[ColBG]);
	}

	parse_line(text, line, align, reverse, 0);
}

long
getcolor(const char *colstr) {
	Colormap cmap = DefaultColormap(nezd.dpy, nezd.screen);
	XColor color;

	if(!XAllocNamedColor(nezd.dpy, cmap, colstr, &color, &color))
		return -1;

	return color.pixel;
}

void
setfont(const char *fontstr) {
#ifndef NEZD_XFT
	char *def, **missing;
	int i, n;

	missing = NULL;
	if(nezd.font.set)
		XFreeFontSet(nezd.dpy, nezd.font.set);

	nezd.font.set = XCreateFontSet(nezd.dpy, fontstr, &missing, &n, &def);
	if(missing)
		XFreeStringList(missing);

	if(nezd.font.set) {
		XFontSetExtents *font_extents;
		XFontStruct **xfonts;
		char **font_names;
		nezd.font.ascent = nezd.font.descent = 0;
		font_extents = XExtentsOfFontSet(nezd.font.set);
		n = XFontsOfFontSet(nezd.font.set, &xfonts, &font_names);
		for(i = 0, nezd.font.ascent = 0, nezd.font.descent = 0; i < n; i++) {
			if(nezd.font.ascent < (*xfonts)->ascent)
				nezd.font.ascent = (*xfonts)->ascent;
			if(nezd.font.descent < (*xfonts)->descent)
				nezd.font.descent = (*xfonts)->descent;
			xfonts++;
		}
	}
	else {
		if(nezd.font.xfont)
			XFreeFont(nezd.dpy, nezd.font.xfont);
		nezd.font.xfont = NULL;
		if(!(nezd.font.xfont = XLoadQueryFont(nezd.dpy, fontstr)))
			eprint("nezd: error, cannot load font: '%s'\n", fontstr);
		nezd.font.ascent = nezd.font.xfont->ascent;
		nezd.font.descent = nezd.font.xfont->descent;
	}
	nezd.font.height = nezd.font.ascent + nezd.font.descent;
#else
        if(nezd.font.xftfont)
           XftFontClose(nezd.dpy, nezd.font.xftfont);
	nezd.font.xftfont = XftFontOpenXlfd(nezd.dpy, nezd.screen, fontstr);
	if(!nezd.font.xftfont)
	   nezd.font.xftfont = XftFontOpenName(nezd.dpy, nezd.screen, fontstr);
	if(!nezd.font.xftfont)
	   eprint("error, cannot load font: '%s'\n", fontstr);
	nezd.font.extents = malloc(sizeof(XGlyphInfo));
	XftTextExtentsUtf8(nezd.dpy, nezd.font.xftfont, (unsigned const char *) fontstr, strlen(fontstr), nezd.font.extents);
	nezd.font.height = nezd.font.xftfont->ascent + nezd.font.xftfont->descent;
	nezd.font.width = (nezd.font.extents->width)/strlen(fontstr);
#endif
}


int
get_tokval(const char* line, char **retdata) {
	int i;
	char tokval[ARGLEN];

	for(i=0; i < ARGLEN && (*(line+i) != ')'); i++)
		tokval[i] = *(line+i);

	tokval[i] = '\0';
	*retdata = strdup(tokval);

	return i+1;
}

int
get_token(const char *line, int * t, char **tval) {
	int off=0, next_pos=0, i;
	char *tokval = NULL;

	if(*(line+1) == ESC_CHAR)
		return 0;
	line++;

	for(i=0; cmd_lookup_table[i].name; ++i) {
		if( off=cmd_lookup_table[i].off,
				!strncmp(line, cmd_lookup_table[i].name, off) ) {
			next_pos = get_tokval(line+off, &tokval);
			*t = cmd_lookup_table[i].id;
			break;
		}
	}


	*tval = tokval;
	return next_pos+off;
}

static void
setcolor(Drawable *pm, int x, int width, long tfg, long tbg, int reverse, int nobg) {

	if(nobg)
		return;

	XSetForeground(nezd.dpy, nezd.tgc, reverse ? tfg : tbg);
	XFillRectangle(nezd.dpy, *pm, nezd.tgc, x, 0, width, nezd.line_height);

	XSetForeground(nezd.dpy, nezd.tgc, reverse ? tbg : tfg);
	XSetBackground(nezd.dpy, nezd.tgc, reverse ? tfg : tbg);
}

int 
get_sens_area(char *s, int *b, char *cmd) {
	memset(cmd, 0, 1024);
    sscanf(s, "%5d", b);
    char *comma = strchr(s, ',');
    if (comma != NULL)
        strncpy(cmd, comma+1, 1024);

	return 0;
}

static int
get_rect_vals(char *s, int *w, int *h, int *x, int *y) {
	*w=*h=*x=*y=0;

	return sscanf(s, "%5dx%5d%5d%5d", w, h, x, y);
}

static int
get_circle_vals(char *s, int *d, int *a) {
	int ret;
	*d=*a=ret=0;

	return  sscanf(s, "%5d%5d", d, a);
}

static int
get_pos_vals(char *s, int *d, int *a) {
	int i=0, ret=3, onlyx=1;
	char buf[128];
	*d=*a=0;

	if(s[0] == '_') {
		if(!strncmp(s, "_LOCK_X", 7)) {
			*d = LOCK_X;
		}
		if(!strncmp(s, "_UNLOCK_X", 8)) {
			*d = UNLOCK_X;
		}
		if(!strncmp(s, "_LEFT", 5)) {
			*d = LEFT;
		}
		if(!strncmp(s, "_RIGHT", 6)) {
			*d = RIGHT;
		}
		if(!strncmp(s, "_CENTER", 7)) {
			*d = CENTER;
		}
		if(!strncmp(s, "_BOTTOM", 7)) {
			*d = BOTTOM;
		}
		if(!strncmp(s, "_TOP", 4)) {
			*d = TOP;
		}

		return 5;
	} else {
		for(i=0; s[i] && i<128; i++) {
			if(s[i] == ';') {
				onlyx=0;
				break;
			} else
				buf[i]=s[i];
		}

		if(i) {
			buf[i]='\0';
			*d=atoi(buf);
		} else
			ret=2;

		if(s[++i]) {
			*a=atoi(s+i);
		} else
			ret = 1;

		if(onlyx) ret=1;

		return ret;
	}
}

static int
get_block_align_vals(char *s, int *a, int *w)
{
	char buf[32];
	int r;
	*w = -1;
	r = sscanf(s, "%d,%31s", w, buf);
	if(!strcmp(buf, "_LEFT"))
		*a = ALIGNLEFT;
	else if(!strcmp(buf, "_RIGHT"))
		*a = ALIGNRIGHT;
	else if(!strcmp(buf, "_CENTER"))
		*a = ALIGNCENTER;
	else
		*a = -1;

	return r;
}


static int
search_icon_cache(const char* name) {
	int i;

	for(i=0; i < MAX_ICON_CACHE; i++)
		if(!strncmp(icons[i].name, name, ARGLEN))
			return i;

	return -1;
}

#ifdef NEZD_XPM
static void
cache_icon(const char* name, Pixmap pm, int w, int h) {
	if(icon_cnt >= MAX_ICON_CACHE)
		icon_cnt = 0;

	if(icons[icon_cnt].p)
		XFreePixmap(nezd.dpy, icons[icon_cnt].p);

	strncpy(icons[icon_cnt].name, name, ARGLEN);
	icons[icon_cnt].w = w;
	icons[icon_cnt].h = h;
	icons[icon_cnt].p = pm;
	icon_cnt++;
}
#endif


char *
parse_line(const char *line, int lnr, int align, int reverse, int nodraw) {
	/* bitmaps */
	unsigned int bm_w, bm_h;
	int bm_xh, bm_yh;
	/* rectangles, cirlcles*/
	int rectw, recth, rectx, recty;
	/* positioning */
	int n_posx, n_posy, set_posy=0;
	int px=0, py=0, opx=0;
	int i, next_pos=0, j=0, h=0, tw=0;
	/* buffer pos */
	const char *linep=NULL;
	/* fonts */
	int font_was_set=0;
	/* position */
	int pos_is_fixed = 0;
	/* block alignment */
	int block_align = -1;
	int block_width = -1;
	/* clickable area y tracking */
	int max_y=-1;

	/* temp buffers */
	char lbuf[MAX_LINE_LEN], *rbuf = NULL;

	/* parser state */
	int t=-1, nobg=0;
	char *tval=NULL;

	/* X stuff */
	long lastfg = nezd.norm[ColFG], lastbg = nezd.norm[ColBG];
	Fnt *cur_fnt = NULL;
#ifndef NEZD_XFT
	XGCValues gcv;
#endif
	Drawable pm=0, bm;
#ifdef NEZD_XPM
	int free_xpm_attrib = 0;
	Pixmap xpm_pm;
	XpmAttributes xpma;
	XpmColorSymbol xpms;
#endif

	/* icon cache */
	int ip;

#ifdef NEZD_XFT
	XftDraw *xftd=NULL;
	XftColor xftc;
	char *xftcs;
	char *xftcs_bg;

	/* set default fg/bg for XFT */
	xftcs = estrdup(nezd.fg);
	xftcs_bg = estrdup(nezd.bg);
#endif

	/* parse line and return the text without control commands */
	if(nodraw) {
		rbuf = emalloc(MAX_LINE_LEN);
		rbuf[0] = '\0';
		if( (lnr + nezd.slave_win.first_line_vis) >= nezd.slave_win.tcnt)
			line = NULL;
		else
			line = nezd.slave_win.tbuf[nezd.slave_win.first_line_vis+lnr];

	}
	/* parse line and render text */
	else {
		h = nezd.font.height;
		py = (nezd.line_height - h) / 2;
		xorig[LNR2WINDOW(lnr)] = 0;
		
		if(lnr != -1) {
			pm = XCreatePixmap(nezd.dpy, RootWindow(nezd.dpy, DefaultScreen(nezd.dpy)), nezd.slave_win.width,
					nezd.line_height, DefaultDepth(nezd.dpy, nezd.screen));
		}
		else {
			pm = XCreatePixmap(nezd.dpy, RootWindow(nezd.dpy, DefaultScreen(nezd.dpy)), nezd.title_win.width,
					nezd.line_height, DefaultDepth(nezd.dpy, nezd.screen));
		}

#ifdef NEZD_XFT
		xftd = XftDrawCreate(nezd.dpy, pm, DefaultVisual(nezd.dpy, nezd.screen), 
				DefaultColormap(nezd.dpy, nezd.screen));
#endif

		if(!reverse) {
			XSetForeground(nezd.dpy, nezd.tgc, nezd.norm[ColBG]);
#ifdef NEZD_XPM
			xpms.pixel = nezd.norm[ColBG];
#endif
#ifdef NEZD_XFT
			xftcs_bg = estrdup(nezd.bg);
#endif
		}
		else {
			XSetForeground(nezd.dpy, nezd.tgc, nezd.norm[ColFG]);
#ifdef NEZD_XPM
			xpms.pixel = nezd.norm[ColFG];
#endif
		}
		XFillRectangle(nezd.dpy, pm, nezd.tgc, 0, 0, nezd.w, nezd.h);

		if(!reverse) {
			XSetForeground(nezd.dpy, nezd.tgc, nezd.norm[ColFG]);
		}
		else {
			XSetForeground(nezd.dpy, nezd.tgc, nezd.norm[ColBG]);
		}

#ifdef NEZD_XPM
		xpms.name = NULL;
		xpms.value = (char *)"none";

		xpma.colormap = DefaultColormap(nezd.dpy, nezd.screen);
		xpma.depth = DefaultDepth(nezd.dpy, nezd.screen);
		xpma.visual = DefaultVisual(nezd.dpy, nezd.screen);
		xpma.colorsymbols = &xpms;
		xpma.numsymbols = 1;
		xpma.valuemask = XpmColormap|XpmDepth|XpmVisual|XpmColorSymbols;
#endif

#ifndef NEZD_XFT 
		if(!nezd.font.set){
			gcv.font = nezd.font.xfont->fid;
			XChangeGC(nezd.dpy, nezd.tgc, GCFont, &gcv);
		}
#endif
		cur_fnt = &nezd.font;

		if( lnr != -1 && (lnr + nezd.slave_win.first_line_vis >= nezd.slave_win.tcnt)) {
			XCopyArea(nezd.dpy, pm, nezd.slave_win.drawable[lnr], nezd.gc,
					0, 0, px, nezd.line_height, xorig[LNR2WINDOW(lnr)], 0);
			XFreePixmap(nezd.dpy, pm);
			return NULL;
		}
	}

	linep = line;
	while(1) {
		if(*linep == ESC_CHAR || *linep == '\0') {
			lbuf[j] = '\0';

			/* clear _lock_x at EOL so final width is correct */
			if(*linep=='\0')
				pos_is_fixed=0;

			if(nodraw) {
				strcat(rbuf, lbuf);
			}
			else {
				if(t != -1 && tval) {
					switch(t) {
						case icon:
							if(MAX_ICON_CACHE && (ip=search_icon_cache(tval)) != -1) {
								int y;
								XCopyArea(nezd.dpy, icons[ip].p, pm, nezd.tgc,
										0, 0, icons[ip].w, icons[ip].h, px, y=(set_posy ? py :
										(nezd.line_height >= (signed)icons[ip].h ?
										(nezd.line_height - icons[ip].h)/2 : 0)));
								px += !pos_is_fixed ? icons[ip].w : 0;
								max_y = MAX(max_y, y+icons[ip].h);
							} else {
								int y;
								if(XReadBitmapFile(nezd.dpy, pm, tval, &bm_w,
											&bm_h, &bm, &bm_xh, &bm_yh) == BitmapSuccess
										&& (h/2 + px + (signed)bm_w < nezd.w)) {
									setcolor(&pm, px, bm_w, lastfg, lastbg, reverse, nobg);

									XCopyPlane(nezd.dpy, bm, pm, nezd.tgc,
											0, 0, bm_w, bm_h, px, y=(set_posy ? py :
											(nezd.line_height >= (int)bm_h ?
												(nezd.line_height - (int)bm_h)/2 : 0)), 1);
									XFreePixmap(nezd.dpy, bm);
									px += !pos_is_fixed ? bm_w : 0;
									max_y = MAX(max_y, y+bm_h);
								}
#ifdef NEZD_XPM
								else if(XpmReadFileToPixmap(nezd.dpy, nezd.title_win.win, tval, &xpm_pm, NULL, &xpma) == XpmSuccess) {
									setcolor(&pm, px, xpma.width, lastfg, lastbg, reverse, nobg);

									if(MAX_ICON_CACHE)
										cache_icon(tval, xpm_pm, xpma.width, xpma.height);

									XCopyArea(nezd.dpy, xpm_pm, pm, nezd.tgc,
											0, 0, xpma.width, xpma.height, px, y=(set_posy ? py :
											(nezd.line_height >= (int)xpma.height ?
												(nezd.line_height - (int)xpma.height)/2 : 0)));
									px += !pos_is_fixed ? xpma.width : 0;
									max_y = MAX(max_y, y+xpma.height);

									/* freed by cache_icon() */
									//XFreePixmap(nezd.dpy, xpm_pm);
									free_xpm_attrib = 1;
								}
#endif
							}
							break;


						case rect:
							get_rect_vals(tval, &rectw, &recth, &rectx, &recty);
							recth = recth > nezd.line_height ? nezd.line_height : recth;
							if(set_posy)
								py += recty;
							recty =	recty == 0 ? (nezd.line_height - recth)/2 :
								(nezd.line_height - recth)/2 + recty;
							px += !pos_is_fixed ? rectx : 0;
							setcolor(&pm, px, rectw, lastfg, lastbg, reverse, nobg);

							XFillRectangle(nezd.dpy, pm, nezd.tgc, px,
									set_posy ? py :
									((int)recty < 0 ? nezd.line_height + recty : recty),
									rectw, recth);

							px += !pos_is_fixed ? rectw : 0;
							break;

						case recto:
							get_rect_vals(tval, &rectw, &recth, &rectx, &recty);
							if (!rectw) break;

							recth = recth > nezd.line_height ? nezd.line_height-2 : recth-1;
							if(set_posy)
								py += recty;
							recty =	recty == 0 ? (nezd.line_height - recth)/2 :
								(nezd.line_height - recth)/2 + recty;
							px = (rectx == 0) ? px : rectx+px;
							/* prevent from stairs effect when rounding recty */
							if (!((nezd.line_height - recth) % 2)) recty--;
							setcolor(&pm, px, rectw, lastfg, lastbg, reverse, nobg);
							XDrawRectangle(nezd.dpy, pm, nezd.tgc, px,
									set_posy ? py :
									((int)recty<0 ? nezd.line_height + recty : recty), rectw-1, recth);
							px += !pos_is_fixed ? rectw : 0;
							break;

						case circle:
							rectx = get_circle_vals(tval, &rectw, &recth);
							setcolor(&pm, px, rectw, lastfg, lastbg, reverse, nobg);
							XFillArc(nezd.dpy, pm, nezd.tgc, px, set_posy ? py :(nezd.line_height - rectw)/2,
									rectw, rectw, 90*64, rectx>1?recth*64:64*360);
							px += !pos_is_fixed ? rectw : 0;
							break;

						case circleo:
							rectx = get_circle_vals(tval, &rectw, &recth);
							setcolor(&pm, px, rectw, lastfg, lastbg, reverse, nobg);
							XDrawArc(nezd.dpy, pm, nezd.tgc, px, set_posy ? py : (nezd.line_height - rectw)/2,
									rectw, rectw, 90*64, rectx>1?recth*64:64*360);
							px += !pos_is_fixed ? rectw : 0;
							break;

						case pos:
							if(tval[0]) {
								int r=0;
								r = get_pos_vals(tval, &n_posx, &n_posy);
								if( (r == 1 && !set_posy))
									set_posy=0;
								else if (r == 5) {
									switch(n_posx) {
										case LOCK_X:
											pos_is_fixed = 1;
											break;
										case UNLOCK_X:
											pos_is_fixed = 0;
											break;
										case LEFT:
											px = 0;
											break;
										case RIGHT:
											px = nezd.w;
											break;
										case CENTER:
											px = nezd.w/2;
											break;
										case BOTTOM:
											set_posy = 1;
											py = nezd.line_height;
											break;
										case TOP:
											set_posy = 1;
											py = 0;
											break;
									}
								} else
									set_posy=1;

								if(r != 2)
									px = px+n_posx<0? 0 : px + n_posx;
								if(r != 1) 
									py += n_posy;
							} else {
								set_posy = 0;
								py = (nezd.line_height - nezd.font.height) / 2;
							}
							break;

						case abspos:
							if(tval[0]) {
								int r=0;
								if( (r=get_pos_vals(tval, &n_posx, &n_posy)) == 1 && !set_posy)
									set_posy=0;
								else
									set_posy=1;

								n_posx = n_posx < 0 ? n_posx*-1 : n_posx;
								if(r != 2)
									px = n_posx;
								if(r != 1)
									py = n_posy;
							} else {
								set_posy = 0;
								py = (nezd.line_height - nezd.font.height) / 2;
							}
							break;

						case ibg:
							nobg = atoi(tval);
							break;

						case bg:
							lastbg = tval[0] ? (unsigned)getcolor(tval) : nezd.norm[ColBG];
#ifdef NEZD_XFT
							if(xftcs_bg) free(xftcs_bg);
							xftcs_bg = estrdup(tval[0] ? tval : nezd.bg);
#endif

							break;

						case fg:
							lastfg = tval[0] ? (unsigned)getcolor(tval) : nezd.norm[ColFG];
							XSetForeground(nezd.dpy, nezd.tgc, lastfg);
#ifdef NEZD_XFT
							if (xftcs) free(xftcs);
							xftcs = estrdup(tval[0] ? tval : nezd.fg);
#endif
							break;

						case fn:
							if(tval[0]) {
#ifndef NEZD_XFT
								if(!strncmp(tval, "dfnt", 4)) {
									cur_fnt = &(nezd.fnpl[atoi(tval+4)]);

									if(!cur_fnt->set) {
										gcv.font = cur_fnt->xfont->fid;
										XChangeGC(nezd.dpy, nezd.tgc, GCFont, &gcv);
									}
								}
								else
#endif					
									setfont(tval);
							}
							else {
								cur_fnt = &nezd.font;
#ifndef NEZD_XFT		
								if(!cur_fnt->set){
									gcv.font = cur_fnt->xfont->fid;
									XChangeGC(nezd.dpy, nezd.tgc, GCFont, &gcv);
								}
#else
							setfont(nezd.fnt ? nezd.fnt : FONT);
#endif								
							}
							py = set_posy ? py : (nezd.line_height - cur_fnt->height) / 2;
							font_was_set = 1;
							break;
						case ca:
							; //nop to keep gcc happy
							sens_w *w = &window_sens[LNR2WINDOW(lnr)];
							
							if(tval[0]) {
								click_a *area = &((*w).sens_areas[(*w).sens_areas_cnt]);
								if((*w).sens_areas_cnt < MAX_CLICKABLE_AREAS) {
									get_sens_area(tval, 
											&(*area).button, 
											(*area).cmd);
									(*area).start_x = px;
									(*area).start_y = py;
									(*area).end_y = py;
									max_y = py;
									(*area).active = 0;
									if(lnr == -1) {
										(*area).win = nezd.title_win.win;
									} else {
										(*area).win = nezd.slave_win.line[lnr];
									}
									(*w).sens_areas_cnt++;
								}
							} else {
									//find most recent unclosed area
									for(i = (*w).sens_areas_cnt - 1; i >= 0; i--)
										if(!(*w).sens_areas[i].active)
											break;
									if(i >= 0 && i < MAX_CLICKABLE_AREAS) {
										(*w).sens_areas[i].end_x = px;
										(*w).sens_areas[i].end_y = max_y;
										(*w).sens_areas[i].active = 1;
								}
							}
							break;
						case ba:
							if(tval[0])
								get_block_align_vals(tval, &block_align, &block_width);
							else
								block_align=block_width=-1;
							break;
					}
					free(tval);
				}

				/* check if text is longer than window's width */
				tw = textnw(cur_fnt, lbuf, strlen(lbuf));
				while((((tw + px) > (nezd.w)) || (block_align!=-1 && tw>block_width)) && j>=0) {
					lbuf[--j] = '\0';
					tw = textnw(cur_fnt, lbuf, strlen(lbuf));
				}
				
				opx = px;

				/* draw background for block */
				if(block_align!=-1 && !nobg) {
					setcolor(&pm, px, rectw, lastbg, lastbg, 0, nobg);
					XFillRectangle(nezd.dpy, pm, nezd.tgc, px, 0, block_width, nezd.line_height);
				}

				if(block_align==ALIGNRIGHT)
					px += (block_width - tw);
				else if(block_align==ALIGNCENTER)
					px += (block_width/2) - (tw/2);

				if(!nobg)
					setcolor(&pm, px, tw, lastfg, lastbg, reverse, nobg);
				
#ifndef NEZD_XFT
				if(cur_fnt->set)
					XmbDrawString(nezd.dpy, pm, cur_fnt->set,
							nezd.tgc, px, py + cur_fnt->ascent, lbuf, strlen(lbuf));
				else
					XDrawString(nezd.dpy, pm, nezd.tgc, px, py+nezd.font.ascent, lbuf, strlen(lbuf));
#else
				if(reverse) {
				XftColorAllocName(nezd.dpy, DefaultVisual(nezd.dpy, nezd.screen),
						DefaultColormap(nezd.dpy, nezd.screen),  xftcs_bg,  &xftc);
				} else {
				XftColorAllocName(nezd.dpy, DefaultVisual(nezd.dpy, nezd.screen),
						DefaultColormap(nezd.dpy, nezd.screen),  xftcs,  &xftc);
				}

				XftDrawStringUtf8(xftd, &xftc,
						cur_fnt->xftfont, px, py + nezd.font.xftfont->ascent, (const FcChar8 *)lbuf, strlen(lbuf));
#endif

				max_y = MAX(max_y, py+nezd.font.height);

				if(block_align==-1) {
					if(!pos_is_fixed || *linep =='\0')
						px += tw;
				} else {
					if(pos_is_fixed)
						px = opx;
					else
						px = opx+block_width;
				}

				block_align=block_width=-1;
			}

			if(*linep=='\0')
				break;

			j=0; t=-1; tval=NULL;
			next_pos = get_token(linep, &t, &tval);
			linep += next_pos;

			/* ^^ escapes */
			if(next_pos == 0)
				lbuf[j++] = *linep++;
		}
		else
			lbuf[j++] = *linep;

		linep++;
	}

	if(!nodraw) {
		/* expand/shrink dynamically */
		if(nezd.title_win.expand && lnr == -1){
			i = px;
			switch(nezd.title_win.expand) {
				case left:
					/* grow left end */
					otx = nezd.title_win.x_right_corner - i > nezd.title_win.x ?
						nezd.title_win.x_right_corner - i : nezd.title_win.x;
					XMoveResizeWindow(nezd.dpy, nezd.title_win.win, otx, nezd.title_win.y, px, nezd.line_height);
					break;
				case right:
					XResizeWindow(nezd.dpy, nezd.title_win.win, px, nezd.line_height);
					break;
			}

		} else {
			if(align == ALIGNLEFT)
				xorig[LNR2WINDOW(lnr)] = 0;
			if(align == ALIGNCENTER) {
				xorig[LNR2WINDOW(lnr)] = (lnr != -1) ?
					(nezd.slave_win.width - px)/2 :
					(nezd.title_win.width - px)/2;
			}
			else if(align == ALIGNRIGHT) {
				xorig[LNR2WINDOW(lnr)] = (lnr != -1) ?
					(nezd.slave_win.width - px) :
					(nezd.title_win.width - px);
			}
		}


		if(lnr != -1) {
			XCopyArea(nezd.dpy, pm, nezd.slave_win.drawable[lnr], nezd.gc,
                    0, 0, nezd.w, nezd.line_height, xorig[LNR2WINDOW(lnr)], 0);
		}
		else {
			XCopyArea(nezd.dpy, pm, nezd.title_win.drawable, nezd.gc,
					0, 0, nezd.w, nezd.line_height, xorig[LNR2WINDOW(lnr)], 0);
		}
		XFreePixmap(nezd.dpy, pm);

		/* reset font to default */
		if(font_was_set)
			setfont(nezd.fnt ? nezd.fnt : FONT);

#ifdef NEZD_XPM
		if(free_xpm_attrib) {
			XFreeColors(nezd.dpy, xpma.colormap, xpma.pixels, xpma.npixels, xpma.depth);
			XpmFreeAttributes(&xpma);
		}
#endif

#ifdef NEZD_XFT
		XftDrawDestroy(xftd);
#endif
	}

#ifdef NEZD_XFT
	if(xftcs) free(xftcs);
	if(xftcs_bg) free(xftcs_bg);
#endif

	return nodraw ? rbuf : NULL;
}

int
parse_non_drawing_commands(char * text) {

	if(!text)
		return 1;

	if(!strncmp(text, "^togglecollapse()", strlen("^togglecollapse()"))) {
		a_togglecollapse(NULL);
		return 0;
	}
	if(!strncmp(text, "^collapse()", strlen("^collapse()"))) {
		a_collapse(NULL);
		return 0;
	}
	if(!strncmp(text, "^uncollapse()", strlen("^uncollapse()"))) {
		a_uncollapse(NULL);
		return 0;
	}

	if(!strncmp(text, "^togglestick()", strlen("^togglestick()"))) {
		a_togglestick(NULL);
		return 0;
	}
	if(!strncmp(text, "^stick()", strlen("^stick()"))) {
		a_stick(NULL);
		return 0;
	}
	if(!strncmp(text, "^unstick()", strlen("^unstick()"))) {
		a_unstick(NULL);
		return 0;
	}

	if(!strncmp(text, "^togglehide()", strlen("^togglehide()"))) {
		a_togglehide(NULL);
		return 0;
	}
	if(!strncmp(text, "^hide()", strlen("^hide()"))) {
		a_hide(NULL);
		return 0;
	}
	if(!strncmp(text, "^unhide()", strlen("^unhide()"))) {
		a_unhide(NULL);
		return 0;
	}

	if(!strncmp(text, "^raise()", strlen("^raise()"))) {
		a_raise(NULL);
		return 0;
	}

	if(!strncmp(text, "^lower()", strlen("^lower()"))) {
		a_lower(NULL);
		return 0;
	}

	if(!strncmp(text, "^scrollhome()", strlen("^scrollhome()"))) {
		a_scrollhome(NULL);
		return 0;
	}

	if(!strncmp(text, "^scrollend()", strlen("^scrollend()"))) {
		a_scrollend(NULL);
		return 0;
	}

	if(!strncmp(text, "^exit()", strlen("^exit()"))) {
		a_exit(NULL);
		return 0;
	}

	return 1;
}


void
drawheader(const char * text) {
	if(parse_non_drawing_commands((char *)text)) {
		if (text){
			nezd.w = nezd.title_win.width;
			nezd.h = nezd.line_height;
			
			window_sens[TOPWINDOW].sens_areas_cnt = 0;
			
			XFillRectangle(nezd.dpy, nezd.title_win.drawable, nezd.rgc, 0, 0, nezd.w, nezd.h);
			parse_line(text, -1, nezd.title_win.alignment, 0, 0);
		}
	} else {
		nezd.slave_win.tcnt = -1;
		nezd.cur_line = 0;
	}

	XCopyArea(nezd.dpy, nezd.title_win.drawable, nezd.title_win.win,
			nezd.gc, 0, 0, nezd.title_win.width, nezd.line_height, 0, 0);
}

void
drawbody(char * text) {
	char *ec;
	int i, write_buffer=1;
	

	if(nezd.slave_win.tcnt == -1) {
		nezd.slave_win.tcnt = 0;
		drawheader(text);
		return;
	}

	
	if((ec = strstr(text, "^tw()")) && (*(ec-1) != '^')) {
		drawheader(ec+5);
		return;
	}

	if(nezd.slave_win.tcnt == nezd.slave_win.tsize)
		free_buffer();

	write_buffer = parse_non_drawing_commands(text);


	if(text[0] == '^' && text[1] == 'c' && text[2] == 's') {
		free_buffer();

		for(i=0; i < nezd.slave_win.max_lines; i++)
			XFillRectangle(nezd.dpy, nezd.slave_win.drawable[i], nezd.rgc, 0, 0, nezd.slave_win.width, nezd.line_height);
		x_draw_body();
		return;
	}

	if( write_buffer && (nezd.slave_win.tcnt < nezd.slave_win.tsize) ) {
		nezd.slave_win.tbuf[nezd.slave_win.tcnt] = estrdup(text);
		nezd.slave_win.tcnt++;
	}
}
