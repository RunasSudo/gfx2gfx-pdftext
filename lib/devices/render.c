/* gfxdevice_bitmap.cc

   Part of the swftools package.

   Copyright (c) 2005 Matthias Kramm <kramm@quiss.org> 
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <memory.h>
#include "../gfxdevice.h"
#include "../gfxtools.h"
#include "../png.h"
#include "../mem.h"

typedef unsigned int U32;
typedef unsigned char U8;

typedef gfxcolor_t RGBA;

typedef struct _renderpoint
{
    float x;
} renderpoint_t;

typedef struct _renderline
{
    renderpoint_t*points;
    int size;
    int num;
} renderline_t;

typedef struct _internal_result {
    int width;
    int height;
    RGBA* img;
    struct _internal_result*next;
} internal_result_t;

typedef struct _clipbuffer {
    U32*data;
    int linesize;
    struct _clipbuffer*prev;
} clipbuffer_t;

typedef struct _fontlist
{
    gfxfont_t*font;
    char*id;
    struct _fontlist*next;
} fontlist_t;

typedef struct _internal {
    int width;
    int height;
    int width2;
    int height2;
    int multiply;
    int antialize;
    int ymin, ymax;

    int depth;

    RGBA* img;
    int* zbuf; 

    gfxfont_t*font;
    char*fontid;
    
    fontlist_t* fontlist;

    clipbuffer_t*clipbufs;
    clipbuffer_t*clipbuf;

    renderline_t*lines;

    internal_result_t*results;
    internal_result_t*result_next;
} internal_t;

typedef enum {filltype_solid,filltype_clip,filltype_bitmap} filltype_t;

typedef struct _fillinfo {
    filltype_t type; //0=solid,1=clip
    gfxcolor_t*color;
    gfximage_t*image;
    gfxmatrix_t*matrix;
    gfxcxform_t*cxform;
    char clip;
} fillinfo_t;


static inline void add_pixel(internal_t*i, float x, int y)
{
    renderpoint_t p;

    if(x >= i->width2 || y >= i->height2 || y<0) return;
    p.x = x;
    if(y<i->ymin) i->ymin = y;
    if(y>i->ymax) i->ymax = y;

    renderline_t*l = &i->lines[y];

    if(l->num == l->size) {
	l->size += 32;
	l->points = (renderpoint_t*)rfx_realloc(l->points, l->size * sizeof(renderpoint_t));
    }
    l->points[l->num] = p;
    l->num++;
}

/* set this to 0.777777 or something if the "both fillstyles set while not inside shape"
   problem appears to often */
#define CUT 0.5

#define INT(x) ((int)((x)+16)-16)

