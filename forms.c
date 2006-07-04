/* This handles forms. */

/* It's pretty hackish and ugly, sorry :(. */

#include <ctype.h>
#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ewterm.h"
#include "mml.h"

#include "forms.h"

char FormsDir[256] = EWDIR "/rc";

extern int yyparse();

extern int yydebug;
extern FILE *yyin;

int key;

hash_t *nodehash[256];

node_t *node;

list_t nodes = {0, 0};

int active = 0;

//int insert = 1;

field_t *afield;

int move_down = 0, move_up = 0;
int moved_down = 0, moved_up = 0;

int ix = 0;

extern void CmdEnterHook();
extern void HideForms();
extern void RedrawStatus();
extern int LineBLen, LineBPos;
extern char LineBuf[];
extern WINDOW *HelpWindow;
extern WINDOW *CUAWindow;

extern int init_show_help(node_t *node, char *optname);

WINDOW *headpad = NULL;
WINDOW *formspad = NULL;
WINDOW *vdelimpad = NULL;
WINDOW *hdelimpad = NULL;
int scrolled_by = 0, max_y = 0, pad_size = 0;

int maxy, maxx;

int load_node(char *);
int goto_node_do(char *);
int goto_node(char *);

enum color_types {
  C_BLACK, C_BLUE, C_GREEN, C_CYAN, C_RED, C_MAGENTA,
  C_BROWN, C_LIGHT_GRAY, C_DARK_GRAY, C_LIGHT_BLUE,
  C_LIGHT_GREEN, C_LIGHT_CYAN, C_LIGHT_RED, C_LIGHT_MAGENTA,
  C_YELLOW, C_WHITE, MAX_COLOR
};

#undef COL_HELP
#undef COL_LINK
#define COL_HELP (7 + 1)
#define COL_LINK (8 + 1)

#define	WC_TEXT		COL_HELP<<8
#define	WC_NODE		COL_HELP<<8 | A_BOLD
#define	WC_INPUT	COL_LINK<<8
#define	WC_INPUTA	COL_LINK<<8 | A_BOLD
#define	WC_LINK		COL_LINK<<8
#define	WC_LINKA	COL_LINK<<8 | A_BOLD
#define WC_OPTA		COL_HELP<<8 | A_BOLD
#define	WC_VARREQ	COL_HELP<<8 | A_BOLD

#if 0
#define	WC_TEXT		C_LIGHT_GRAY
#define	WC_NODE		C_LIGHT_CYAN
#define	WC_INPUT	C_GREEN
#define	WC_INPUTA	C_LIGHT_GREEN
#define	WC_LINK		C_YELLOW
#define	WC_LINK		C_YELLOW
#define	WC_LINKA	C_LIGHT_RED
#define WC_OPTA		C_LIGHT_GREEN
#endif



void
setcolor(int color) {
  if (!formspad) return;
  if (!headpad) return;
  
  wattrset(formspad, color);
  wattrset(headpad, color);
}

void
draw(int pad, int x, int y, char *str) {
  if (!formspad) return;
  if (!headpad) return;
  
  mvwaddstr(pad?formspad:headpad, y, x, str);
}


void
w_dummy_render(field_t *field) {
  setcolor(WC_TEXT);
  draw(field->page, field->x, field->y, field->str);
  if (max_y < field->y) max_y = field->y;
}

void
w_node_render(field_t *field) {
  setcolor(WC_NODE);
  draw(field->page, field->x, (field->page?8:0) + field->y, field->str);
  draw(field->page, field->x, field->y, field->str);
  if (max_y < field->y) max_y = field->y;
}

void
w_varname_render(field_t *field) {
  setcolor(field->flags&FIELD_REQUIRED?WC_VARREQ:WC_TEXT);
  draw(field->page, field->x, field->y, field->str);
  if (max_y < field->y) max_y = field->y;
}

void
w_input_render(field_t *field) {
  setcolor(field->active?WC_INPUTA:WC_INPUT);
  draw(field->page, field->x, field->y, field->str);
  draw(field->page, field->x, field->y, field->input);
  if (max_y < field->y) max_y = field->y;
  
  if (formspad) wmove(formspad, field->y, field->x + ix);
}

void
w_link_render(field_t *field) {
  setcolor(field->active?WC_LINKA:WC_LINK);
  draw(field->page, field->x, field->y, field->str);
  if (max_y < field->y) max_y = field->y;
}

void
w_opta_render(field_t *field) {
  setcolor(WC_OPTA);
  if (field->line->optname) {
    draw(0, 4, 4, "                   ");
    draw(0, 4, 4, field->line->optname);
  }
}


