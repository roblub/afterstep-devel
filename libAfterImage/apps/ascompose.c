#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "afterimage.h"
#include "common.h"

#include <X11/Xatom.h> // For XA_PIXMAP.

#define xml_tagchar(a) (isalnum(a) || (a) == '-' || (a) == '_')

// We don't trust the math library to actually provide this number.
#undef PI
#define PI 180

typedef struct xml_elem_t {
	struct xml_elem_t* next;
	struct xml_elem_t* child;
	char* tag;
	char* parm;
} xml_elem_t;

char* load_file(const char* filename);
void showimage(ASImage* im);
ASImage* build_image_from_xml(xml_elem_t* doc, xml_elem_t** rparm);
xml_elem_t* xml_parse_parm(const char* parm);
void xml_print(xml_elem_t* root);
xml_elem_t* xml_elem_new(void);
xml_elem_t* xml_elem_remove(xml_elem_t** list, xml_elem_t* elem);
void xml_elem_delete(xml_elem_t** list, xml_elem_t* elem); 
xml_elem_t* xml_parse_doc(const char* str);
int xml_parse(const char* str, xml_elem_t* current);
void xml_insert(xml_elem_t* parent, xml_elem_t* child);
char* lcstring(char* str);

Pixmap GetRootPixmap (Atom id);

Display* dpy;
int screen, depth;
ASVisual *asv;
int verbose = 0;

char* default_doc_str = "
<composite type=hue>
  <composite type=add>
    <scale width=512 height=384><img src=rose512.jpg/></scale>
    <tile width=512 height=384><img src=back.xpm/></tile>
  </composite>
  <tile width=512 height=384><img src=fore.xpm/></tile>
</composite>
";

void version(void) {
	printf("ascompose version 1.0\n");
}

void usage(void) {
	printf(
		"Usage:\n"
		"ascompose [-h] [-f file] [-s string] [-v] [-V]\n"
		"  -h --help          display this help and exit\n"
		"  -f --file file     an XML file to use as input\n"
		"  -s --string string an XML string to use as input\n"
		"  -v --version       display version and exit\n"
		"  -V --verbose       increase verbosity\n"
	);
}

int main(int argc, char** argv) {
	ASImage* im;
	xml_elem_t* doc;
	char* doc_str = default_doc_str;
	char* doc_file = NULL;
	int i;

	/* see ASView.1 : */
	set_application_name(argv[0]);

	// Parse command line.
	for (i = 1 ; i < argc ; i++) {
		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			version();
			usage();
			exit(0);
		} else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
			version();
			exit(0);
		} else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-V")) {
			verbose++;
		} else if ((!strcmp(argv[i], "--file") || !strcmp(argv[i], "-f")) && i < argc + 1) {
			doc_file = argv[++i];
		} else if ((!strcmp(argv[i], "--string") || !strcmp(argv[i], "-s")) && i < argc + 1) {
			doc_str = argv[++i];
		}
	}

	// Load the document from file, if one was given.
	if (doc_file) {
		doc_str = load_file(doc_file);
		if (!doc_str) {
			fprintf(stderr, "Unable to load file [%s]: %s.\n", doc_file, strerror(errno));
			exit(1);
		}
	}

	dpy = XOpenDisplay(NULL);
	_XA_WM_DELETE_WINDOW = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	screen = DefaultScreen(dpy);
	depth = DefaultDepth(dpy, screen);
	asv = create_asvisual(dpy, screen, depth, NULL);

	doc = xml_parse_doc(doc_str);
	if (verbose > 1) {
		xml_print(doc);
		printf("\n");
	}

	// Do something with the structure!
	im = build_image_from_xml(doc, NULL);
	showimage(im);

#ifdef DEBUG_ALLOCS
	print_unfreed_mem();
#endif

	return 0;
}

char* load_file(const char* filename) {
	struct stat st;
	FILE* fp;
	char* str;
	int len;

	// Get the file size.
	if (stat(filename, &st)) return NULL;

	// Open the file.
	fp = fopen(filename, "rb");
	if (!(fp = fopen(filename, "rb"))) return NULL;

	// Read in the file.
	str = NEW_ARRAY(char, st.st_size + 1);
	len = fread(str, 1, st.st_size, fp);
	if (len >= 0) str[len] = '\0';

	fclose(fp);

	return str;
}

