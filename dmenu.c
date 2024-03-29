/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef XFT
#include <X11/Xft/Xft.h>
#endif
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

/* macros */
#define CLEANMASK(mask)         (mask & ~(numlockmask | LockMask))
#define INRECT(X,Y,RX,RY,RW,RH) ((X) >= (RX) && (X) < (RX) + (RW) && (Y) >= (RY) && (Y) < (RY) + (RH))
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define HIST_SIZE 20

/* enums */
enum { ColFG, ColBG, ColLast };

typedef struct {
    unsigned long x[ColLast];
#ifdef XFT
    XftColor xft[ColLast];
#endif
} COL;

/* typedefs */
typedef struct {
	int x, y, w, h;
	COL norm;
	COL sel;
	COL last;
	Drawable drawable;
#ifdef XFT
	XftDraw *xftdrawable;
#endif
	GC gc;
	struct {
		XFontStruct *xfont;
		XFontSet set;
		int ascent;
		int descent;
		int height;
#ifdef XFT
		XftFont *xftfont;
		XGlyphInfo *extents;
#endif
	} font;
} DC; /* draw context */

typedef struct Item Item;
struct Item {
	char *text;
	Item *next;		/* traverses all items */
	Item *left, *right;	/* traverses items matching current search pattern */
};

/* forward declarations */
static void appenditem(Item *i, Item **list, Item **last);
static void calcoffsetsh(void);
static void calcoffsetsv(void);
static char *cistrstr(const char *s, const char *sub);
static void cleanup(void);
static void drawmenuh(void);
static void drawmenuv(void);
static void drawtext(const char *text, COL col);
static void eprint(const char *errstr, ...);
static unsigned long getcolor(const char *colstr);
#ifdef XFT
static unsigned long getxftcolor(const char *colstr, XftColor *color);
#endif
static Bool grabkeyboard(void);
static void initfont(const char *fontstr);
static void kpress(XKeyEvent * e);
static void resizewindow(void);
static void match(char *pattern);
static void readstdin(void);
static void run(void);
static void setup(void);
static int textnw(const char *text, unsigned int len);
static int textw(const char *text);

#include "config.h"

/* variables */
static char *maxname = NULL;
static char *prompt = NULL;
static char *lastitem = NULL; 
static char *nl = "";
static char **tokens = NULL;
static char text[4096];
static char hitstxt[16];
static int cmdw = 0;
static int promptw = 0;
static int ret = 0;
static int screen;
static unsigned int mw, mh, bh;
static int x, y;
static unsigned int numlockmask = 0;
static unsigned int hits = 0;
static unsigned int lines = 0;
static unsigned int xoffset = 0;
static unsigned int yoffset = 0;
static unsigned int width = 0;
static unsigned int height = 0;
static Bool running = True;
static Bool topbar = True;
static Bool vlist = False;
static Bool hitcounter = False;
static Bool alignright = False;
static Bool multiselect = False;
static Bool resize = False;
static Bool marklastitem = False;
static Bool indicators = True;
static Bool xmms = False;
static Display *dpy;
static DC dc;
static Item *allitems = NULL;	/* first of all items */
static Item *item = NULL;	/* first of pattern matching items */
static Item *sel = NULL;
static Item *next = NULL;
static Item *prev = NULL;
static Item *curr = NULL;
static Window root, win;
static int (*fstrncmp)(const char *, const char *, size_t n) = strncmp;
static char *(*fstrstr)(const char *, const char *) = strstr;
static void (*calcoffsets)(void) = calcoffsetsh;
static void (*drawmenu)(void) = drawmenuh;
static char hist[HIST_SIZE][1024];
static char *histfile = NULL;
static int hcnt = 0;

static int
writehistory(char *command) {
   int i = 0, j = hcnt;
   FILE *f;

   if(!histfile || strlen(command) <= 0)
      return 0;

   if( (f = fopen(histfile, "w")) ) {
      fputs(command, f);
         fputc('\n', f);
      for(; i<HIST_SIZE && i<j; i++) {
         if(strcmp(command, hist[i]) != 0) {
            fputs(hist[i], f);
            fputc('\n', f);
         }
      }
      fclose(f);
      return 1;
   }

   return 0;
}

