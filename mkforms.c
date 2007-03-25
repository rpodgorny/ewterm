/* Convert EWSD forms from Siemens' messy binary format to nice C-like text
 * files. */
/* Copyright (c) 2001, 2002 Petr Baudis <pasky@ji.cz> */
/* This program is GPL'ed. */

/* This file is ugly, yes. It's only a quick hack anyway. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define FORMAT /* if not defined, show rather own debugging output of the input file, not rc-format file */

static char fl[655350];

struct {
/*   0 */ uint32_t      ofs_st_ar51;    /* offset of start of area51 (first upper node name comes) [99%] */
/*   4 */ uint32_t      ofs_st_data;    /* offset of start of data [CONFIRMED] */
/*   8 */ uint32_t      ofs_st_strs;    /* offset of start of strings (first node name comes) [CONFIRMED] */

/*   c */ uint32_t      ofs_st_misc;    /* offset of start of misc record, if any, otherwise a nonsense */
/*  10 */ uint32_t      ofs_st_hrc1;    /* offset of start of helprec1, if any, otherwise as [c] */
/*  14 */ uint32_t      ofs_st_hrc2;    /* offset of start of helprec2, if any, otherwise as [c] */
/*  18 */ uint32_t      ofs_st_hrc3;    /* offset of  end  of helprec2, if any, otherwise as [c] */

#define	FILE_MENU	0x4
#define	FILE_MENU2	0x2
#define	FILE_CMD	0x1
/*  1c */ uint32_t      type;           /* 4 in menu, 1 in cmd */

/*  20 */ uint32_t      u9;             /* zero */
/*  24 */ uint32_t      u10;            /* zero */
/*  28 */ uint32_t      u11;            /* zero */
/*  2c */ uint32_t      u12;            /* zero */
/*  30 */ uint32_t      u13;            /* zero */
} hdr;

struct {
/*   0 */ uint32_t      len_area51;     /* length of area51, [ofs_st_ar51 + len_area51]=str_unode [?] */

/*   4 */ uint16_t      u15;      /* 5 */
/*   6 */ uint16_t      u16;      /* 1 */
/*   8 */ uint16_t      u17;      /* 1 */
/*   a */ uint16_t      u18;      /* 0 */
} a51;

#if 0
/*   c */ uint8_t       *unknown19;     /* zero zero .. (non-fixlen) */

/* it's unclear if this belongs to area51 ? it seems it doesn't */

/*  1c */ uint8_t       *str_unode;     /* upper node name (non-fixlen str) */
#endif

struct {
/*   0 */ uint16_t      u1;      /* x37 (not offset) */
/*   2 */ uint16_t      xsize;   /* x50 (== 80 == xsize?) (n/o) */
/*   4 */ uint16_t      fields;  /* number of strdescs */
/*   6 */ uint16_t      u4;      /* x19 (== 25 == fieldno?) (n/o) */
/*   8 */ uint16_t      u5;      /* x5211 (== 21009 == ???) (n/o) */
/*   a */ uint16_t	u6;
/*   c */ uint16_t      u7;      /* 1 */
/*   e */ uint16_t	u8;
/*   0 */ uint32_t      u9;      /* 0 */
} strhead;

struct {
/*   0 */ uint32_t      ofs_st_str;     /* ofs_st_strs + ofs_st_str == addr of string */
/*   4 */ uint16_t      str_len;	/* length of string */
/*   6 */ uint16_t      str_y;          /* Y coord of string (pos of 0 varies!) */
/*   8 */ uint16_t      str_x;          /* X coord of string */
/*   a */ uint16_t      page;		/* string appears on 'page'.. */
/*   c */ uint16_t      flags;          /* (input) 10 == required ; rest is unknown */

#define	DESC_NODWHAT	0x0		/* CORRECT TIME */
#define	DESC_WHAT	0x1		/* SECOND */
#define	DESC_IDX	0x2		/*   2 */
#define	DESC_SEP	0x3		/* ------ */
#define	DESC_NODMENU	0x4		/* CRPBX */
#define	DESC_LINK	0x5		/* IMDD */
#define	DESC_NODE	0x24		/* CORR TIME: */
#define	DESC_VAR	0x25		/* SEC */
#define	DESC_INP	0x26		/* <81><81> */
#define	DESC_COL	0x27		/* , */
/*   e */ uint16_t      type;		/* type of string */

/*  10 */ uint32_t      u8;      /* 0 usually */
} strdesc;