void showimage(ASImage* im) {
	if( im != NULL )
	{
		/* see ASView.4 : */
		Window w = create_top_level_window( asv, DefaultRootWindow(dpy), 32, 32,
			                         im->width, im->height, 1, 0, NULL,
									 "ASView" );
		if( w != None )
		{
			Pixmap p ;

			XSelectInput (dpy, w, (StructureNotifyMask | ButtonPress));
	  		XMapRaised   (dpy, w);
			/* see ASView.5 : */
			p = asimage2pixmap( asv, DefaultRootWindow(dpy), im, NULL,
				                False );
			destroy_asimage( &im );
			/* see common.c:set_window_background_and_free(): */
			p = set_window_background_and_free( w, p );
		}
		/* see ASView.6 : */
	    while(w != None)
  		{
    		XEvent event ;
	        XNextEvent (dpy, &event);
  		    switch(event.type)
			{
		  		case ButtonPress:
					break ;
	  		    case ClientMessage:
			        if ((event.xclient.format == 32) &&
	  			        (event.xclient.data.l[0] == _XA_WM_DELETE_WINDOW))
		  			{
						XDestroyWindow( dpy, w );
						XFlush( dpy );
						w = None ;
				    }
					break;
			}
  		}
	}

    if( dpy )
        XCloseDisplay (dpy);
}