static int
readhistory (void) {
   char buf[1024];
   FILE *f;


   if(!histfile)
      return 0;

   if( (f = fopen(histfile, "r+")) ) {
      while(fgets(buf, sizeof buf, f) && (hcnt < HIST_SIZE))  
         strncpy(hist[hcnt++], buf, (strlen(buf) <= 1024) ? strlen(buf): 1024 );
      fclose(f);
   }

   return hcnt;
}

void
appenditem(Item *i, Item **list, Item **last) {
	if(!(*last))
		*list = i;
	else
		(*last)->right = i;
	i->left = *last;
	i->right = NULL;
	*last = i;
	++hits;
}

void
calcoffsetsh(void) {
	static int tw;
	static unsigned int w;

	if(!curr)
		return;
	w = promptw + cmdw + 2 * spaceitem;
	for(next = curr; next; next=next->right) {
		tw = textw(next->text);
		if(tw > mw / 3)
			tw = mw / 3;
		w += tw;
		if(w > mw)
			break;
	}
	w = promptw + cmdw + 2 * spaceitem;
	for(prev = curr; prev && prev->left; prev=prev->left) {
		tw = textw(prev->left->text);
		if(tw > mw / 3)
			tw = mw / 3;
		w += tw;
		if(w > mw)
			break;
	}
}

void
calcoffsetsv(void) {
	static unsigned int w;

	if(!curr)
		return;
	w = (dc.font.height + 2) * (lines + 1);
	for(next = curr; next; next=next->right) {
		w -= dc.font.height + 2;
		if(w <= 0)
			break;
	}
	w = (dc.font.height + 2) * (lines + 1);
	for(prev = curr; prev && prev->left; prev=prev->left) {
		w -= dc.font.height + 2;
		if(w <= 0)
			break;
	}
}

char *
cistrstr(const char *s, const char *sub) {
	int c, csub;
	unsigned int len;

	if(!sub)
		return (char *)s;
	if((c = *sub++) != 0) {
		c = tolower(c);
		len = strlen(sub);
		do {
			do {
				if((csub = *s++) == 0)
					return NULL;
			}
			while(tolower(csub) != c);
		}
		while(strncasecmp(s, sub, len) != 0);
		s--;
	}
	return (char *)s;
}

void
cleanup(void) {
	Item *itm;

	while(allitems) {
		itm = allitems->next;
		free(allitems->text);
		free(allitems);
		allitems = itm;
	}
	if(!dc.font.xftfont) {
		if(dc.font.set)
			XFreeFontSet(dpy, dc.font.set);
		else
			XFreeFont(dpy, dc.font.xfont);
	}
	XFreePixmap(dpy, dc.drawable);
	XFreeGC(dpy, dc.gc);
	XDestroyWindow(dpy, win);
	XUngrabKeyboard(dpy, CurrentTime);
	free(tokens);
}

void
drawmenuh(void) {
	static Item *i;

	dc.x = 0;
	dc.y = 0;
	dc.w = mw;
	dc.h = mh;
	drawtext(NULL, dc.norm);
	/* print prompt? */
	if(promptw) {
		dc.w = promptw;
		drawtext(prompt, dc.sel);
	}
	dc.x += promptw;
	dc.w = mw - promptw;
	/* print command */
	if(cmdw && item)
		dc.w = cmdw;
	drawtext(text[0] ? text : NULL, dc.norm);
	dc.x += cmdw;
	if(curr) {
		dc.w = spaceitem;
		drawtext((curr && curr->left) ? "<" : NULL, dc.norm);
		dc.x += dc.w;
		/* determine maximum items */
		for(i = curr; i != next; i=i->right) {
			dc.w = textw(i->text);
			if(dc.w > mw / 3)
				dc.w = mw / 3;
			drawtext(i->text, (sel == i) ? dc.sel : dc.norm);
			dc.x += dc.w;
		}
		dc.x = mw - spaceitem;
		dc.w = spaceitem;
		drawtext(next ? ">" : NULL, dc.norm);
	}
	XCopyArea(dpy, dc.drawable, win, dc.gc, 0, 0, mw, mh, 0, 0);
	XFlush(dpy);
}