struct {
/*   0 */ uint32_t	hdrlen;
/*   4 */ uint32_t	ofs_st_subgroups;
/*   8 */ uint32_t	ofs_st_recs; /* this is the good one */
/*   c */ uint32_t	ofs_st_names;
/*  10 */ uint32_t	ofs_st_descs;
/*  14 */ uint16_t	groups;
/*  16 */ uint16_t	u5;
/*  18 */ uint32_t	u6;
} stropthdr;

struct {
/*   0 */ uint32_t	u1; /* ofs ??? */
/*   4 */ uint32_t	ofs_st_optdesc;
/*   8 */ uint16_t	id;
/*   a */ uint16_t	pseudosubgroup; /* just what refers subgroup link to; ff is subgroup */
/*   c */ uint16_t	opts;
/*   e */ uint16_t	type; /* 0 is direct, 1 is subgroup */
/*  10 */ uint32_t	u6;
} stroptgroupdesc;

struct {
/*   0 */ uint32_t	ofs_st_recs;
/*   4 */ uint16_t	id;
/*   6 */ uint16_t	count;
} stroptsubgroupdesc;

struct {
/*   0 */ uint32_t	ofs_st_name;
/*   4 */ uint32_t	ofs_st_desc;
/*   c */ uint32_t	ofs_st_longdesc;
/*  10 */ uint32_t	u4;
/*  14 */ uint32_t	u5;
} stroptdesc;

struct {
/*   0 */ uint16_t	hdrlen;
/*   2 */ uint16_t	u0;
/*   4 */ uint16_t	ofs;
/*   6 */ uint16_t	u1;
/*   8 */ uint16_t	u2;
/*   a */ uint16_t	u3;
/*   c */ uint16_t	u4;
/*   e */ uint16_t	u5;
/*  10 */ uint16_t	u6;
/*  12 */ uint16_t	u7;
/*  14 */ uint16_t	u8;
/*  16 */ uint16_t	u9;
/*  18 */ uint16_t	u10;
/*  1a */ uint16_t	u11;
/*  1c */ uint16_t	u12;
/*  1e */ uint16_t	u13;
/*  20 */ uint16_t	u14;
/*  22 */ uint16_t	u15;
/*  24 */ uint16_t	u16;
/*  26 */ uint16_t	u17;
} strhelphdr;

struct {
/*   0 */ uint16_t	u1;
/*   2 */ uint16_t	u2;
/*   4 */ uint16_t	ofs_st_str;	/* ofs_st_strs + ofs_st_str == addr of string */
/*   6 */ uint16_t	suboptgroup;	/* 0 != link */
} strhelpdesc;

void
load_optdesc()
{
#ifndef FORMAT
  printf("\n ");
  printf("(x%x) %s ", stroptdesc.ofs_st_name, fl+hdr.ofs_st_hrc1+stropthdr.ofs_st_names+stroptdesc.ofs_st_name);
  printf("(x%x) %s ", stroptdesc.ofs_st_desc, fl+hdr.ofs_st_hrc1+stropthdr.ofs_st_descs+stroptdesc.ofs_st_desc);
  if (stroptdesc.ofs_st_longdesc)
    printf("(x%x) %s ", stroptdesc.ofs_st_longdesc, fl+hdr.ofs_st_hrc1+stropthdr.ofs_st_descs+stroptdesc.ofs_st_longdesc);

  printf(":: %4x ", stroptdesc.u4);
  printf(":: %4x ", stroptdesc.u5);
#else
  printf("\t\t\toption {\n");
  printf("\t\t\t\tname = \"%s\";\n\t\t\t\tdesc = \"%s\";\n", fl + hdr.ofs_st_hrc1 + stropthdr.ofs_st_names + stroptdesc.ofs_st_name, fl + hdr.ofs_st_hrc1 + stropthdr.ofs_st_descs + stroptdesc.ofs_st_desc);
  if (stroptdesc.ofs_st_longdesc)
    printf("\t\t\t\tlongdesc = \"%s\";\n", fl + hdr.ofs_st_hrc1 + stropthdr.ofs_st_descs + stroptdesc.ofs_st_longdesc);
  printf("\t\t\t}\n");
#endif
}