// Each tag is only allowed to return ONE image.
ASImage* build_image_from_xml(xml_elem_t* doc, xml_elem_t** rparm) {
	xml_elem_t* ptr;
	ASImage* result = NULL;

	if (!strcmp(doc->tag, "img")) {
		xml_elem_t* parm = xml_parse_parm(doc->parm);
		xml_elem_t* src;
		for (src = parm ; src && strcmp(src->tag, "src") ; src = src->next);
		if (!strcmp(src->parm, "xroot:")) {
			int width, height;
			Pixmap rp = GetRootPixmap(None);
			if (verbose) printf("Getting root pixmap.\n");
			if (rp) {
				get_drawable_size(rp, &width, &height);
				result = pixmap2asimage(asv, rp, 0, 0, width, height, 0xFFFFFFFF, False, 100);
			}
		} else if (src) {
			if (verbose) printf("Loading image [%s].\n", src->parm);
			result = file2ASImage(src->parm, 0xFFFFFFFF, SCREEN_GAMMA, 100, NULL);
		}
		if (rparm) *rparm = parm; else xml_elem_delete(NULL, parm);
	}

	if (!strcmp(doc->tag, "gradient")) {
		xml_elem_t* parm = xml_parse_parm(doc->parm);
		int width = 0, height = 0;
		double angle = 0;
		char* color_str = NULL;
		char* offset_str = NULL;
		for (ptr = parm ; ptr ; ptr = ptr->next) {
			if (!strcmp(ptr->tag, "width")) width = strtol(ptr->parm, NULL, 0);
			if (!strcmp(ptr->tag, "height")) height = strtol(ptr->parm, NULL, 0);
			if (!strcmp(ptr->tag, "angle")) angle = strtod(ptr->parm, NULL);
			if (!strcmp(ptr->tag, "colors")) color_str = ptr->parm;
			if (!strcmp(ptr->tag, "offsets")) offset_str = ptr->parm;
		}
		if (width && height && color_str) {
			ASGradient gradient;
			int reverse = 0, npoints1 = 0, npoints2 = 0;
			char* p;
			angle = fmod(angle, 2 * PI);
			if (angle > 2 * PI * 15 / 16 || angle < 2 * PI * 1 / 16) {
				gradient.type = GRADIENT_Left2Right;
			} else if (angle < 2 * PI * 3 / 16) {
				gradient.type = GRADIENT_TopLeft2BottomRight;
			} else if (angle < 2 * PI * 5 / 16) {
				gradient.type = GRADIENT_Top2Bottom;
			} else if (angle < 2 * PI * 7 / 16) {
				gradient.type = GRADIENT_BottomLeft2TopRight; reverse = 1;
			} else if (angle < 2 * PI * 9 / 16) {
				gradient.type = GRADIENT_Left2Right; reverse = 1;
			} else if (angle < 2 * PI * 11 / 16) {
				gradient.type = GRADIENT_TopLeft2BottomRight; reverse = 1;
			} else if (angle < 2 * PI * 13 / 16) {
				gradient.type = GRADIENT_Top2Bottom; reverse = 1;
			} else {
				gradient.type = GRADIENT_BottomLeft2TopRight;
			}
			for (p = color_str ; isspace(*p) ; p++);
			for (npoints1 = 0 ; *p ; npoints1++) {
				if (*p) for ( ; *p && !isspace(*p) ; p++);
				for ( ; isspace(*p) ; p++);
			}
			if (offset_str) {
				for (p = offset_str ; isspace(*p) ; p++);
				for (npoints2 = 0 ; *p ; npoints2++) {
					if (*p) for ( ; *p && !isspace(*p) ; p++);
					for ( ; isspace(*p) ; p++);
				}
			}
			if (npoints1 > 1) {
				int i;
				if (offset_str && npoints1 > npoints2) npoints1 = npoints2;
				gradient.color = NEW_ARRAY(ARGB32, npoints1);
				gradient.offset = NEW_ARRAY(double, npoints1);
				for (p = color_str ; isspace(*p) ; p++);
				for (npoints1 = 0 ; *p ; ) {
					char* pb = p, ch;
					if (*p) for ( ; *p && !isspace(*p) ; p++);
					for ( ; isspace(*p) ; p++);
					ch = *p; *p = '\0';
					if (parse_argb_color(pb, gradient.color + npoints1)) npoints1++;
					*p = ch;
				}
				if (offset_str) {
					for (p = offset_str ; isspace(*p) ; p++);
					for (npoints2 = 0 ; *p ; ) {
						char* pb = p, ch;
						if (*p) for ( ; *p && !isspace(*p) ; p++);
						ch = *p; *p = '\0';
						gradient.offset[npoints2] = strtod(pb, &pb);
						if (pb == p) npoints2++;
						*p = ch;
						for ( ; isspace(*p) ; p++);
					}
				} else {
					for (npoints2 = 0 ; npoints2 < npoints1 ; npoints2++)
						gradient.offset[npoints2] = (double)npoints2 / (npoints1 - 1);
				}
				gradient.npoints = npoints1;
				if (npoints2 && gradient.npoints > npoints2)
					gradient.npoints = npoints2;
				if (reverse) {
					for (i = 0 ; i < gradient.npoints / 2 ; i++) {
						int i2 = gradient.npoints - 1 - i;
						ARGB32 c = gradient.color[i];
						double o = gradient.offset[i];
						gradient.color[i] = gradient.color[i2];
						gradient.color[i2] = c;
						gradient.offset[i] = gradient.offset[i2];
						gradient.offset[i2] = o;
					}
					for (i = 0 ; i < gradient.npoints ; i++) {
						gradient.offset[i] = 1.0 - gradient.offset[i];
					}
				}
				if (verbose) printf("Generating [%dx%d] gradient with angle [%f], colors [%s], offsets [%s], and npoints [%d/%d].\n", width, height, angle, color_str, offset_str ? offset_str : "(none given)", npoints1, npoints2);
				if (verbose > 1) {
					for (i = 0 ; i < gradient.npoints ; i++) {
						printf("  Point [%d] has color [#%08x] and offset [%f].\n", i, (unsigned int)gradient.color[i], gradient.offset[i]);
					}
				}
				result = make_gradient(asv, &gradient, width, height, 0xFFFFFFFF, ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT);
			}
		}
		if (rparm) *rparm = parm; else xml_elem_delete(NULL, parm);
	}

	if (!strcmp(doc->tag, "mirror")) {
		xml_elem_t* parm = xml_parse_parm(doc->parm);
		ASImage* imtmp = NULL;
		int dir = 0;
		for (ptr = parm ; ptr ; ptr = ptr->next) {
			if (!strcmp(ptr->tag, "dir")) dir = !mystrcasecmp(ptr->parm, "vertical");
		}
		for (ptr = doc->child ; ptr && !imtmp ; ptr = ptr->next) {
			imtmp = build_image_from_xml(ptr, NULL);
		}
		if (imtmp) {
printf("Mirror.\n");
			result = mirror_asimage(asv, imtmp, 0, 0, imtmp->width, imtmp->height, dir, ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT);
if (!result) printf("Mirror failed.\n");
			destroy_asimage(&imtmp);
		}
		if (verbose) printf("Mirroring image [%sally].\n", dir ? "horizont" : "vertic");
		if (rparm) *rparm = parm; else xml_elem_delete(NULL, parm);
	}

	if (!strcmp(doc->tag, "rotate")) {
		xml_elem_t* parm = xml_parse_parm(doc->parm);
		ASImage* imtmp = NULL;
		double angle = 0;
		for (ptr = parm ; ptr ; ptr = ptr->next) {
			if (!strcmp(ptr->tag, "angle")) angle = strtod(ptr->parm, NULL);
		}
		for (ptr = doc->child ; ptr && !imtmp ; ptr = ptr->next) {
			imtmp = build_image_from_xml(ptr, NULL);
		}
		if (imtmp) {
			int dir = 0;
			angle = fmod(angle, 2 * PI);
			if (angle > 2 * PI * 7 / 8 || angle < 2 * PI * 1 / 8) {
				dir = 0;
			} else if (angle < 2 * PI * 3 / 8) {
				dir = FLIP_VERTICAL;
			} else if (angle < 2 * PI * 5 / 8) {
				dir = FLIP_UPSIDEDOWN;
			} else {
				dir = FLIP_VERTICAL | FLIP_UPSIDEDOWN;
			}
			if (dir) {
				result = flip_asimage(asv, imtmp, 0, 0, imtmp->width, imtmp->height, dir, ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT);
				destroy_asimage(&imtmp);
				if (verbose) printf("Rotating image [%f degrees].\n", angle);
			} else {
				result = imtmp;
			}
		}
		if (rparm) *rparm = parm; else xml_elem_delete(NULL, parm);
	}

	if (!strcmp(doc->tag, "scale")) {
		xml_elem_t* parm = xml_parse_parm(doc->parm);
		int width = 0, height = 0;
		for (ptr = parm ; ptr ; ptr = ptr->next) {
			if (!strcmp(ptr->tag, "width")) width = strtol(ptr->parm, NULL, 0);
			if (!strcmp(ptr->tag, "height")) height = strtol(ptr->parm, NULL, 0);
		}
		if (width && height) {
			ASImage* imtmp = NULL;
			for (ptr = doc->child ; ptr && !imtmp ; ptr = ptr->next) {
				imtmp = build_image_from_xml(ptr, NULL);
			}
			if (imtmp) {
				result = scale_asimage(asv, imtmp, width, height, ASA_ASImage, 100, ASIMAGE_QUALITY_DEFAULT);
				destroy_asimage(&imtmp);
			}
			if (verbose) printf("Scaling image to [%dx%d].\n", width, height);
		}
		if (rparm) *rparm = parm; else xml_elem_delete(NULL, parm);
	}

	if (!strcmp(doc->tag, "tile")) {
		xml_elem_t* parm = xml_parse_parm(doc->parm);
		int width = 0, height = 0;
		for (ptr = parm ; ptr ; ptr = ptr->next) {
			if (!strcmp(ptr->tag, "width")) width = strtol(ptr->parm, NULL, 0);
			if (!strcmp(ptr->tag, "height")) height = strtol(ptr->parm, NULL, 0);
		}
		if (width && height) {
			ASImage* imtmp = NULL;
			for (ptr = doc->child ; ptr && !imtmp ; ptr = ptr->next) {
				imtmp = build_image_from_xml(ptr, NULL);
			}
			if (imtmp) {
				result = tile_asimage(asv, imtmp, 0, 0, width, height, 0, ASA_ASImage, 100, ASIMAGE_QUALITY_TOP);
				destroy_asimage(&imtmp);
			}
			if (verbose) printf("Tiling image to [%dx%d].\n", width, height);
		}
		if (rparm) *rparm = parm; else xml_elem_delete(NULL, parm);
	}

	if (!strcmp(doc->tag, "composite")) {
		xml_elem_t* parm = xml_parse_parm(doc->parm);
		char* pop = NULL;
		int num;
		for (ptr = parm ; ptr ; ptr = ptr->next) {
			if (!strcmp(ptr->tag, "op")) pop = ptr->parm;
		}
		if (pop) {
			// Find out how many subimages we have.
			num = 0;
			for (ptr = doc->child ; ptr ; ptr = ptr->next) {
				if (strcmp(ptr->tag, "CDATA")) num++;
			}
		}
		if (pop && num) {
			int x = 0, y = 0, width = 0, height = 0;
			ASImageLayer *layers;

			// Build the layers first.
			layers = NEW_ARRAY(ASImageLayer, num);
			for (num = 0, ptr = doc->child ; ptr ; ptr = ptr->next) {
				xml_elem_t* sparm = NULL;
				if (!strcmp(ptr->tag, "CDATA")) continue;
				layers[num].im = build_image_from_xml(ptr, &sparm);
				if (sparm) {
					xml_elem_t* tmp;
					for (tmp = sparm ; tmp ; tmp = tmp->next) {
						if (!strcmp(tmp->tag, "x")) x = strtol(tmp->parm, NULL, 0);
						if (!strcmp(tmp->tag, "y")) y = strtol(tmp->parm, NULL, 0);
					}
				}
				if (layers[num].im) {
					layers[num].dst_x = x;
					layers[num].dst_y = y;
					layers[num].clip_x = 0;
					layers[num].clip_y = 0;
					layers[num].clip_width = layers[num].im->width;
					layers[num].clip_height = layers[num].im->height;
					layers[num].tint = 0;
					layers[num].bevel = 0;
					layers[num].merge_mode = 0;
					layers[num].merge_scanlines = blend_scanlines_name2func(pop);
					if (width < layers[num].im->width) width = layers[num].im->width;
					if (height < layers[num].im->height) height = layers[num].im->height;
					num++;
				}
				if (sparm) xml_elem_delete(NULL, sparm);
			}

			if (verbose) printf("Compositing [%d] image(s) with op [%s].\n", num, pop);

			result = merge_layers(asv, layers, num, width, height, ASA_ASImage, 0, ASIMAGE_QUALITY_DEFAULT);
			while (--num >= 0) destroy_asimage(&layers[num].im);
			free(layers);
		}
		if (rparm) *rparm = parm; else xml_elem_delete(NULL, parm);
	}

	// No match so far... see if one of our children can do any better.
	if (!result) {
		for (ptr = doc->child ; ptr && !result ; ptr = ptr->next) {
			xml_elem_t* sparm = NULL;
			result = build_image_from_xml(ptr, &sparm);
			if (result && rparm) *rparm = sparm; else xml_elem_delete(NULL, sparm);
		}
	}

	return result;
}