void
drawmenuv(void) {
	static Item *i;

	dc.x = 0;
	dc.y = 0;
	dc.h = mh;
	drawtext(NULL, dc.norm);
	/* print prompt? */
	if(promptw) {
		dc.w = promptw;
		drawtext(prompt, dc.sel);
	}
	dc.x += promptw;
	dc.w = mw - promptw - (hitcounter ? textnw(hitstxt, strlen(hitstxt)) : 0);

	drawtext(text[0] ? text : NULL, dc.norm);
	if(curr) {
		if (hitcounter) {
			dc.w = textw(hitstxt);
			dc.x = mw - textw(hitstxt);
			drawtext(hitstxt, dc.norm);
		}
		dc.x = 0;
		dc.w = mw;
		if (indicators) {	
			dc.y += dc.font.height + 2;
			drawtext((curr && curr->left) ? "^" : NULL, dc.norm);
		}
		dc.y += dc.font.height + 2;
		/* determine maximum items */
		for(i = curr; i != next; i=i->right) {
			if((sel != i) && marklastitem && lastitem && !strncmp(lastitem, i->text, strlen(i->text)))
				drawtext(i->text, dc.last);
			else
				drawtext(i->text, (sel == i) ? dc.sel : dc.norm);
			dc.y += dc.font.height + 2;
		}
		drawtext(indicators && next ? "v" : NULL, dc.norm);
	} else {
		if (hitcounter) {
			dc.w = textw(hitstxt);
			dc.x = mw - textw(hitstxt);
			dc.y = 0;
			drawtext(hitstxt, dc.norm);
		}
		dc.x = 0;
		dc.w = mw;
		dc.h = mh;
		dc.y += dc.font.height + 2;
		drawtext(NULL, dc.norm);
	} 
	XCopyArea(dpy, dc.drawable, win, dc.gc, 0, 0, mw, mh, 0, 0);
	XFlush(dpy);
}

void
updatemenuv(Bool updown) {
	static Item *i;
	
	if(curr) {
		dc.x = 0;
		dc.y = (dc.font.height + 2) * (indicators?2:1);
		dc.w = mw;
		dc.h = mh;
		for(i = curr; i != next; i = i->right) {
			if(((i == sel->left) && !updown) || (i == sel)
			||((i == sel->right) && updown)) {
				if((sel != i) && marklastitem && lastitem && !strncmp(lastitem, i->text, strlen(i->text)))
					drawtext(i->text, dc.last);
				else
					drawtext(i->text, (sel == i) ? dc.sel : dc.norm);
				XCopyArea(dpy, dc.drawable, win, dc.gc, dc.x, dc.y,
					dc.w, dc.font.height + 2, dc.x, dc.y);
			}
			dc.y += dc.font.height + 2;
		}
	}			
	XFlush(dpy);
}

void
drawtext(const char *text, COL col) {
	char buf[256];
	int i, x, y, h, len, olen;
	XRectangle r = { dc.x, dc.y, dc.w, dc.h };

	XSetForeground(dpy, dc.gc, col.x[ColBG]);
	XFillRectangles(dpy, dc.drawable, dc.gc, &r, 1);
	if(!text)
		return;
	olen = strlen(text);
	h = dc.font.height;
	y = dc.y + ((h + 2) / 2) - (h / 2) + dc.font.ascent;
	x = dc.x + (h / 2);
	/* shorten text if necessary */
	for(len = MIN(olen, sizeof buf); len && textnw(text, len) > dc.w - h; len--);
	if(!len)
		return;
	memcpy(buf, text, len);
	if(len < olen)
		for(i = len; i && i > len - 3; buf[--i] = '.');
#ifdef XFT
	if(dc.font.xftfont)
		XftDrawStringUtf8(dc.xftdrawable, &col.xft[ColFG], dc.font.xftfont, x, y, (unsigned char*) buf, len);
	else {
#endif
	XSetForeground(dpy, dc.gc, col.x[ColFG]);
	if(dc.font.set)
		XmbDrawString(dpy, dc.drawable, dc.font.set, dc.gc, x, y, buf, len);
	else
		XDrawString(dpy, dc.drawable, dc.gc, x, y, buf, len);
#ifdef XFT
	}
#endif
}