void
w_focus(field_t *field)
{
  field->active = 1;
  field->widget->render(field);
}

void
w_defocus(field_t *field)
{
  field->active = 0;
  field->widget->render(field);
}

void
w_input_focus(field_t *field)
{
  if (ix > strlen(field->input))
    ix = strlen(field->input);
  w_focus(field);
}

void
w_link_raise(field_t *field)
{
  if (goto_node(field->target) < 0);
}

void
w_input_key(field_t *field)
{
  if (key == KEY_BACKSPACE) {
    if (ix > 0) {
      memmove(field->input + ix - 1, field->input + ix,
	      strlen(field->input + ix) + 1);
      ix--;
    } else {
      move_up = moved_up?0:1;
    }
  } else if (key >= 32) {
    if (((ConvertMode & OVERWRITE) && ix < strlen(field->input)) ||
	strlen(field->input) < strlen(field->str)) {
      if (!(ConvertMode & OVERWRITE)) {
	memmove(field->input + ix + 1, field->input + ix,
		strlen(field->input + ix) + 1);
      } else if (!field->input[ix]) field->input[ix+1] = '\0';
      field->input[ix] = key;
      ix++;
    } else if (ix >= strlen(field->str)) {
      move_down = moved_down?0:1;
    }
  }
  w_input_render(field);
}


#define trigger_hook(field, hook) { \
  if (field && ((field_t *) field)->widget->hook) \
  ((field_t *) field)->widget->hook (field); \
}

widget_t w_index = {
  W_INDEX,
  NULL,
  NULL,
  NULL,
  NULL,
  w_dummy_render,
};
widget_t w_hline = {
  W_HLINE,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* change to w_dummy_render for no hline */
};
widget_t w_varname = {
  W_VARNAME,
  NULL,
  NULL,
  NULL,
  NULL,
  w_varname_render,
};
widget_t w_desc = {
  W_DESC,
  NULL,
  NULL,
  NULL,
  NULL,
  w_dummy_render,
};
widget_t w_ndesc = {
  W_NDESC,
  NULL,
  NULL,
  NULL,
  NULL,
  w_dummy_render,
};
widget_t w_menu = {
  W_MENU,
  NULL,
  NULL,
  NULL,
  NULL,
  w_node_render,
};
widget_t w_node = {
  W_NODE,
  NULL,
  NULL,
  NULL,
  NULL,
  w_node_render,
};
widget_t w_sep = {
  W_SEP,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* change to w_dummy_render for separators */
};
widget_t w_input = {
  W_INPUT,
  w_input_focus,
  w_defocus,
  NULL,
  w_input_key,
  w_input_render,
};
widget_t w_link = {
  W_LINK,
  w_focus,
  w_defocus,
  w_link_raise,
  NULL,
  w_link_render,
};







void
bye(int ret) {
  endwin();
  printf("\n");

  exit(ret);
}

void
stripspaces(char *str) {
  while (*str) {
    char *lastpos;

    while (*str && *str != ' ') str++;
    if (!*str) break;

    lastpos = str;

    while (*str && *str == ' ') str++;

    memmove(lastpos, str, strlen(str) + 1);
    str = lastpos;
  }
}


void
forms_border_arrow(char ch)
{
  mvwaddch(hdelimpad, 0, 1, ch);
}

void
render() {
  int j;

  wclear(HelpWindow);
  wchgat(HelpWindow, -1, WC_TEXT, COL_HELP, 0);
  for (j = 0; j < 8; j++) mvwchgat(headpad, j, 0, -1, WC_TEXT, COL_HELP, 0);
  for (j = 0; j < max_y + pad_size; j++) mvwchgat(formspad, j, 0, -1, WC_TEXT, COL_HELP, 0);

  for (j = 0; j < node->line.len; j++) {
    line_t *line = node->line.list[j];
    int k;

    for (k = 0; k < line->field.len; k++) {
      field_t *field = line->field.list[k];

      if (field->widget->render)
	field->widget->render(field);
    }
  }
}

int
load_node(char *file) {
  int i;

  /* TODO: Use hash ;) */
  
  for (i = 0; i < nodes.len; i++) {
    if (!strcmp(((node_t *) nodes.list[i])->name, file)) {
      node = nodes.list[i];
      return 0;
    }
  }
  
//  yydebug = 1;

  if (yydebug) fprintf(stderr, "--file %s\n", file);
  yyin = fopen(file, "r");
  if (!yyin) {
    return -1;
  }
  
//  endwin();
  if (yyparse()) {
    return -1;
  }
//  refresh();
  
  fclose(yyin);

  return 1;
}