xml_elem_t* xml_parse_parm(const char* parm) {
	xml_elem_t* list = NULL;
	const char* eparm;

	if (!parm) return NULL;

	for (eparm = parm ; *eparm ; ) {
		xml_elem_t* p;
		const char* bname;
		const char* ename;
		const char* bval;
		const char* eval;

		// Spin past any leading whitespace.
		for (bname = eparm ; isspace(*bname) ; bname++);

		// Check for a parm.  First is the parm name.
		for (ename = bname ; xml_tagchar(*ename) ; ename++);

		// No name equals no parm equals broken tag.
		if (!*ename) { eparm = NULL; break; }
		
		// No "=" equals broken tag.  We do not support HTML-style parms 
		// with no value.
		for (bval = ename ; isspace(*bval) ; bval++);
		if (*bval != '=') { eparm = NULL; break; }

		while (isspace(*++bval));

		// If the next character is a quote, spin until we see another one.
		if (*bval == '"' || *bval == '\'') {
			char quote = *bval;
			bval++;
			for (eval = bval ; *eval && *eval != quote ; eval++);
		} else {
			for (eval = bval ; *eval && !isspace(*eval) ; eval++);
		}

		for (eparm = eval ; *eparm && !isspace(*eparm) ; eparm++);

		// Add the parm to our list.
		p = xml_elem_new();
		if (!list) list = p;
		else { p->next = list; list = p; }
		p->tag = lcstring(mystrndup(bname, ename - bname));
		p->parm = mystrndup(bval, eval - bval);
	}

	if (!eparm) {
		while (list) {
			xml_elem_t* p = list->next;
			free(list->tag);
			free(list->parm);
			free(list);
			list = p;
		}
	}

	return list;
}

