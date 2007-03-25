%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "forms.h"

void yyerror(char *);
int yylex();

node_t *node;
line_t *line;
field_t *field;
optgroup_t *optgroup;
suboptgroup_t *suboptgroup;
static option_t *option;
desc_t *desc;
include_t *include;

#define YYERROR_VERBOSE
#define YYDEBUG 1
%}

%union {
 int number;
 char *string;
}

%token NODE

%token NAME
%token TYPE
%token PARENT
%token GROUP
%token WIDTH
%token FIELDS

%token LINE

%token NO

%token FIELD

%token STR
%token X
%token Y
%token PAGE
%token TYPE
%token OPTNAME
%token TARGET
%token FLAGS

%token OPTION
%token OPTGROUP
%token SUBOPTGROUP

%token ID
%token COUNT
%token LONGDESC

%token DESC

%token GRP
%token OPTNAME
%token INCLUDE
%token TEXT

%token NODETYPE
%token FIELDTYPE
%token FIELDFLAGS

%token START
%token IS
%token END

%token STRING
%token NUMBER

%type   <string>   STRING
%type   <number>   NUMBER
%type   <number>   NODETYPE
%type   <number>   nodetype
%type   <number>   FIELDTYPE
%type   <number>   FIELDFLAGS

%%

nodes:
	| nodes node_block
	;

node_block:	NODE START
{
  node = calloc(1, sizeof(node_t));
  add_list(&nodes, node);
}
		node END

node:	node node_item | node_item

node_item:	node_name | node_type | node_parent | node_group | node_width | node_fields | line_block | optgroup_block | desc_block

node_name:	NAME IS STRING
{
  node->name = strdup(yylval.string);
  add_hash(nodehash, node->name, node);
};

node_type:	TYPE IS nodetype
{
  node->type = $3;
};

nodetype:	NUMBER
{
  $$ = $1; /*NODE_UNKNOWN;*/
}
		| NODETYPE
{
  $$ = $1;
};

node_parent:	PARENT IS STRING
{
  node->parent = strdup(yylval.string);
};

node_group:	GROUP IS STRING
{
  node->group = strdup(yylval.string);
};

node_width:	WIDTH IS NUMBER
{
  node->width = $3;
};

node_fields:	FIELDS IS NUMBER
{
  node->fields = $3;
};


line_block:	LINE START
{
  line = calloc(1, sizeof(line_t));
  add_list(&node->line, line);
}
		line END

line:	line line_item | line_item

line_item:	line_no | field_block

line_no:	NO IS NUMBER
{
  line->no = $3;
};


field_block:	FIELD START
{
  field = calloc(1, sizeof(field_t));
  add_list(&line->field, field);
  field->line = line;
}
		field END

field:	field field_item | field_item

field_item:	field_no | field_str | field_x | field_y | field_page | field_type | field_optname | field_target | field_flags

field_no:	NO IS NUMBER
{
  field->no = $3;
};

field_str:	STR IS STRING
{
  field->str = strdup(yylval.string);
};

field_x:	X IS NUMBER
{
  field->x = $3;
};

field_y:	Y IS NUMBER
{
  field->y = $3;
};

field_page:	PAGE IS NUMBER
{
  field->page = $3;
};

field_flags:	FLAGS IS FIELDFLAGS
{
  field->flags = $3;
}

field_type:	TYPE IS FIELDTYPE
{
  switch ($3) {
    /* XXX array is better */
    case W_INDEX: field->widget = &w_index; break;
    case W_HLINE: field->widget = &w_hline; break;
    case W_VARNAME: field->widget = &w_varname; break;
    case W_DESC: field->widget = &w_desc; break;
    case W_NDESC: field->widget = &w_ndesc; break;
    case W_MENU: field->widget = &w_menu; break;
    case W_NODE: field->widget = &w_node; break;
    case W_SEP: field->widget = &w_sep; break;
    case W_INPUT: field->widget = &w_input; break;
    case W_LINK: field->widget = &w_link; break;
    default: fprintf(stderr, "Alert! Got invalid widget type %d!\n", $3); field->widget = NULL; break;
  }
  if (field->widget->type == W_INPUT) {
    memset(field->str, '_', strlen(field->str));
    field->input = strdup(field->str);
    field->input[0] = '\0';
  }
  if (field->widget->type == W_INPUT || field->widget->type == W_LINK)
    add_list(&node->actives, field);
};

field_optname:	OPTNAME IS STRING
{
  /* XXX: this is evil hack */
  line->optname = strdup(yylval.string);
  add_hash(node->opthash, line->optname, line);
};

field_target:	TARGET IS STRING
{
  field->target = strdup(yylval.string);
};


optgroup_block:	OPTGROUP START
{
  optgroup = calloc(1, sizeof(optgroup_t));
  add_list(&node->optgroup, optgroup);
}
		optgroup END

optgroup:	optgroup optgroup_item | optgroup_item

optgroup_item:	optgroup_id | optgroup_count | suboptgroup_block

optgroup_id:	ID IS NUMBER
{
  optgroup->id = $3;
}

optgroup_count:	COUNT IS NUMBER
{
  optgroup->count = $3;
}

suboptgroup_block:	SUBOPTGROUP START
{
  suboptgroup = calloc(1, sizeof(suboptgroup_t));
  add_list(&optgroup->subs, suboptgroup);
}
		suboptgroup END

suboptgroup:	suboptgroup suboptgroup_item | suboptgroup_item

suboptgroup_item:	suboptgroup_id | option_block

suboptgroup_id:	ID IS NUMBER
{
  suboptgroup->id = $3;
}

option_block:	OPTION START
{
  option = calloc(1, sizeof(option_t));
  add_list(&suboptgroup->options, option);
}		option END

option:		option option_item | option_item

option_item:	option_name | option_desc | option_longdesc

option_name:	NAME IS STRING
{
  option->name = strdup(yylval.string);
}

option_desc:	DESC IS STRING
{
  option->desc = strdup(yylval.string);
}

option_longdesc:	LONGDESC IS STRING
{
  option->longdesc = strdup(yylval.string);
}


desc_block:	DESC START
{
  desc = calloc(1, sizeof(desc_t));
  add_list(&node->desc, desc);
}
		desc END

desc:	desc desc_item | desc_item

desc_item:	desc_grp | desc_optname | text_block | include_block

desc_grp:	GRP IS NUMBER
{
  desc->grp = $3;
}

desc_optname:	OPTNAME IS STRING
{
  desc->optname = strdup(yylval.string);
  add_hash(node->deschash, desc->optname, desc);
}


text_block:	TEXT START text_str END

text_str:	STR IS STRING
{
  add_list(&desc->str, strdup(yylval.string));
}


include_block:	INCLUDE START
{
  include = calloc(1, sizeof(include_t));
  include->magic = 0x01;
  add_list(&desc->str, include);
}
		include END

include:	include include_item | include_item
include_item:	include_optgroup | include_suboptgroup

include_optgroup:	OPTGROUP IS NUMBER
{
  include->optgroup = $3;
}

include_suboptgroup:	SUBOPTGROUP IS NUMBER
{
  include->suboptgroup = $3;
}

%%

void yyerror(char *s) {
  fprintf(stderr, "Parser failed: %s\n", s);
}