void
del_forms_wins()
{
  if (formspad) delwin(formspad);
  formspad = NULL;

  if (headpad) delwin(headpad);
  headpad = NULL;

  if (vdelimpad) delwin(vdelimpad);
  vdelimpad = NULL;

  if (hdelimpad) delwin(hdelimpad);
  hdelimpad = NULL;
}

void
init_forms_wins()
{
  int ret;

  if (pad_size > maxy - 3 - 5 - 8) pad_size = maxy - 3 - 5 - 8;
  DelHelpWin();
  wnoutrefresh(CUAWindow);
  MkHelpWin();

  vdelimpad = newpad(pad_size + 9, 1);
  hdelimpad = newpad(1, node->width + 2);
  for (ret = 0; ret < pad_size + 9; ret++) mvwaddch(vdelimpad, ret, 0, '|');
  for (ret = 0; ret < node->width + 1; ret++) mvwaddch(hdelimpad, 0, ret, '-'); mvwaddch(hdelimpad, 0, ret, '+');
  forms_border_arrow('^');

  formspad = newpad((max_y/pad_size + 2) * pad_size, node->width + 1);
  headpad = newpad(8, node->width + 1);

  render();
}

int
goto_node_do(char *file) {
  int ret = load_node(file);

  if (ret > 0) {
    node = nodes.list[nodes.len - 1];
  } else if (ret < 0) {
    return ret;
  }

  del_forms_wins();

  max_y = 0;
  render(); /* get the max_y */
  pad_size = max_y + 1; /* see back_from_help() as well */

  scrolled_by = 0;
  init_forms_wins();

  active = 0;

  if (node->actives.list) {
    trigger_hook(node->actives.list[active], focus);
  }

  touchwin(formspad);
  touchwin(headpad);

  return 0;
}

static char *requested_cmd = NULL;

int
goto_node(char *target)
{
  struct mml_command *cmd;
  char *orig_target = target;
  char *preload;
  char *strip;
  node_t *uninode = NULL;
  int ret;

  target = strdup(target);
  if (strchr(target, ':')) *strchr(target, ':') = '\0';
  if (strchr(target, ';')) *strchr(target, ';') = '\0';
  stripspaces(target);
  preload = strdup(target);

  /* If there's CRPBX-/.CRPBX, we need to load it first, and then fill missing
   * pieces of CRPBX-/WHATEVER by it (like help). I'm not sure I understand it
   * fully yet, but now it seems to work :). */

  if (node && (strip = strchr(preload, '/'))) {
    preload = realloc(preload, strlen(preload) + 200); /* safe hopefully ;) */
    strip = strchr(preload, '/'); /* !!! ;) */

    strip++;
    strip[0] = '\0';
    
    /* XXX: here we rely on the fact that name looks like ./BLA */
    strcat(preload, node->name);
    memmove(strip + 1, strip + 2, strlen(strip + 2) + 1);
    
    /* Could we load this? */
    if (access(preload, R_OK) >= 0) {
      load_node(preload);
      uninode = nodes.list[nodes.len - 1];
    }
  }

  free(preload);
  
  if ((ret = goto_node_do(target)) < 0) {
    free(target);
    return ret;
  }

  /* XXX: Pretty ugly code duplication, I know. */

  if (uninode) {
    /* Fill missing desc pieces. */
    desc_t **descs1 = (desc_t **) uninode->desc.list;
    int count1 = uninode->desc.len;

    for (; --count1 > 0; descs1++) {
      desc_t **descs2 = (desc_t **) node->desc.list;
      int count2 = node->desc.len;
      int found = 0;

      for (; --count2 > 0; descs2++) {
	/* If strings match or they are both NULL. */
	if ((!descs1[0]->optname && !descs2[0]->optname) ||
	    (descs1[0]->optname && descs2[0]->optname
	     && !strcmp(descs1[0]->optname, descs2[0]->optname))) found = 1;
      }

      if (!found) {
	add_list(&node->desc, descs1[0]);
        if (descs1[0]->optname)
	  add_hash(node->deschash, descs1[0]->optname, descs1[0]);
      }
    }

    /* TODO: opt as well? :/ */

    render();
  }

  if (requested_cmd && node->type & NODE_COMMAND) {
    cmd = parse_mml_command(requested_cmd);
    free(requested_cmd); requested_cmd = NULL;
  } else {
    char *t = strdup(orig_target);
    cmd = parse_mml_command(t);
    free(t);

    if (node->type & NODE_MENU && !requested_cmd) {
      requested_cmd = strdup(orig_target);
    }
  }

  if (cmd->params) {
    int i;

    for (i = 0; i < cmd->params; i++) {
      int len, tlen, j;

      if (!mml_param_valid(&cmd->param[i]))
	continue;

      len = tlen = strlen(cmd->param[i].value);

      /* Hrrrmmmmmmzzz.. This can look strange. This is strange. */

      for (j = 0; j < node->actives.len; j++) {
        field_t *field = node->actives.list[j];

	if (field->widget->type == W_INPUT &&
	    !strcmp(field->line->optname, cmd->param[i].name)) {
	  int destlen = strlen(field->str);

	  strncpy(field->input, cmd->param[i].value + tlen - len, destlen);
	  len -= destlen;
	  if (len <= 0) break;
	}
      }
    }

    render();
  }

  free_mml_command(cmd);

  free(target);
  return ret;
}