void xml_print(xml_elem_t* root) {
	xml_elem_t* child;
	if (!strcmp(root->tag, "CDATA")) {
		printf("%s", root->parm);
	} else {
		printf("<%s", root->tag);
		if (root->parm) {
			xml_elem_t* parm = xml_parse_parm(root->parm);
			while (parm) {
				xml_elem_t* p = parm->next;
				printf(" %s=\"%s\"", parm->tag, parm->parm);
				free(parm->tag);
				free(parm->parm);
				free(parm);
				parm = p;
			}
		}
		printf(">");
		for (child = root->child ; child ; child = child->next) xml_print(child);
		printf("</%s>", root->tag);
	}
}

xml_elem_t* xml_elem_new(void) {
	xml_elem_t* elem = NEW(xml_elem_t);
	elem->next = elem->child = NULL;
	elem->parm = elem->tag = NULL;
	return elem;
}

xml_elem_t* xml_elem_remove(xml_elem_t** list, xml_elem_t* elem) {
	// Splice the element out of the list, if it's in one.
	if (list) {
		if (*list == elem) {
			*list = elem->next;
		} else {
			xml_elem_t* ptr;
			for (ptr = *list ; ptr->next ; ptr = ptr->next) {
				if (ptr->next == elem) {
					ptr->next = elem->next;
					break;
				}
			}
		}
	}
	elem->next = NULL;
	return elem;
}