static void add_line(gfxdevice_t*dev , double x1, double y1, double x2, double y2)
{
    internal_t*i = (internal_t*)dev->internal;
    double diffx, diffy;
    double ny1, ny2, stepx;
/*    if(DEBUG&4) {
        int l = sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
        printf(" l[%d - %.2f/%.2f -> %.2f/%.2f]\n", l, x1/20.0, y1/20.0, x2/20.0, y2/20.0);
    }*/

    if(y2 < y1) {
        double x;
        double y;
	x = x1;x1 = x2;x2=x;
	y = y1;y1 = y2;y2=y;
    }
    
    diffx = x2 - x1;
    diffy = y2 - y1;
    
    ny1 = INT(y1)+CUT;
    ny2 = INT(y2)+CUT;

    if(ny1 < y1) {
        ny1 = INT(y1) + 1.0 + CUT;
    }
    if(ny2 >= y2) {
        ny2 = INT(y2) - 1.0 + CUT;
    }

    if(ny1 > ny2)
        return;

    stepx = diffx/diffy;
    x1 = x1 + (ny1-y1)*stepx;
    x2 = x2 + (ny2-y2)*stepx;

    {
	int posy=INT(ny1);
	int endy=INT(ny2);
	double posx=0;
	double startx = x1;

	while(posy<=endy) {
	    float xx = (float)(startx + posx);
	    add_pixel(i, xx ,posy);
	    posx+=stepx;
	    posy++;
	}
    }
}
#define PI 3.14159265358979
static void add_solidline(gfxdevice_t*dev, double x1, double y1, double x2, double y2, double width)
{
    internal_t*i = (internal_t*)dev->internal;

    double dx = x2-x1;
    double dy = y2-y1;
    double sd;
    double d;

    int t;
    int segments;
    double lastx,lasty;
    double vx,vy;
    double xx,yy;
  
    /* Make sure the line is always at least one pixel wide */
#ifdef LINEMODE1
    /* That's what Macromedia's Player does at least at zoom level >= 1.  */
    width += 1.0;
#else
    /* That's what Macromedia's Player seems to do at zoom level 0.  */
    /* TODO: needs testing */

    /* TODO: how does this interact with scaling? */
    if(width * i->multiply < 1.0)
	width = 1.0 / i->multiply;
#endif

    sd = (double)dx*(double)dx+(double)dy*(double)dy;
    d = sqrt(sd);

    if(!dx && !dy) {
        vx = 1;
        vy = 0;
    } else {
        vx = ( dy/d);
        vy = (-dx/d);
    }

    segments = width/2;
    if(segments < 2)
        segments = 2;

    segments = 8;

    vx=vx*width*0.5;
    vy=vy*width*0.5;

    xx = x2+vx;
    yy = y2+vy;
    add_line(dev, x1+vx, y1+vy, xx, yy);
    lastx = xx;
    lasty = yy;
    for(t=1;t<segments;t++) {
        double s = sin(t*PI/segments);
        double c = cos(t*PI/segments);
        xx = (x2 + vx*c - vy*s);
        yy = (y2 + vx*s + vy*c);
        add_line(dev, lastx, lasty, xx, yy);
        lastx = xx;
        lasty = yy;
    }
    
    xx = (x2-vx);
    yy = (y2-vy);
    add_line(dev, lastx, lasty, xx, yy);
    lastx = xx;
    lasty = yy;
    xx = (x1-vx);
    yy = (y1-vy);
    add_line(dev, lastx, lasty, xx, yy);
    lastx = xx;
    lasty = yy;
    for(t=1;t<segments;t++) {
        double s = sin(t*PI/segments);
        double c = cos(t*PI/segments);
        xx = (x1 - vx*c + vy*s);
        yy = (y1 - vx*s - vy*c);
        add_line(dev, lastx, lasty, xx, yy);
        lastx = xx;
        lasty = yy;
    }
    add_line(dev, lastx, lasty, (x1+vx), (y1+vy));
}

static int compare_renderpoints(const void * _a, const void * _b)
{
    renderpoint_t*a = (renderpoint_t*)_a;
    renderpoint_t*b = (renderpoint_t*)_b;
    if(a->x < b->x) return -1;
    if(a->x > b->x) return 1;
    return 0;
}

static void fill_line_solid(RGBA*line, U32*z, int y, int x1, int x2, RGBA col)
{
    int x = x1;

    U32 bit = 1<<(x1&31);
    int bitpos = (x1/32);

    if(col.a!=255) {
        int ainv = 255-col.a;
        col.r = (col.r*col.a)>>8;
        col.g = (col.g*col.a)>>8;
        col.b = (col.b*col.a)>>8;
        col.a = 255;
        do {
	    if(z[bitpos]&bit) {
		line[x].r = ((line[x].r*ainv)>>8)+col.r;
		line[x].g = ((line[x].g*ainv)>>8)+col.g;
		line[x].b = ((line[x].b*ainv)>>8)+col.b;
		line[x].a = 255;
	    }
	    bit <<= 1;
	    if(!bit) {
		bit = 1;bitpos++;
	    }
        } while(++x<x2);
    } else {
        do {
	    if(z[bitpos]&bit) {
		line[x] = col;
	    }
	    bit <<= 1;
	    if(!bit) {
		bit = 1;bitpos++;
	    }
        } while(++x<x2);
    }
}

