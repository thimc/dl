#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <keyboard.h>
#include <mouse.h>

enum {
	Bordersiz = 2,
	Barheight = 25,
};

Keyboardctl *kctl;
Mousectl *mctl;

char *menustr[] = { "exit", 0 };
Menu menu = { menustr };

int autoflag, exitflag;
char *url, *out;
long pos, siz, eta, dur, speed;
float percentage;

Image *background, *inner;

void redraw(void);

int
webclone(int *conn)
{
	char buf[128];
	int n, fd;

	if((fd = open("/mnt/web/clone", ORDWR)) < 0)
		sysfatal("webclone: %r");
	if((n = read(fd, buf, sizeof(buf))) < 0)
		sysfatal("webclone %d: reading clone: %r", fd);
	if(n == 0)
		sysfatal("webclone %d: short read on clone", fd);
	buf[n] = '\0';
	*conn = atoi(buf);

	return fd;
}

void
getfilename(int fd)
{
	char buf[64], *s;
	int n, ufd;

	snprint(buf, sizeof(buf), "/mnt/web/%d/parsed/url", fd);
	if((ufd = open(buf, OREAD)) < 0)
		sysfatal("open %s: %r", buf);
	n = read(ufd, buf, sizeof(buf));
	buf[n] = 0;
	s = utfrrune(buf, '/');
	if(s)
		s++;
	else
		s = buf;
	if((out = malloc(sizeof(*out) * strlen(s))) == nil)
		sysfatal("malloc: %r");
	strcpy(out, s);
	close(ufd);

}

long
readsize(int fd)
{
	char buf[128], body[1024];
	int bfd, clfd, n;

	snprint(buf, sizeof(buf), "/mnt/web/%d/body", fd);
	if((bfd = open(buf, OREAD)) < 0)
		sysfatal("open %s: %r", buf);

	snprint(buf, sizeof(buf), "/mnt/web/%d/contentlength", fd);
	if((clfd = open(buf, OREAD)) < 0)
		sysfatal("open %s: %r", buf);
	if((n = readn(clfd, body, sizeof(body))) < 0)
		sysfatal("readn %s: %r", buf);

	close(clfd);
	close(bfd);
	body[n] = '\0';
	return strtol(body, 0, 0);
}


char*
formatbytes(ulong mem)
{
	static char bytestr[12];
	const char *unit = "BKMGTPZYREQ?";
	int n;
	double siz;

	bytestr[0] = 0;
	if(percentage == 1.0)
		return "";
	for(n = 0, siz = (double)mem * 1024.0; siz >= 1024.0;){
		siz /= 1024.0;
		n++;
	}
	if(n > 0)
		n--;
	if(n >= strlen(unit) || snprint(bytestr, sizeof(bytestr), "%.2f%c", siz, unit[n]) < 1)
		strncat(bytestr, "?", sizeof(bytestr));
	if(n > 0)
		strncat(bytestr + strlen(bytestr), "B", sizeof(bytestr));
	strncat(bytestr + strlen(bytestr), "/s", sizeof(bytestr));

	return bytestr;
}

char*
remainingtime(long t)
{
	static char timestr[128];
	long h, m, s;

	h = t / 3600;
	m = (t % 3600) / 60;
	s = t % 60;
	timestr[0] = 0;
	if(h > 0) snprint(timestr + strlen(timestr), sizeof(timestr) - strlen(timestr), "%ldh", h);
	if(m > 0) snprint(timestr + strlen(timestr), sizeof(timestr) - strlen(timestr), "%ldm", m);
	if(s > 0) snprint(timestr + strlen(timestr), sizeof(timestr) - strlen(timestr), "%lds", s);
	if(percentage < 1.0 && speed > 0 && timestr[0] == 0)
		snprint(timestr, sizeof(timestr), "<1s");
	return timestr;
}

