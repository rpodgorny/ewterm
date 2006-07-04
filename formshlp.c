#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "forms.h"


#define COL_HELP (7 + 1)
#define WC_HELP COL_HELP << 8

extern WINDOW *HelpWindow;

WINDOW *helppad = NULL;
static int max_y = 0;
static int scrolled_by = 0; extern int pad_size;
static int maxy, maxx;

extern void del_forms_wins();
extern void init_forms_wins();

void draw_help_windows();

int
count_lines(char *text) {
  int count = 1;

  while ((text = strchr(text, '\n'))) text++, count++;

  return count;
}

void
draw_lines(WINDOW *helppad, char *text, int y, int x) {
  if (!helppad) return;

  wattrset(helppad, WC_HELP);
  mvwchgat(helppad, y, 0, -1, WC_HELP, COL_HELP, 0);
  mvwaddstr(helppad, y, x, text);
}

void
render_help(WINDOW *helppad, node_t *node, desc_t *desc) {
  char **strs = (char **) desc->str.list;
  int count = desc->str.len;
  int y = 0;

  if (helppad) {
    wattron(helppad, WC_HELP);
    wclear(helppad);
  }

  for (; count-- > 0; strs++) {
    if (**strs == 0x1) {
      include_t include;
      optgroup_t **groups = (optgroup_t **) node->optgroup.list;
      int count = node->optgroup.len;

      memcpy(&include, *strs, sizeof(include));

      for (; count--; groups++) {
	if ((*groups)->id == include.optgroup) {
	  suboptgroup_t **subgroups = (suboptgroup_t **) (*groups)->subs.list;
	  int count = (*groups)->subs.len;

	  for (; count--; subgroups++) {
	    if ((*subgroups)->id == include.suboptgroup) {
	      option_t **options = (option_t **) (*subgroups)->options.list;
	      int count = (*subgroups)->options.len;

	      for (; count--; options++) {
		char line[512];

		snprintf(line, 512, "                %-13s%s\n",
		         (*options)->name, (*options)->desc);
		draw_lines(helppad, line, y++, 0);
		if ((*options)->longdesc) {
		  draw_lines(helppad, (*options)->longdesc, y, 0);
		  y += count_lines((*options)->longdesc);
		} else y++;
	      }
	    }
	  }
	}
      }
    } else {
      draw_lines(helppad, *strs, y, 0);
      y += count_lines(*strs);
    }
  }

  max_y = y;

  if (helppad) {
    int j;
    wchgat(helppad, -1, WC_HELP, COL_HELP, 0);
    for (j = 0; j < max_y + pad_size; j++)
      mvwchgat(helppad, j, 0, -1, WC_HELP, COL_HELP, 0);
  }
}


void start_loop_help();

int
init_show_help(node_t *node, char *name) {
  desc_t *desc = NULL;

  if (!name) {
    desc_t **descs = (desc_t **) node->desc.list;
    int count = node->desc.len;

    for (; count-- > 0; descs++) {
      if (descs[0]->optname == NULL) desc = descs[0];
    }
  } else {
    desc = get_hash(node->deschash, name);
  }

  if (!desc) return -1;
  
  if (helppad) delwin(helppad);
  helppad = NULL;

  render_help(helppad, node, desc); /* count max_y */
  
  del_forms_wins();
  getmaxyx(HelpWindow, maxy, maxx);
  pad_size = max_y + 1;
  init_forms_wins();
  helppad = newpad((max_y/pad_size + 2) * pad_size, node->width + 1);
  scrollok(helppad, FALSE);
  scrolled_by = 0;

  render_help(helppad, node, desc);

  start_loop_help();
  return 0;
}

void
done_show_help() {
  delwin(helppad); helppad = NULL;
}

void
draw_help_windows() {
    pnoutrefresh(helppad, scrolled_by, 0, 8, 0, pad_size + 8, maxx);
}

void
start_loop_help() {
    keypad(helppad, TRUE);
    intrflush(helppad, FALSE);
    leaveok(helppad, FALSE);

    draw_help_windows();
}

void
end_loop_help(int ch) {
    switch (ch) {
#if 0
      case KEY_F(3):
	end_help();
	return;

      case KEY_F(1):
	show_help(node, NULL);
#endif

      case KEY_UP:
	if (scrolled_by > 0) scrolled_by--;
	break;
	
      case KEY_DOWN:
	if (scrolled_by < max_y) scrolled_by++;
	break;
	
      case KEY_PPAGE:
	if (scrolled_by >= pad_size) scrolled_by -= pad_size; else scrolled_by = 0;
	break;
	
      case KEY_NPAGE:
	if (scrolled_by <= max_y - pad_size) scrolled_by += pad_size; else scrolled_by = max_y;
	break;
    }
}