static void fill_line_bitmap(RGBA*line, U32*z, int y, int x1, int x2, fillinfo_t*info)
{
    int x = x1;

    gfxmatrix_t*m = info->matrix;
    gfximage_t*b = info->image;
    
    double det = m->m00*m->m11 - m->m01*m->m10;
    if(fabs(det) < 0.0005) { 
	/* x direction equals y direction- the image is invisible */
	return;
    }
    det = 1.0/det;
    
    if(!b->width || !b->height) {
	gfxcolor_t red = {255,255,0,0};
        fill_line_solid(line, z, y, x1, x2, red);
        return;
    }

    U32 bit = 1<<(x1&31);
    int bitpos = (x1/32);

    do {
	if(z[bitpos]&bit) {
	    RGBA col;
	    int xx = (int)((  (x - m->tx) * m->m11 - (y - m->ty) * m->m10)*det);
	    int yy = (int)((- (x - m->tx) * m->m01 + (y - m->ty) * m->m00)*det);
	    int ainv;

	    if(info->clip) {
		if(xx<0) xx=0;
		if(xx>=b->width) xx = b->width-1;
		if(yy<0) yy=0;
		if(yy>=b->height) yy = b->height-1;
	    } else {
		xx %= b->width;
		yy %= b->height;
		if(xx<0) xx += b->width;
		if(yy<0) yy += b->height;
	    }

	    col = b->data[yy*b->width+xx];
	    ainv = 255-col.a;

	    line[x].r = ((line[x].r*ainv)>>8)+col.r;
	    line[x].g = ((line[x].g*ainv)>>8)+col.g;
	    line[x].b = ((line[x].b*ainv)>>8)+col.b;
	    line[x].a = 255;
	}
	bit <<= 1;
	if(!bit) {
	    bit = 1;bitpos++;
	}
    } while(++x<x2);
}

static void fill_line_clip(RGBA*line, U32*z, int y, int x1, int x2)
{
    int x = x1;

    U32 bit = 1<<(x1&31);
    int bitpos = (x1/32);

    do {
	z[bitpos]|=bit;
	bit <<= 1;
	if(!bit) {
	    bit = 1;bitpos++;
	}
    } while(++x<x2);
}

void fill_line(gfxdevice_t*dev, RGBA*line, U32*zline, int y, int startx, int endx, fillinfo_t*fill)
{
    if(fill->type == filltype_solid)
	fill_line_solid(line, zline, y, startx, endx, *fill->color);
    else if(fill->type == filltype_clip)
	fill_line_clip(line, zline, y, startx, endx);
    else if(fill->type == filltype_bitmap)
	fill_line_bitmap(line, zline, y, startx, endx, fill);
    // etc.
}

void fill(gfxdevice_t*dev, fillinfo_t*fill)
{
    internal_t*i = (internal_t*)dev->internal;
    int y;
    U32 clipdepth = 0;
    for(y=i->ymin;y<=i->ymax;y++) {
	renderpoint_t*points = i->lines[y].points;
        RGBA*line = &i->img[i->width2*y];
        int*zline = &i->zbuf[i->width2*y];
	int n;
	int num = i->lines[y].num;
	int lastx;
        qsort(points, num, sizeof(renderpoint_t), compare_renderpoints);

        for(n=0;n<num;n++) {
            renderpoint_t*p = &points[n];
            renderpoint_t*next= n<num-1?&points[n+1]:0;
            int startx = p->x;
            int endx = next?next->x:i->width2;
            if(endx > i->width2)
                endx = i->width2;
            if(startx < 0)
                startx = 0;
            if(endx < 0)
                endx = 0;

	    if(!(n&1))
		fill_line(dev, line, zline, y, startx, endx, fill);

	    lastx = endx;
            if(endx == i->width2)
                break;
        }
	i->lines[y].num = 0;
    }
}

void fill_solid(gfxdevice_t*dev, gfxcolor_t* color)
{
    fillinfo_t info;
    info.type = filltype_solid;
    info.color = color;
    fill(dev, &info);
}

int render_setparameter(struct _gfxdevice*dev, const char*key, const char*value)
{
    internal_t*i = (internal_t*)dev->internal;
    if(!strcmp(key, "antialize")) {
	i->antialize = atoi(value);
    } else if(!strcmp(key, "multiply")) {
	i->multiply = atoi(value);
    }
    return 0;
}