void xml_elem_delete(xml_elem_t** list, xml_elem_t* elem) {
	if (list) xml_elem_remove(list, elem);
	while (elem) {
		xml_elem_t* ptr = elem;
		elem = elem->next;
		if (ptr->child) xml_elem_delete(NULL, ptr->child);
		free(ptr->tag);
		free(ptr->parm);
		free(ptr);
	}
}

xml_elem_t* xml_parse_doc(const char* str) {
	xml_elem_t* elem = xml_elem_new();
	elem->tag = "CONTAINER";
	xml_parse(str, elem);
	return elem;
}

int xml_parse(const char* str, xml_elem_t* current) {
	const char* ptr = str;

	// Find a tag of the form <tag opts>, </tag>, or <tag opts/>.
	while (*ptr) {
		const char* oab = ptr;

		// Look for an open oab bracket.
		for (oab = ptr ; *oab && *oab != '<' ; oab++);

		// If there are no oab brackets left, we're done.
		if (*oab != '<') return oab - str;

		// Does this look like a close tag?
		if (oab[1] == '/') {
			const char* etag;
			// Find the end of the tag.
			for (etag = oab + 2 ; xml_tagchar(*etag) ; etag++);

			// If this is an end tag, and the tag matches the tag we're parsing, 
			// we're done.  If not, continue on blindly.
			if (*etag == '>') {
				if (!strncasecmp(oab + 2, current->tag, etag - (oab + 2))) {
					if (oab - ptr) {
						xml_elem_t* child = xml_elem_new();
						child->tag = "CDATA";
						child->parm = mystrndup(ptr, oab - ptr);
						xml_insert(current, child);
					}
					return (etag + 1) - str;
				}
			}

			// This tag isn't interesting after all.
			ptr = oab + 1;
		}

		// Does this look like a start tag?
		if (oab[1] != '/') {
			int empty = 0;
			const char* btag = oab + 1;
			const char* etag;
			const char* bparm;
			const char* eparm;

			// Find the end of the tag.
			for (etag = btag ; xml_tagchar(*etag) ; etag++);

			// If we reached the end of the document, continue on.
			if (!*etag) { ptr = oab + 1; continue; }

			// Find the beginning of the parameters, if they exist.
			for (bparm = etag ; isspace(*bparm) ; bparm++);
			
			// From here on, we're looking for a sequence of parms, which have 
			// the form [a-z0-9-]+=("[^"]"|'[^']'|[^ \t\n]), followed by either 
			// a ">" or a "/>".
			for (eparm = bparm ; *eparm ; ) {
				const char* tmp;

				// Spin past any leading whitespace.
				for ( ; isspace(*eparm) ; eparm++);

				// Are we at the end of the tag?
				if (*eparm == '>' || (*eparm == '/' && eparm[1] == '>')) break;

				// Check for a parm.  First is the parm name.
				for (tmp = eparm ; xml_tagchar(*tmp) ; tmp++);

				// No name equals no parm equals broken tag.
				if (!*tmp) { eparm = NULL; break; }
				
				// No "=" equals broken tag.  We do not support HTML-style parms 
				// with no value.
				for ( ; isspace(*tmp) ; tmp++);
				if (*tmp != '=') { eparm = NULL; break; }

				while (isspace(*++tmp));

				// If the next character is a quote, spin until we see another one.
				if (*tmp == '"' || *tmp == '\'') {
					char quote = *tmp;
					for (tmp++ ; *tmp && *tmp != quote ; tmp++);
				}

				// Now look for a space or the end of the tag.
				for ( ; *tmp && !isspace(*tmp) && *tmp != '>' && !(*tmp == '/' && tmp[1] == '>') ; tmp++);
				
				// If we reach the end of the string, there cannot be a '>'.
				if (!*tmp) { eparm = NULL; break; }
				
				// End of the parm.
				if (!isspace(*tmp)) { eparm = tmp; break; }

				eparm = tmp;
			}

			// If eparm is NULL, the parm string is invalid, and we should 
			// abort processing.
			if (!eparm) { ptr = oab + 1; continue; }

			// Save CDATA, if there is any.
			if (oab - ptr) {
				xml_elem_t* child = xml_elem_new();
				child->tag = "CDATA";
				child->parm = mystrndup(ptr, oab - ptr);
				xml_insert(current, child);
			}

			// We found a tag!  Advance the pointer.
			for (ptr = eparm ; isspace(*ptr) ; ptr++);
			empty = (*ptr == '/');
			ptr += empty + 1;

			// Add the tag to our children and parse it.
			{
				xml_elem_t* child = xml_elem_new();
				child->tag = lcstring(mystrndup(btag, etag - btag));
				if (eparm - bparm) child->parm = mystrndup(bparm, eparm - bparm);
				xml_insert(current, child);
				if (!empty) ptr += xml_parse(ptr, child);
			}
		}
	}
	return ptr - str;
}