void
eprint(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

unsigned long
getcolor(const char *colstr) {
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor color;

	if(!XAllocNamedColor(dpy, cmap, colstr, &color, &color))
		eprint("error, cannot allocate color '%s'\n", colstr);
	return color.pixel;
}

#ifdef XFT
unsigned long
getxftcolor(const char *colstr, XftColor *color) {
	Colormap cmap = DefaultColormap(dpy, screen);
	Visual *vis = DefaultVisual(dpy, screen);

	if(!XftColorAllocName(dpy, vis, cmap, colstr, color))
		eprint("error, cannot allocate color '%s'\n", colstr);
	return color->pixel;
}
#endif

Bool
grabkeyboard(void) {
	unsigned int len;

	for(len = 1000; len; len--) {
		if(XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime)
		== GrabSuccess)
			break;
		usleep(1000);
	}
	return len > 0;
}

void
initfont(const char *fontstr) {
#ifdef XFT
	dc.font.xftfont = 0;
	if(cistrstr(fontstr,"xft:")) {
		dc.font.xftfont = XftFontOpenXlfd(dpy, screen, fontstr+4);
		if(!dc.font.xftfont)
			dc.font.xftfont = XftFontOpenName(dpy, screen, fontstr+4);
		if(!dc.font.xftfont)
			eprint("error, cannot load font: '%s'\n", fontstr+4);
		dc.font.extents = malloc(sizeof(XGlyphInfo));
		XftTextExtentsUtf8(dpy, dc.font.xftfont, (unsigned const char *) fontstr+4, strlen(fontstr+4), dc.font.extents);
		dc.font.height = dc.font.xftfont->ascent + dc.font.xftfont->descent;
		dc.font.ascent = dc.font.xftfont->ascent;
		dc.font.descent = dc.font.xftfont->descent;
	}
	else {
#endif
	char *def, **missing;
	int i, n;

	if(!fontstr || fontstr[0] == '\0')
		eprint("error, cannot load font: '%s'\n", fontstr);
	missing = NULL;
	dc.font.set = XCreateFontSet(dpy, fontstr, &missing, &n, &def);
	if(missing)
		XFreeStringList(missing);
	if(dc.font.set) {
		XFontSetExtents *font_extents;
		XFontStruct **xfonts;
		char **font_names;
		dc.font.ascent = dc.font.descent = 0;
		font_extents = XExtentsOfFontSet(dc.font.set);
		n = XFontsOfFontSet(dc.font.set, &xfonts, &font_names);
		for(i = 0, dc.font.ascent = 0, dc.font.descent = 0; i < n; i++) {
			if(dc.font.ascent < (*xfonts)->ascent)
				dc.font.ascent = (*xfonts)->ascent;
			if(dc.font.descent < (*xfonts)->descent)
				dc.font.descent = (*xfonts)->descent;
			xfonts++;
		}
	}
	else {
		if(!(dc.font.xfont = XLoadQueryFont(dpy, fontstr))
		&& !(dc.font.xfont = XLoadQueryFont(dpy, "fixed")))
			eprint("error, cannot load font: '%s'\n", fontstr);
		dc.font.ascent = dc.font.xfont->ascent;
		dc.font.descent = dc.font.xfont->descent;
	}
	dc.font.height = dc.font.ascent + dc.font.descent;
#ifdef XFT
	}
#endif
}