void newclip(struct _gfxdevice*dev)
{
    internal_t*i = (internal_t*)dev->internal;
    
    clipbuffer_t*c = rfx_calloc(sizeof(clipbuffer_t));
    c->linesize = ((i->width2+31) / 32);
    c->data = rfx_calloc(c->linesize * i->height2);

    if(!i->clipbufs) {
	i->clipbufs = i->clipbuf = c;
    } else {
	clipbuffer_t*old = i->clipbuf;
	i->clipbuf = c;
	i->clipbuf->prev = old;
    }
}

void endclip(struct _gfxdevice*dev)
{
    internal_t*i = (internal_t*)dev->internal;
    
    if(!i->clipbufs) {
	fprintf(stderr, "endclip without any active clip buffers");
	return;
    }
    
    clipbuffer_t*old = i->clipbuf;

    if(i->clipbuf == i->clipbufs)
	i->clipbufs = 0;

    i->clipbuf = i->clipbuf->prev;

    old->prev = 0;
    free(old->data);old->data = 0;
    free(old);
}

void render_stroke(struct _gfxdevice*dev, gfxline_t*line, gfxcoord_t width, gfxcolor_t*color, gfx_capType cap_style, gfx_joinType joint_style, gfxcoord_t miterLimit)
{
    internal_t*i = (internal_t*)dev->internal;
    double x,y;
    
    if(cap_style != gfx_capRound || joint_style != gfx_joinRound) {
	fprintf(stderr, "Warning: cap/joint style != round not yet supported\n");
    }

    while(line) {
        int x1,y1,x2,y2,x3,y3;

        if(line->type == gfx_moveTo) {
        } else if(line->type == gfx_lineTo) {
	    double x1=x,y1=y;
	    double x3=line->x,y3=line->y;
	    add_solidline(dev, x1, y1, x3, y3, width * i->multiply);
	    fill_solid(dev, color);
        } else if(line->type == gfx_splineTo) {
	    int c,t,parts,qparts;
	    double xx,yy;
           
	    double x1=x,y1=y;
	    double x2=line->sx,y2=line->sy;
	    double x3=line->x,y3=line->y;
            
            c = abs(x3-2*x2+x1) + abs(y3-2*y2+y1);
            xx=x1;
	    yy=y1;

            parts = (int)(sqrt(c)/3);
            if(!parts) parts = 1;

            for(t=1;t<=parts;t++) {
                double nx = (double)(t*t*x3 + 2*t*(parts-t)*x2 + (parts-t)*(parts-t)*x1)/(double)(parts*parts);
                double ny = (double)(t*t*y3 + 2*t*(parts-t)*y2 + (parts-t)*(parts-t)*y1)/(double)(parts*parts);
                
		add_solidline(dev, xx, yy, nx, ny, width * i->multiply);
		fill_solid(dev, color);
                xx = nx;
                yy = ny;
            }
        }
        x = line->x;
        y = line->y;
        line = line->next;
    }
}

static void draw_line(gfxdevice_t*dev, gfxline_t*line)
{
    internal_t*i = (internal_t*)dev->internal;
    double x,y;

    while(line)
    {
        int x1,y1,x2,y2,x3,y3;

        if(line->type == gfx_moveTo) {
        } else if(line->type == gfx_lineTo) {
	    double x1=x,y1=y;
	    double x3=line->x,y3=line->y;
            
            add_line(dev, x1, y1, x3, y3);
        } else if(line->type == gfx_splineTo) {
	    int c,t,parts,qparts;
	    double xx,yy;
            
	    double x1=x,y1=y;
	    double x2=line->sx,y2=line->sy;
	    double x3=line->x,y3=line->y;
            
            c = abs(x3-2*x2+x1) + abs(y3-2*y2+y1);
            xx=x1;
	    yy=y1;

            parts = (int)(sqrt(c)/3);
            if(!parts) parts = 1;

            for(t=1;t<=parts;t++) {
                double nx = (double)(t*t*x3 + 2*t*(parts-t)*x2 + (parts-t)*(parts-t)*x1)/(double)(parts*parts);
                double ny = (double)(t*t*y3 + 2*t*(parts-t)*y2 + (parts-t)*(parts-t)*y1)/(double)(parts*parts);
                
                add_line(dev, xx, yy, nx, ny);
                xx = nx;
                yy = ny;
            }
        }
        x = line->x;
        y = line->y;
        line = line->next;
    }
}