void xml_insert(xml_elem_t* parent, xml_elem_t* child) {
	child->next = NULL;
	if (!parent->child) {
		parent->child = child;
		return;
	}
	for (parent = parent->child ; parent->next ; parent = parent->next);
	parent->next = child;
}

char* lcstring(char* str) {
	char* ptr = str;
	for ( ; *ptr ; ptr++) if (isupper(*ptr)) *ptr = tolower(*ptr);
	return str;
}

/* Stolen from libAfterStep. */
Pixmap GetRootPixmap (Atom id)
{
	Pixmap        currentRootPixmap = None;

	if (id == None)
		id = XInternAtom (dpy, "_XROOTPMAP_ID", True);

	if (id != None)
	{
		Atom          act_type;
		int           act_format;
		unsigned long nitems, bytes_after;
		unsigned char *prop = NULL;

/*fprintf(stderr, "\n aterm GetRootPixmap(): root pixmap is set");                  */
		if (XGetWindowProperty (dpy, RootWindow(dpy, screen), id, 0, 1, False, XA_PIXMAP,
								&act_type, &act_format, &nitems, &bytes_after, &prop) == Success)
		{
			if (prop)
			{
				currentRootPixmap = *((Pixmap *) prop);
				XFree (prop);
/*fprintf(stderr, "\n aterm GetRootPixmap(): root pixmap is [%lu]", currentRootPixmap); */
			}
		}
	}
	return currentRootPixmap;
}
