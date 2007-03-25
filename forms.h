#ifndef __EWTERM_FORMS_H
#define __EWTERM_FORMS_H

typedef struct {
  int len;
  void **list;
} list_t;

typedef struct HASH_T {
  char *str;
  void *val;
  
  struct HASH_T *next;
} hash_t;

typedef struct {
  char *name;
  /* bitmask (?) */
#define NODE_COMMAND	1
#define NODE_MENU	2
  int type;
  char *parent;
  char *group;
  int width;
  int fields;
  
  list_t line;
  list_t optgroup;
  list_t desc;
  list_t actives;

  hash_t *opthash[256];
  hash_t *deschash[256];
} node_t;

typedef struct {
  int no;
  char *optname;

  list_t field;
} line_t;

typedef struct FIELD_T field_t;

typedef struct WIDGET_T {
  enum {
    W_INDEX,
    W_HLINE,
    W_VARNAME,
    W_DESC,
    W_NDESC,
    W_MENU,
    W_NODE,
    W_SEP,
    W_INPUT,
    W_LINK,
  } type;
  
  void (*focus)(field_t *);
  void (*defocus)(field_t *);
  void (*raise)(field_t *);
  void (*key)(field_t *);
  void (*render)(field_t *);
} widget_t;

extern widget_t w_index;
extern widget_t w_hline;
extern widget_t w_varname;
extern widget_t w_desc;
extern widget_t w_ndesc;
extern widget_t w_menu;
extern widget_t w_node;
extern widget_t w_sep;
extern widget_t w_input;
extern widget_t w_link;

struct FIELD_T {
  int no;
  char *str;
  int x, y, page;
#define FIELD_REQUIRED 16
  int flags;

  char *target;
  char *input;
  
  int active;

  widget_t *widget;
  line_t *line;
};

typedef struct {
  char *name, *desc, *longdesc;
} option_t;

typedef struct {
  int id;
  list_t options;
} suboptgroup_t;

typedef struct {
  int id, count;
  list_t subs;
} optgroup_t;

typedef struct {
  unsigned char magic;
  int optgroup;
  int suboptgroup;
} include_t;

typedef struct {
  int grp;
  char *optname;

  list_t str;
} desc_t;

extern hash_t *nodehash[256];

extern list_t nodes;
extern node_t *node;

extern void add_list(list_t *, void *);
extern void add_hash(hash_t **, char *, void *);
extern void *get_hash(hash_t **, char *);

extern void setcolor(int);
extern void draw(int, int, int, char *);

#endif