int
main(int argc, char *argv[]) {
  FILE *f;
  
  if (argc < 2) { fprintf(stderr, "Usage: %s <filename>\n", argv[0]); exit(1); }
  f = fopen(argv[1], "rb");
  if (!f) { fprintf(stderr, "Cannot open file %s, sorry.\n", argv[1]); exit(1); }
  
  fread(&fl, 655350, 1, f);
  fclose(f);
  
  memcpy(&hdr, fl, sizeof(hdr));

#ifndef FORMAT
  printf("area51 offset:\t%x\ndata offset:\t%x\nstrings offset:\t%x\n", hdr.ofs_st_ar51, hdr.ofs_st_data, hdr.ofs_st_strs);
  printf("misc offset:\t%x\n", hdr.ofs_st_misc);
  printf("helprec1 offset:\t%x\n", hdr.ofs_st_hrc1);
  printf("helprec2 offset:\t%x\n", hdr.ofs_st_hrc2);
  printf("helprec3 ofsend:\t%x\n", hdr.ofs_st_hrc3);
  printf("node type:\t");
#else
  printf("\n###\n\nnode {\n\tname = \"%s\";\n\ttype = ", argv[1]);
#endif
  switch (hdr.type) {
    case FILE_MENU: printf("menu"); break;
    case FILE_MENU2: printf("menu"); break;
    case FILE_CMD: printf("command"); break;
    default: printf("%x", hdr.type); break;
  }
#ifndef FORMAT
  printf("\n");
  printf("rest:\t%x %x %x %x %x\n", hdr.u9, hdr.u10, hdr.u11, hdr.u12, hdr.u13);
#else
  printf(";\n");
#endif
  
  printf("\n");

  memcpy(&a51, fl+hdr.ofs_st_ar51, sizeof(a51));
  
#ifndef FORMAT
  printf("[area51]\nlength:\t%x\nrest:\t%x %x %x %x\nparent:\t%s\n", a51.len_area51, a51.u15, a51.u16, a51.u17, a51.u18, fl+hdr.ofs_st_ar51+a51.len_area51);
  {
    int c;
    printf("fill:\t");
    for(c=12;c<a51.len_area51;c++)printf("%x ", (uint8_t)*(fl+hdr.ofs_st_ar51+c));
    printf("\n");
  }

  printf("\n");
#else
  printf("\tparent = \"%s\";\n", fl+hdr.ofs_st_ar51+a51.len_area51);
#endif

  if (hdr.type & FILE_CMD) {
#ifndef FORMAT
    printf("misc: %x :: %s :: %x\n", (uint16_t)*(fl+hdr.ofs_st_misc), fl+hdr.ofs_st_misc+2, (uint16_t)*(fl+hdr.ofs_st_misc+8));

    printf("\n");
#else
    printf("\tgroup = \"%s\";\n", fl+hdr.ofs_st_misc+2);
#endif
  }

  memcpy(&strhead, fl+hdr.ofs_st_data, sizeof(strhead));

#ifndef FORMAT
  printf("[strhead]\n%x (. .) %x %x %x %x %x %d\n", strhead.u1, strhead.u4, strhead.u5, strhead.u6, strhead.u7, strhead.u8, strhead.u9);
  printf("width:\t%d\n", strhead.xsize);
  printf("fields:\t%d\n", strhead.fields);
#else
  printf("\twidth = %d;\n\tfields = %d;\n", strhead.xsize, strhead.fields);
  printf("\tline {\n\t\tno = 0;\n");
#endif

  printf("\n");
  {
    int c;
    int lpage = 0;
    for (c=0;c<strhead.fields;c++) {
      memcpy(&strdesc, fl+hdr.ofs_st_data+(c+1)*sizeof(strdesc), sizeof(strdesc));
#ifndef FORMAT
      printf("\n[%d] (x%x) (%d)\n%s\nx %2d :: y %2d :: page %d :: flags %x :: type ", c, strdesc.ofs_st_str, strdesc.str_len,
	  fl+strdesc.ofs_st_str+hdr.ofs_st_strs, strdesc.str_x, strdesc.str_y,
	  strdesc.page, strdesc.flags);
#else
      {
	char *str = fl+strdesc.ofs_st_str+hdr.ofs_st_strs;
	if (lpage != strdesc.page) { printf("\t}\n\tline {\n\t\tno = %d;\n", strdesc.page); lpage = strdesc.page; }
	printf("\t\tfield {\n");
	if (strdesc.type == DESC_VAR) {
	  char *str2 = strdup(str);
	  *strchr(str2, '=') = 0;
	  if (strchr(str2, ' ')) *strchr(str2, ' ') = 0;
	  printf("\t\t\toptname = \"%s\";\n", str2);
	} else if (strdesc.type == DESC_LINK) {
	  /* Code dup. with help.c! */
	  char *str2 = strdup(str);
	  char *str2p;
	  while ((str2p = strchr(str2, ' '))) {
	    if (str2p > str2 && str2p[-1] == '-')
	      *str2p = '/';
	    else
	      memmove(str2p, str2p + 1, strlen(str2p));
	  }
	  printf("\t\t\ttarget = \"%s\";\n", str2);
	}
	if (strdesc.flags & 0x10) printf("\t\t\tflags = required;\n");
	printf("\t\t\tno = %d;\n\t\t\tstr = \"%s\";\n\t\t\tx = %d;\n\t\t\ty = %d;\n\t\t\tpage = %d;\n\t\t\ttype = ",
	    c, str, strdesc.str_x, strdesc.str_y, strdesc.page);
      }
#endif
      switch (strdesc.type) {
	case DESC_IDX: printf("index"); break;
	case DESC_SEP: printf("hline"); break;
        case DESC_VAR: printf("varname"); break;
	case DESC_WHAT: printf("descript"); break;
	case DESC_NODWHAT: printf("node_desc"); break;
	case DESC_NODMENU: printf("menu_name"); break;
	case DESC_NODE: printf("node_name"); break;
	case DESC_COL: printf("separator"); break;
	case DESC_INP: printf("input"); break;
	case DESC_LINK: printf("link"); break;
	default: printf("%x", strdesc.type); break;
      }
#ifndef FORMAT
      printf(" :: %x\n", strdesc.u8);
#else
      printf(";\n\t\t}\n\n");
#endif
    }
  }
#ifndef FORMAT
#else
  printf("\t}\n\n");
#endif

  if (hdr.type & FILE_CMD) {
    
    memcpy(&stropthdr, fl+hdr.ofs_st_hrc1, sizeof(stropthdr));

#ifndef FORMAT
    printf("\n[stropt head]\nlen:\t%x\nrecofs:\t%x\t%x\nstrofs:\t%x\t%x\n",
	stropthdr.hdrlen, stropthdr.ofs_st_subgroups, stropthdr.ofs_st_recs, stropthdr.ofs_st_names, stropthdr.ofs_st_descs);

    printf("%4x ", stropthdr.u5);

    printf("\n");
#endif

    {
      int c;

      for (c = 0; c < stropthdr.groups; c++) {
	int d;

	memcpy(&stroptgroupdesc, fl + hdr.ofs_st_hrc1 + stropthdr.hdrlen + c * sizeof(stroptgroupdesc), sizeof(stroptgroupdesc));

#ifndef FORMAT
	printf("\n\nstroptgroupdesc[%d](x%x) ", c, hdr.ofs_st_hrc1 + stropthdr.hdrlen + c * sizeof(stroptgroupdesc));
	printf("(%d) *%d x%x\n", stroptgroupdesc.id, stroptgroupdesc.opts, stroptgroupdesc.ofs_st_optdesc);
#else
	printf("\toptgroup {\n\t\tid = %d;\n\t\tcount = %d;\n", stroptgroupdesc.id, stroptgroupdesc.opts);
#endif

#ifdef FORMAT
	if (stroptgroupdesc.pseudosubgroup != 0xffff) {
	  printf("\t\tsuboptgroup {\n");
	  printf("\t\t\tid = %d;\n", stroptgroupdesc.pseudosubgroup);
	}
#endif
	for (d = 0; d < stroptgroupdesc.opts; d++) {
	  if (stroptgroupdesc.pseudosubgroup != 0xffff) {
	    memcpy(&stroptdesc, fl + hdr.ofs_st_hrc1 + stropthdr.ofs_st_recs + stroptgroupdesc.ofs_st_optdesc + d * sizeof(stroptdesc), sizeof(stroptdesc));
	    load_optdesc();
	  } else {
	    memcpy(&stroptsubgroupdesc, fl + hdr.ofs_st_hrc1 + stropthdr.ofs_st_subgroups + stroptgroupdesc.ofs_st_optdesc + d * sizeof(stroptsubgroupdesc), sizeof(stroptsubgroupdesc));
#ifndef FORMAT
	    printf("\n\nstroptsubgroupdesc(%x) .. %d x%x\n", stroptsubgroupdesc.id, stroptsubgroupdesc.count, stroptsubgroupdesc.ofs_st_recs);
#else
	    printf("\t\tsuboptgroup {\n\t\t\tid = %d;\n", stroptsubgroupdesc.id);
#endif
	    {
	      int e;
	      for (e = 0; e < stroptsubgroupdesc.count; e++) {
		memcpy(&stroptdesc, fl + hdr.ofs_st_hrc1 + stropthdr.ofs_st_recs + stroptsubgroupdesc.ofs_st_recs + e * sizeof(stroptdesc), sizeof(stroptdesc));
		load_optdesc();
	      }
	    }
#ifdef FORMAT
	    printf("\t\t}\n");
#endif
	  }
	}
#ifdef FORMAT
	if (stroptgroupdesc.pseudosubgroup != 0xffff) {
	  printf("\t\t}\n");
	}
#endif
#ifdef FORMAT
	printf("\t}\n\n");
#endif
      }
    }
    
    memcpy(&strhelphdr, fl+hdr.ofs_st_hrc2, sizeof(strhelphdr));

#ifndef FORMAT
    printf("\n[strhelp head]\nlen:\t%x\n?:\t%x\nstrofs:\t%x\n", strhelphdr.hdrlen, strhelphdr.u0, strhelphdr.ofs);

    printf("%2x :: ", strhelphdr.u1);
    printf("%2x :: ", strhelphdr.u2);
    printf("%2x :: ", strhelphdr.u3);
    printf("%2x :: ", strhelphdr.u4);
    printf("%2x :: ", strhelphdr.u5);
    printf("%2x :: ", strhelphdr.u6);
    printf("%2x :: ", strhelphdr.u7);
    printf("%2x :: ", strhelphdr.u8);
    printf("%2x :: ", strhelphdr.u9);
    printf("%2x :: ", strhelphdr.u10);
    printf("%2x :: ", strhelphdr.u11);
    printf("%2x :: ", strhelphdr.u12);
    printf("%2x :: ", strhelphdr.u13);
    printf("%2x :: ", strhelphdr.u14);
    printf("%2x :: ", strhelphdr.u15);
    printf("%2x :: ", strhelphdr.u16);
    printf("%2x", strhelphdr.u17);
    printf("\n");
#endif

    {
      int c;
      for (c=0;c*sizeof(strhelpdesc)+strhelphdr.hdrlen<strhelphdr.ofs;c++) {
	memcpy(&strhelpdesc, fl+hdr.ofs_st_hrc2+strhelphdr.hdrlen+c*sizeof(strhelpdesc), sizeof(strhelpdesc));
#ifndef FORMAT
	printf("\n[%d] (x%x)\n%s\n%2x :: %2x :: suboptgroup %4x\n",
	    c, strhelpdesc.ofs_st_str, !strhelpdesc.suboptgroup ? fl+hdr.ofs_st_hrc2+strhelphdr.ofs+strhelpdesc.ofs_st_str : "<LINK>",
	    strhelpdesc.u1, strhelpdesc.u2, strhelpdesc.suboptgroup);
#else
	{
	  static int grp = 0, notfirst = 0;
	  if (! notfirst) {
	    printf("\tdesc {\n\t\tgrp = %d;\n", grp);
	    notfirst++;
	  }
	  if (! strhelpdesc.suboptgroup) {
	    if (! *(fl+hdr.ofs_st_hrc2+strhelphdr.ofs+strhelpdesc.ofs_st_str)) {
	      notfirst = 0; grp++;
	      printf("\t}\n\n");
	    } else {
	      char *str = fl+hdr.ofs_st_hrc2+strhelphdr.ofs+strhelpdesc.ofs_st_str;
	      if (grp && notfirst == 1) {
		char *str2 = strdup(str);
		*strchr(str2, ' ') = 0;
		printf("\t\toptname = \"%s\";\n", str2);
	      }
	      printf("\t\ttext {\n\t\t\tstr = \"%s\";\n\t\t}\n\n", str);
	      notfirst++;
	    }
	  } else {
	    printf("\t\tinclude {\n");
	    printf("\t\t\toptgroup = %d;\n", strhelpdesc.ofs_st_str);
	    printf("\t\t\tsuboptgroup = %d;\n", strhelpdesc.suboptgroup);
	    printf("\t\t}\n\n");
	  }
	}
#endif
      }
    }
  }
#ifdef FORMAT
  printf("}\n");
#endif
  return 0;
}