void
init_forms() {
  getmaxyx(HelpWindow, maxy, maxx);
  
  chdir(FormsDir);
}

void
done_forms() {
  if (requested_cmd) free(requested_cmd), requested_cmd = NULL;

  del_forms_wins();
}

int och = 0;

void end_loop(int ch);

void
draw_form_windows() {
  extern int in_help;
  extern void draw_help_windows();
  if (!node) return;
    pnoutrefresh(hdelimpad, 0, 0, pad_size + 9, 0, pad_size + 9, node->width + 2);
    pnoutrefresh(vdelimpad, 0, 0, 0, node->width + 1, pad_size + 9, node->width + 1);
    pnoutrefresh(headpad, 0, 0, 0, 0, 8, maxx);
    pnoutrefresh(formspad, scrolled_by, 0, 8, 0, pad_size + 8, maxx);
    if (in_help) draw_help_windows();
}

void
back_from_help()
{
  del_forms_wins();
  render();
  pad_size = max_y + 1; /* see goto_node_do() as well */
  init_forms_wins();
}

void
start_loop() {
  afield = node->actives.list ? node->actives.list[active] : NULL;
  if (afield) {
    w_opta_render(afield); /* XXX */
    afield->widget->render(afield);
  }

  draw_form_windows();

  if (move_down || move_up) end_loop(och), start_loop();
}