void render_startclip(struct _gfxdevice*dev, gfxline_t*line)
{
    internal_t*i = (internal_t*)dev->internal;
    fillinfo_t info;
    newclip(dev);
    info.type = filltype_clip;
    draw_line(dev, line);
    fill(dev, &info);
}

void render_endclip(struct _gfxdevice*dev)
{
    internal_t*i = (internal_t*)dev->internal;
    endclip(dev);
}

void render_fill(struct _gfxdevice*dev, gfxline_t*line, gfxcolor_t*color)
{
    internal_t*i = (internal_t*)dev->internal;

    draw_line(dev, line);
    fill_solid(dev, color);
}

void render_fillbitmap(struct _gfxdevice*dev, gfxline_t*line, gfximage_t*img, gfxmatrix_t*matrix, gfxcxform_t*cxform)
{
    internal_t*i = (internal_t*)dev->internal;

    gfxcolor_t black = {255,0,0,0};

    draw_line(dev, line);

    fillinfo_t info;
    info.type = filltype_bitmap;
    info.image = img;
    info.matrix = matrix;
    info.cxform = cxform;
    fill(dev, &info);
}

void render_fillgradient(struct _gfxdevice*dev, gfxline_t*line, gfxgradient_t*gradient, gfxgradienttype_t type, gfxmatrix_t*matrix)
{
    internal_t*i = (internal_t*)dev->internal;
    
    gfxcolor_t black = {255,0,0,0};

    draw_line(dev, line);
    fill_solid(dev, &black);
}

void render_addfont(struct _gfxdevice*dev, char*fontid, gfxfont_t*font)
{
    internal_t*i = (internal_t*)dev->internal;
    
    fontlist_t*last=0,*l = i->fontlist;
    while(l) {
	last = l;
	if(!strcmp((char*)l->id, fontid)) {
	    return; // we already know this font
	}
	l = l->next;
    }
    l = (fontlist_t*)rfx_calloc(sizeof(fontlist_t));
    l->font = font;
    l->next = 0;
    if(last) {
	last->next = l;
    } else {
	i->fontlist = l;
    }
}

void render_drawchar(struct _gfxdevice*dev, char*fontid, int glyphnr, gfxcolor_t*color, gfxmatrix_t*matrix)
{
    internal_t*i = (internal_t*)dev->internal;

    if(i->font && i->fontid && !strcmp(fontid, i->fontid)) {
	// current font is correct
    } else {
	fontlist_t*l = i->fontlist;
	i->font = 0;
	i->fontid = 0;
	while(l) {
	    if(!strcmp((char*)l->id, i->fontid)) {
		i->font = l->font;
		i->fontid = l->id;
		break;
	    }
	    l = l->next;
	}
	if(i->font == 0) {
	    fprintf(stderr, "Unknown font id: %s", fontid);
	    return;
	}
    }

    gfxglyph_t*glyph = &i->font->glyphs[glyphnr];
    
    gfxline_t*line2 = gfxline_clone(glyph->line);
    gfxline_transform(line2, matrix);
    draw_line(dev, line2);
    fill_solid(dev, color);
    gfxline_free(line2);
    
    return;
}

void render_result_write(gfxresult_t*r, int filedesc)
{
    internal_result_t*i= (internal_result_t*)r->internal;
}
int  render_result_save(gfxresult_t*r, char*filename)
{
    internal_result_t*i= (internal_result_t*)r->internal;
    if(i->next) {
	int nr=0;
	while(i->next) {
	    writePNG(filename, (unsigned char*)i->img, i->width, i->height);
	    nr++;
	}
    } else {
	writePNG(filename, (unsigned char*)i->img, i->width, i->height);
    }
    return 1;
}
void*render_result_get(gfxresult_t*r, char*name)
{
    internal_result_t*i= (internal_result_t*)r->internal;
    return 0;
}
void render_result_destroy(gfxresult_t*r)
{
    internal_result_t*i= (internal_result_t*)r->internal;
    free(i); r->internal = 0;
    free(r);
}