void
redraw(void)
{
	char buf[128];
	Rectangle bar, progr;
	Point pt, pp;
	int n;

	draw(screen, screen->r, background, nil, ZP);

	bar = screen->r;
	n = stringwidth(font, "0");
	bar.min.x += n;
	bar.max.x -= n;

	bar.min.y = screen->r.min.y + (Dy(screen->r) - font->height) / 2 + font->height;
	bar.max.y = bar.min.y + Barheight;

	progr = (Rectangle){ addpt(bar.min, Pt(Bordersiz, Bordersiz)), subpt(bar.max, Pt(Bordersiz, Bordersiz)) };
	if(percentage > 1.0)
		percentage = 1.0;
	progr.max.x *= percentage;
	pt = bar.min;
	pt.y -= font->height;

	n = Dx(bar) / font->width;
	snprint(buf, sizeof(buf), "eta: %s  speed: %s  %.1f%%",
		remainingtime(eta), formatbytes(speed), 100.0 * percentage);
	n -= strlen(buf) + 2;

	pp = bar.min;
	pp.y -= font->height;
	pp.x += Dx(bar) - stringwidth(font, buf);
	string(screen, pp, display->black, ZP, font, buf);

	snprint(buf, sizeof(buf), "%s %s", percentage < 1.0 ? "Downloading" : "Done!", url);
	if(out)
		snprint(buf + strlen(buf), sizeof(buf) - strlen(buf), " -> %s", out);

	if(n > 3 && n < strlen(buf))
		snprint(buf + n - 3, 3, "..");

	string(screen, pt, display->black, ZP, font, buf);
	border(screen, bar, Bordersiz, display->black, ZP);
	draw(screen, progr, inner, nil, ZP);
	flushimage(display, 1);
}

void
dlproc(void *c)
{
 	char buf[1024], p[256];
 	long start, n, curr;
 	int ctlfd, fd, fdout;
 
 	threadsetname("dlproc");
 	ctlfd = webclone(&fd);

 	fprint(ctlfd, "url %s", url);
 	fprint(ctlfd, "request HEAD");

	if((siz = readsize(fd)) < 1)
		sysfatal("could not determine the file size for url: '%s'", url);

	if(autoflag)
		getfilename(fd);

 	fprint(ctlfd, "url %s", url);
 	snprint(p, sizeof(p), "/mnt/web/%d/body", fd);
 	if((fd = open(p, OREAD)) < 0)
 		sysfatal("open %s: %r", buf);

 	if(out != nil){
 		if((fdout = create(out, OWRITE, 0644)) < 0)
 			sysfatal("open %s: %r", "out");
 	}else{
		fdout = 1;
 	}
	sendul(c, 0);

	start = time(nil);
	while((n = readn(fd, buf, sizeof(buf))) > 0){
		curr = time(nil);
		pos += n;
		write(fdout, buf, sizeof(buf));
		if(curr - start < 1)
			continue;
		dur = curr - start;			// seconds
		speed = pos / dur;			// bytes per second
		eta = (siz - pos) / speed;	// seconds
		percentage = (float)pos / (float)siz;
		sendul(c, 0);
	}
	close(ctlfd);
	close(fd);
	if(out != nil)
		close(fdout);
	if(exitflag)
		threadexitsall(nil);
}

void
usage(void)
{
	fprint(2, "usage: %s [-o output] [-ax] url\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	Mouse m;
	Rune k;
	Alt alts[] = {
		{ nil, &m,  CHANRCV },
		{ nil, &k,  CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, nil, CHANEND },
	};

	exitflag = 1;
	ARGBEGIN{
	case 'a':
		autoflag++;
		break;
	case 'o':
		out = EARGF(usage());
		break;
	case 'x':
		exitflag--;
		break;
	}ARGEND;
	if(!argc)
		usage();
	url = argv[0];

	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	alts[0].c = mctl->c;
	alts[1].c = kctl->c;
	alts[2].c = mctl->resizec;
	alts[3].c = chancreate(sizeof(ulong), 0);
	proccreate(dlproc, alts[3].c, 4096);

	background = allocimagemix(display, DPalebluegreen, DWhite);
	inner = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DPalegreygreen);

	for(;;){
		switch(alt(alts)){
		case 0:
			if(m.buttons & 4){
				if(menuhit(3, mctl, &menu, nil) == 0)
					threadexitsall(nil);
 			}
			break;
		case 1:
			switch(k){
			case 'q':
			case Kdel:
			case Keof:
				threadexitsall(nil);
			}
			break;
		case 2:
			if(getwindow(display, Refnone) < 0)
				sysfatal("getwindow: %r");
			break;
		case 3:
			redraw();
		}
		redraw();
	}

}