void
kpress(XKeyEvent * e) {
	char buf[32];
	int i, num;
	unsigned int len;
	KeySym ksym;

	len = strlen(text);
	buf[0] = 0;
	num = XLookupString(e, buf, sizeof buf, &ksym, NULL);
	if(IsKeypadKey(ksym)) {
		if(ksym == XK_KP_Enter)
			ksym = XK_Return;
		else if(ksym >= XK_KP_0 && ksym <= XK_KP_9)
			ksym = (ksym - XK_KP_0) + XK_0;
	}
	if(IsFunctionKey(ksym) || IsKeypadKey(ksym)
	   || IsMiscFunctionKey(ksym) || IsPFKey(ksym)
	   || IsPrivateKeypadKey(ksym))
		return;
	/* first check if a control mask is omitted */
	if(e->state & ControlMask) {
		switch (ksym) {
		default:	/* ignore other control sequences */
			return;
		case XK_bracketleft:
			ksym = XK_Escape;
			break;
		case XK_h:
		case XK_H:
			ksym = XK_BackSpace;
			break;
		case XK_i:
		case XK_I:
			ksym = XK_Tab;
			break;
		case XK_j:
		case XK_J:
			ksym = XK_Return;
			break;
		case XK_u:
		case XK_U:
			text[0] = 0;
			match(text);
			drawmenu();
			return;
		case XK_w:
		case XK_W:
			if(len) {
				i = len - 1;
				while(i >= 0 && text[i] == ' ')
					text[i--] = 0;
				while(i >= 0 && text[i] != ' ')
					text[i--] = 0;
				match(text);
				drawmenu();
			}
			return;
		}
	}
	if(CLEANMASK(e->state) & Mod1Mask) {
		switch(ksym) {
		default: return;
		case XK_h:
			ksym = XK_Left;
			break;
		case XK_l:
			ksym = XK_Right;
			break;
		case XK_j:
			ksym = XK_Next;
			break;
		case XK_k:
			ksym = XK_Prior;
			break;
		case XK_g:
			ksym = XK_Home;
			break;
		case XK_G:
			ksym = XK_End;
			break;
		}
	}
	switch(ksym) {
	default:
		if(num && !iscntrl((int) buf[0])) {
			buf[num] = 0;
			strncpy(text + len, buf, sizeof text - len);
			match(text);
		}
		break;
	case XK_BackSpace:
		if(len) {
			text[--len] = 0;
			match(text);
		}
		break;
	case XK_End:
		if(!item)
			return;
		while(next) {
			sel = curr = next;
			calcoffsets();
		}
		while(sel && sel->right)
			sel = sel->right;
		break;
	case XK_Escape:
		ret = 1;
		running = False;
		break;
	case XK_Home:
		if(!item)
			return;
		sel = curr = item;
		calcoffsets();
		break;
	case XK_Left:
	case XK_Up:
		if(!(sel && sel->left))
			return;
		sel=sel->left;
		if(sel->right == curr) {
			if (vlist)
				curr = curr->left;
			else
				curr = prev;
			calcoffsets();
		} else {
			if (vlist) {
				updatemenuv(True);
				return;
			}
		}
		break;
	case XK_Next:
		if(!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XK_Prior:
		if(!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XK_Return:
		if((e->state & ShiftMask) && *text)
			fprintf(stdout, "%s%s", text, nl);
		else if(sel) {
			fprintf(stdout, "%s%s", sel->text, nl);
			lastitem = sel->text;
		}
		else if(*text)
			fprintf(stdout, "%s%s", text, nl);
        writehistory( (sel == NULL) ? text : sel->text);
		fflush(stdout);
		running = multiselect;
		break;
	case XK_Right:
	case XK_Down:
		if(!(sel && sel->right))
			return;
		sel=sel->right;
		if(sel == next) {
			if (vlist)
				curr = curr->right;
			else
				curr = next;
			calcoffsets();
		} else {
			if (vlist) {
				updatemenuv(False);
				return;
			}
		}
		break;
	case XK_Tab:
		if(!sel)
			return;
		strncpy(text, sel->text, sizeof text);
		match(text);
		break;
	}
	drawmenu();
}

void resizewindow(void)
{
	if (resize) {
		static int rlines, ry, rmh;

		rlines = (hits > lines ? lines : hits) + (indicators?3:1);
		rmh = vlist ? (dc.font.height + 2) * rlines : mh;
		ry = topbar ? y + yoffset : y - rmh + (dc.font.height + 2) - yoffset;
		XMoveResizeWindow(dpy, win, x, ry, mw, rmh);
	}
}

unsigned int tokenize(char *pat, char **tok)
{
	unsigned int i = 0;
	char tmp[4096] = {0};

	strncpy(tmp, pat, strlen(pat));
	tok[0] = strtok(tmp, " ");

	while(tok[i] && i < maxtokens)
		tok[++i] = strtok(NULL, " ");
	return i;
}

void
match(char *pattern) {
	unsigned int plen, tokencnt = 0;
	char append = 0;
	Item *i, *itemend, *lexact, *lprefix, *lsubstr, *exactend, *prefixend, *substrend;

	if(!pattern)
		return;

	if(!xmms)
		tokens[(tokencnt = 1)-1] = pattern;
	else
		if(!(tokencnt = tokenize(pattern, tokens)))
			tokens[(tokencnt = 1)-1] = "";

	item = lexact = lprefix = lsubstr = itemend = exactend = prefixend = substrend = NULL;
	for(i = allitems; i; i = i->next) {
		for(int j = 0; j < tokencnt; ++j) {
			plen = strlen(tokens[j]);
			if(!fstrncmp(tokens[j], i->text, plen + 1))
				append = !append || append > 1 ? 1 : append;
			else if(!fstrncmp(tokens[j], i->text, plen ))
				append = !append || append > 2 ? 2 : append;
			else if(fstrstr(i->text, tokens[j]))
				append = append > 0 && append < 3 ? append : 3;
			else {
				append = 0;
				break;
			}
		}
		if(append == 1)
			appenditem(i, &lexact, &exactend);
		else if(append == 2)
			appenditem(i, &lprefix, &prefixend);
		else if(append == 3)
			appenditem(i, &lsubstr, &substrend);
	}
	if(lexact) {
		item = lexact;
		itemend = exactend;
	}
	if(lprefix) {
		if(itemend) {
			itemend->right = lprefix;
			lprefix->left = itemend;
		}
		else
			item = lprefix;
		itemend = prefixend;
	}
	if(lsubstr) {
		if(itemend) {
			itemend->right = lsubstr;
			lsubstr->left = itemend;
		}
		else
			item = lsubstr;
	}
	curr = prev = next = sel = item;
	calcoffsets();
	resizewindow();
	snprintf(hitstxt, sizeof(hitstxt), "(%d)", hits);
	hits = 0;
}

void
readstdin(void) {
	char *p, buf[1024];
	unsigned int len = 0, max = 0;
	Item *i, *new;
	int k;

	i = 0;
	if( readhistory() )  {
       for(k=0; k<hcnt; k++) {
          len = strlen(hist[k]);
          if (hist[k][len - 1] == '\n')
             hist[k][len - 1] = 0;
          p = strdup(hist[k]);
          if(max < len) {
             maxname = p;
             max = len;
          }
          if((new = (Item *)malloc(sizeof(Item))) == NULL)
             eprint("fatal: could not malloc() %u bytes\n", sizeof(Item));
          new->next = new->left = new->right = NULL;
          new->text = p;
          if(!i)
             allitems = new;
          else 
             i->next = new;
          i = new;
       }
    }
    len=0; max=0;

	while(fgets(buf, sizeof buf, stdin)) {
		len = strlen(buf);
		if (buf[len - 1] == '\n')
			buf[len - 1] = 0;
		if(!(p = strdup(buf)))
			eprint("fatal: could not strdup() %u bytes\n", strlen(buf));
		if(max < len) {
			maxname = p;
			max = len;
		}
		if(!(new = (Item *)malloc(sizeof(Item))))
			eprint("fatal: could not malloc() %u bytes\n", sizeof(Item));
		new->next = new->left = new->right = NULL;
		new->text = p;
		if(!i)
			allitems = new;
		else 
			i->next = new;
		i = new;
	}
}

void
run(void) {
	XEvent ev;

	/* main event loop */
	while(running && !XNextEvent(dpy, &ev))
		switch (ev.type) {
		default:	/* ignore all crap */
			break;
		case KeyPress:
			kpress(&ev.xkey);
			break;
		case Expose:
			if(ev.xexpose.count == 0)
				drawmenu();
			break;
		}
}

void
setup(void) {
	int i, j, sy, slines;
#if XINERAMA
	int n;
	XineramaScreenInfo *info = NULL;
#endif
	XModifierKeymap *modmap;
	XSetWindowAttributes wa;

	/* init modifier map */
	modmap = XGetModifierMapping(dpy);
	for(i = 0; i < 8; i++)
		for(j = 0; j < modmap->max_keypermod; j++) {
			if(modmap->modifiermap[i * modmap->max_keypermod + j]
			== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
		}
	XFreeModifiermap(modmap);

	/* style */
	initfont(font);
#ifdef XFT
	if(dc.font.xftfont) {
		dc.norm.x[ColBG] = getxftcolor(normbgcolor, &dc.norm.xft[ColBG]);
		dc.norm.x[ColFG] = getxftcolor(normfgcolor, &dc.norm.xft[ColFG]);
		dc.sel.x[ColBG] = getxftcolor(selbgcolor, &dc.sel.xft[ColBG]);
        dc.sel.x[ColFG] = getxftcolor(selfgcolor, &dc.sel.xft[ColFG]);
        dc.last.x[ColBG] = getxftcolor(lastbgcolor, &dc.last.xft[ColBG]);
        dc.last.x[ColFG] = getxftcolor(lastfgcolor, &dc.last.xft[ColFG]);
	}
	else {
#endif
	dc.norm.x[ColBG] = getcolor(normbgcolor);
	dc.norm.x[ColFG] = getcolor(normfgcolor);
	dc.sel.x[ColBG] = getcolor(selbgcolor);
	dc.sel.x[ColFG] = getcolor(selfgcolor);
	dc.last.x[ColBG] = getcolor(lastbgcolor);
	dc.last.x[ColFG] = getcolor(lastfgcolor);
#ifdef XFT
	}
#endif

	/* menu window */
	wa.override_redirect = True;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | ButtonPressMask | KeyPressMask;

	/* menu window geometry */
	mh = dc.font.height + 2;
	if(mh < bh)
		mh = bh;
#if XINERAMA
	if(XineramaIsActive(dpy) && (info = XineramaQueryScreens(dpy, &n))) {
		i = 0;
		if(n > 1) {
			int di;
			unsigned int dui;
			Window dummy;
			if(XQueryPointer(dpy, root, &dummy, &dummy, &x, &y, &di, &di, &dui))
				for(i = 0; i < n; i++)
					if(INRECT(x, y, info[i].x_org, info[i].y_org, info[i].width, info[i].height))
						break;
		}
		x = info[i].x_org;
		y = topbar ? info[i].y_org : info[i].y_org + info[i].height - mh;
		mw = info[i].width;
		XFree(info);
	}
	else
#endif
	{
		x = 0;
		y = topbar ? 0 : DisplayHeight(dpy, screen) - mh;
		mw = DisplayWidth(dpy, screen);
	}

	/* update menu window geometry */
	
	slines = (lines ? lines : (lines = height / (dc.font.height + 2))) + (indicators?3:1);
	mh = vlist ? (dc.font.height + 2) * slines : mh;
	sy = topbar ? y + yoffset : y - mh + (dc.font.height + 2) - yoffset;
	x = alignright ? mw - (width ? width : mw) - xoffset : xoffset;
	mw = width ? width : mw;

	win = XCreateWindow(dpy, root, x, sy, mw, mh, 0,
			DefaultDepth(dpy, screen), CopyFromParent,
			DefaultVisual(dpy, screen),
			CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

	/* pixmap */
	dc.drawable = XCreatePixmap(dpy, root, mw, mh, DefaultDepth(dpy, screen));
	dc.gc = XCreateGC(dpy, root, 0, NULL);
	XSetLineAttributes(dpy, dc.gc, 1, LineSolid, CapButt, JoinMiter);
#ifdef XFT
	if(dc.font.xftfont) {
		dc.xftdrawable = XftDrawCreate(dpy, dc.drawable, DefaultVisual(dpy,screen), DefaultColormap(dpy,screen));
		if(!dc.xftdrawable)
			eprint("error, cannot create drawable\n");
	}
	else {
#endif
	if(!dc.font.set)
		XSetFont(dpy, dc.gc, dc.font.xfont->fid);
#ifdef XFT
	}
#endif
	if(maxname)
		cmdw = textw(maxname);
	if(cmdw > mw / 3)
		cmdw = mw / 3;
	if(prompt)
		promptw = textw(prompt);
	if(promptw > mw / 5)
		promptw = mw / 5;
	text[0] = 0;
	tokens = malloc((xmms?maxtokens:1)*sizeof(char*));
	match(text);
	XMapRaised(dpy, win);
	/* set WM_CLASS */
    XClassHint *ch = XAllocClassHint();
    ch->res_name = "dmenu";
    ch->res_class = "DockApp";
    XSetClassHint(dpy, win, ch);
    XFree(ch);
}

int
textnw(const char *text, unsigned int len) {
#ifdef XFT
	if (dc.font.xftfont) {
		XftTextExtentsUtf8(dpy, dc.font.xftfont, (unsigned const char *) text, strlen(text), dc.font.extents);
		if(dc.font.extents->height > dc.font.height)
			dc.font.height = dc.font.extents->height;
		return dc.font.extents->xOff;
	}
	else {
#endif
	XRectangle r;

	if(dc.font.set) {
		XmbTextExtents(dc.font.set, text, len, NULL, &r);
		return r.width;
	}
	return XTextWidth(dc.font.xfont, text, len);
#ifdef XFT
	}
#endif
}

int
textw(const char *text) {
	return textnw(text, strlen(text)) + dc.font.height;
}

int
main(int argc, char *argv[]) {
	unsigned int i;

	/* command line args */
	for(i = 1; i < argc; i++)
		if(!strcmp(argv[i], "-i")) {
			fstrncmp = strncasecmp;
			fstrstr = cistrstr;
		}
		else if(!strcmp(argv[i], "-b"))
			topbar = False;
		else if(!strcmp(argv[i], "-r"))
			alignright = True;
		else if(!strcmp(argv[i], "-l")) {
			vlist = True;
			calcoffsets = calcoffsetsv;
			drawmenu = drawmenuv;
			if(++i < argc) lines += atoi(argv[i]);
		}
		else if(!strcmp(argv[i], "-c"))
			hitcounter = True;
		else if(!strcmp(argv[i], "-fn")) {
			if(++i < argc) font = argv[i];
		}
		else if(!strcmp(argv[i], "-nb")) {
			if(++i < argc) normbgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-nf")) {
			if(++i < argc) normfgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-p")) {
			if(++i < argc) prompt = argv[i];
		}
		else if(!strcmp(argv[i], "-sb")) {
			if(++i < argc) selbgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-sf")) {
			if(++i < argc) selfgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-bh")) {
			if(++i < argc) bh = atoi(argv[i]);
		}
		else if(!strcmp(argv[i], "-hist")) {
			if(++i < argc) histfile = argv[i];
        }
		else if(!strcmp(argv[i], "-lb")) {
			if(++i < argc) lastbgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-lf")) {
			if(++i < argc) lastfgcolor = argv[i];
		}
		else if(!strcmp(argv[i], "-w")) {
			if(++i < argc) width = atoi(argv[i]);
		}
		else if(!strcmp(argv[i], "-h")) {
			vlist = True;
			calcoffsets = calcoffsetsv;
			drawmenu = drawmenuv;
			if(++i < argc) height = atoi(argv[i]);
		}
		else if(!strcmp(argv[i], "-x")) {
			if(++i < argc) xoffset = atoi(argv[i]);
		}
		else if(!strcmp(argv[i], "-y")) {
			if(++i < argc) yoffset = atoi(argv[i]);
		}
		else if(!strcmp(argv[i], "-nl"))
			nl = "\n";
		else if(!strcmp(argv[i], "-rs"))
			resize = True;
		else if(!strcmp(argv[i], "-ms"))
			multiselect = True;
		else if(!strcmp(argv[i], "-ml"))
			marklastitem = True;
		else if(!strcmp(argv[i], "-ni"))
			indicators = False;
		else if(!strcmp(argv[i], "-xs"))
			xmms = True;
		else if(!strcmp(argv[i], "-v"))
			eprint("dmenu-"VERSION", (c) 2006-2008 dmenu engineers, see LICENSE for details\n");
		else
			eprint("usage: dmenu [-i] [-b] [-r] [-x <xoffset>] [-y <yoffset>] [-w <width>]\n"
			       "[-fn <font>] [-nb <color>] [-nf <color>] [-p <prompt>] [-sb <color>]\n"
			       "[-sf <color>] [-l <#items>] [-h <height>] [-bg <height>] [-c] [-ms]\n"
			       "[-ml] [-lb <color>] [-lf <color>] [-rs] [-ni] [-nl] [-xs] [-hist <filename>] [-v]\n");

	if(!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fprintf(stderr, "warning: no locale support\n");
	if(!(dpy = XOpenDisplay(NULL)))
		eprint("dmenu: cannot open display\n");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	if(isatty(STDIN_FILENO)) {
		readstdin();
		running = grabkeyboard();
	}
	else { /* prevent keypress loss */
		running = grabkeyboard();
		readstdin();
	}
	
	setup();
	drawmenu();
	XSync(dpy, False);
	run();
	cleanup();
	XCloseDisplay(dpy);
	return ret;
}