gfxresult_t* render_finish(struct _gfxdevice*dev)
{
    internal_t*i = (internal_t*)dev->internal;
    
    gfxresult_t* res = (gfxresult_t*)rfx_calloc(sizeof(gfxresult_t));
    
    res->internal = i->results;i->results = 0;
    res->write = render_result_write;
    res->save = render_result_save;
    res->get = render_result_get;
    res->destroy = render_result_destroy;

    free(dev->internal); dev->internal = 0; i = 0;
    return res;
}

void render_startpage(struct _gfxdevice*dev, int width, int height)
{
    internal_t*i = (internal_t*)dev->internal;
    int y;

    if(i->width2 || i->height2) {
	fprintf(stderr, "Error: startpage() called twice (no endpage()?)\n");
	exit(1);
    }
    
    i->width = width;
    i->height = height;
    i->width2 = width*i->antialize*i->multiply;
    i->height2 = height*i->antialize*i->multiply;

    i->lines = (renderline_t*)rfx_alloc(i->height2*sizeof(renderline_t));
    for(y=0;y<i->height2;y++) {
	memset(&i->lines[y], 0, sizeof(renderline_t));
        i->lines[y].points = 0;
        i->lines[y].num = 0;
    }
    i->zbuf = (int*)rfx_calloc(sizeof(int)*i->width2*i->height2);
    i->img = (RGBA*)rfx_calloc(sizeof(RGBA)*i->width2*i->height2);
    i->ymin = 0x7fffffff;
    i->ymax = -0x80000000;

    newclip(dev);
}

void render_endpage(struct _gfxdevice*dev)
{
    internal_t*i = (internal_t*)dev->internal;
    
    if(!i->width2 || !i->height2) {
	fprintf(stderr, "Error: endpage() called without corresponding startpage()\n");
	exit(1);
    }

    endclip(dev);
    while(i->clipbufs) {
	fprintf(stderr, "Warning: unclosed clip while processing endpage()\n");
	endclip(dev);
    }
    
    internal_result_t*ir= (internal_result_t*)rfx_calloc(sizeof(internal_result_t));
    ir->width = i->width;
    ir->height = i->height;
    ir->img = i->img; i->img = 0;
    ir->next = 0;
    if(i->result_next) {
	i->result_next->next = ir;
    }
    if(!i->results) {
	i->results = ir;
    }
    i->result_next = ir;

    rfx_free(i->lines);i->lines=0; //FIXME
    rfx_free(i->zbuf);i->zbuf = 0;
    if(i->img) {rfx_free(i->img);i->img = 0;}

    i->width2 = 0;
    i->height2 = 0;
}

void render_drawlink(struct _gfxdevice*dev, gfxline_t*line, char*action)
{
    /* not supported for this output device */
}

void gfxdevice_render_init(gfxdevice_t*dev)
{
    internal_t*i = (internal_t*)rfx_calloc(sizeof(internal_t));
    int y;
    memset(dev, 0, sizeof(gfxdevice_t));
    dev->internal = i;
    
    i->width = 0;
    i->width2 = 0;
    i->height = 0;
    i->height2 = 0;
    i->antialize = 1;
    i->multiply = 1;

    dev->setparameter = render_setparameter;
    dev->startpage = render_startpage;
    dev->startclip = render_startclip;
    dev->endclip = render_endclip;
    dev->stroke = render_stroke;
    dev->fill = render_fill;
    dev->fillbitmap = render_fillbitmap;
    dev->fillgradient = render_fillgradient;
    dev->addfont = render_addfont;
    dev->drawchar = render_drawchar;
    dev->drawlink = render_drawlink;
    dev->endpage = render_endpage;
    dev->finish = render_finish;
}