void
end_loop(int ch) {
  int do_not_close = 0;

  och = ch;

  moved_up = move_up; moved_down = move_down;
  move_up = move_down = 0;

  switch (ch) {
#if 0
    case '\033':
      bye(0);
      break;
#endif

    case KEY_IC:
      ConvertMode ^= OVERWRITE;
      RedrawStatus();
      break;

    case KEY_RIGHT:
      if (afield && afield->widget->type == W_INPUT && ix < strlen(afield->input)) ix++;
      break;

    case KEY_LEFT:
      if (afield && afield->widget->type == W_INPUT && ix > 0) ix--;
      break;

    case KEY_HOME:
      if (afield && afield->widget->type == W_INPUT && ix > 0) ix = 0;
      break;

    case KEY_END:
      if (afield && afield->widget->type == W_INPUT && ix < strlen(afield->input)) ix = strlen(afield->input);
      break;

      /* pass-through.. see bellow the switch (we need to do the same for
       * queued moves up/down in move_(up|down)) */
    case KEY_UP:
    case KEY_DOWN:
      break;

    case KEY_PPAGE:
      if (node->actives.len) {
	trigger_hook(node->actives.list[active], defocus);
	active -= active % (pad_size + 1); active -= pad_size + 1;
	if (active < 0) active = 0;
	afield = node->actives.list[active];
	max_y = 0;
	trigger_hook(node->actives.list[active], focus);
	scrolled_by = active;
#if 0
	if (afield->y < scrolled_by - 1) {
	  scrolled_by = max_y - pad_size + 1;
	  if (scrolled_by < 0) scrolled_by = 0;
	}
#endif
      }
      break;

    case KEY_NPAGE:
      if (node->actives.len) {
	trigger_hook(afield, defocus);
	active -= active % (pad_size + 1); active += pad_size + 1;
	if (active > node->actives.len - 1) active = node->actives.len - 1;
	afield = node->actives.list[active];
	max_y = 0;
	trigger_hook(afield, focus);
	if (scrolled_by < active - pad_size) scrolled_by = active;
#if 0
	if (afield->y > pad_size + scrolled_by - 1) {
	  scrolled_by = max_y - 1;
	}
#endif
      }
      break;

    case '\t':
      ix = 0;
      ch = KEY_DOWN;
      break;

#if 0
    case KEY_F(1):
      show_help(node, afield->line->optname);
      break;

    case KEY_F(2):
      goto_node("OMEXCH");
      break;

    case KEY_F(3):
      if (node->parent && node->parent[0]) goto_node(node->parent);
      break;
#endif

    case KEY_F(9):
      do_not_close = 1;

    case KEY_ENTER:
    case '\n':
    case '\r':
      if (node->actives.len &&
	  (((node->type & NODE_MENU) && !(node->type & NODE_COMMAND)) ||
	   (afield && afield->widget->type == W_LINK))) {
	if (!node->actives.len) break;
	trigger_hook(afield, raise);

      } else if (node->type & NODE_COMMAND) {
	/* Handle node->type==3 as well. */
	int j;
	char cmdline[1024]; /* XXX */

	/* XXX: We rely on the fact that we start with ./BLA here! */
	strcpy(cmdline, node->name + 2);
	if (strchr(cmdline, '/')) *(strchr(cmdline, '/')-1) = 0; /* There's '-' as well. */
	strcat(cmdline, ":");

	for (j = 0; j < node->line.len; j++) {
	  line_t *line = node->line.list[j];
	  char *name = NULL;
	  char value[256] = "";
	  int req = 0;
	  int k;

	  for (k = 0; k < line->field.len; k++) {
	    field_t *field = line->field.list[k];

	    if (field->widget->type == W_VARNAME) {
	      name = field->line->optname;
	    } else if (field->widget->type == W_INPUT) {
	      if (field->flags & FIELD_REQUIRED) req++;
	      strcat(value, field->input);
#if 0
	    } else if (field->widget->type == W_MENU ||
		field->widget->type == W_NODE) {
	      char *cmdend;

	      strcpy(cmdline, field->str);

	      cmdend = strchr(cmdline, ':');
	      if (cmdend) *cmdend = '\0';
#endif
	    }
	  }

	  for (k = strlen(value) - 1; k >= 0; k--) value[k] = toupper(value[k]);

	  if (name && value[0])
	    sprintf(cmdline, "%s%s=%s,", cmdline, name, value);
	  else if (req) {
	    goto cmd_break; /* Ehm. */
	  }
	}

	if (cmdline[strlen(cmdline) - 1] == ',') cmdline[strlen(cmdline) - 1] = '\0';
	if (cmdline[strlen(cmdline) - 1] == ':') cmdline[strlen(cmdline) - 1] = '\0';
	if (cmdline[strlen(cmdline) - 1] != ';') strcat(cmdline, ";");

	strncpy(LineBuf, cmdline, 1024);
	LineBLen = strlen(LineBuf);
	LineBPos = 0;
	if (do_not_close) {
	  ClearLastLine = 0;
	  CmdEnterHook();
	  UpdateCUAW = 1;
	} else {
	  HideForms();
	}
#if 0
	endwin();
	printf("cmdline: %s\n", cmdline);
	bye(1);
#endif
      }
cmd_break:	
      break;

    default:
      if (!node->actives.len) break;
      key = ch;
      trigger_hook(afield, key);
      break;
  }

  if (ch == KEY_DOWN || move_down) {
    if (node->actives.len) {
      trigger_hook(afield, defocus);
      if (active < node->actives.len - 1) active++;
      afield = node->actives.list[active];
      max_y = 0;
      trigger_hook(afield, focus);

      if (active > scrolled_by + pad_size)
	scrolled_by += pad_size + 1;
#if 0
      if (afield->y > pad_size + scrolled_by - 1) {
	scrolled_by = max_y - 1;
      }
#endif
    }
  }

  if (ch == KEY_UP || move_up) {
    if (node->actives.len) {
      trigger_hook(node->actives.list[active], defocus);
      if (active > 0) active--;
      afield = node->actives.list[active];
      max_y = 0;
      trigger_hook(node->actives.list[active], focus);
      if (ch == KEY_BACKSPACE && afield->widget->type == W_INPUT) /* XXX */
	ix = strlen(afield->input);

      if (active < scrolled_by)
	scrolled_by -= pad_size + 1;
#if 0
      if (afield->y < scrolled_by + 1) {
	scrolled_by = max_y - pad_size + 1;
	if (scrolled_by < 0) scrolled_by = 0;
      }
#endif
    }
  }
}

int
goto_show_help(node_t *node, field_t *afield) {
  return init_show_help(node, afield ? afield->line->optname : NULL);
}

void
goto_parent(node_t *node) {
  if (node->parent && node->parent[0]) goto_node(node->parent);
}
