%{
#include "forms.h"
#include "parseforms.tab.h"
%}

white	[ \t\n]*
ws	{white}
digit	[0-9]
comment	#.*
/*string	\"(.|\n)*\"{ws};*/
/*stringe	\"{ws};*/
/*string	\"([^{stringe}]|\n)*{stringe}*/
/*string	\"(.|\n)*\";*/

%x str

%%

	char string_buf[65535];
	char *string_buf_ptr;

{comment}	;
{white}		;

{digit}+{ws};	{ yylval.number = atoi(yytext); return NUMBER; }
	/*{string}	{ yylval.string = yytext+1; yylval.string[yyleng-3] = '\0'; return STRING; }*/

\"		{ string_buf_ptr = string_buf; BEGIN(str); }
<str>\"{ws};	{ BEGIN(INITIAL); *string_buf_ptr = 0; yylval.string = string_buf; return STRING; }
<str>\"		{ char *yptr = yytext; while (*yptr) *string_buf_ptr++ = *yptr++; }
<str>([^\"]+)	{ char *yptr = yytext; while (*yptr) *string_buf_ptr++ = *yptr++; }

=		{ return IS; }
\{		{ return START; }
\}		{ return END; }

node		{ return NODE; }

name		{ return NAME; }
type		{ return TYPE; }
parent		{ return PARENT; }
group		{ return GROUP; }
width		{ return WIDTH; }
fields		{ return FIELDS; }

line		{ return LINE; }
no		{ return NO; }

field		{ return FIELD; }

str		{ return STR; }
x		{ return X; }
y		{ return Y; }
page		{ return PAGE; }
optname		{ return OPTNAME; }
target		{ return TARGET; }
flags		{ return FLAGS; }

optgroup	{ return OPTGROUP; }
suboptgroup	{ return SUBOPTGROUP; }

id		{ return ID; }
count		{ return COUNT; }

option		{ return OPTION; }
longdesc	{ return LONGDESC; }

desc		{ return DESC; }

grp		{ return GRP; }
text		{ return TEXT; }
include		{ return INCLUDE; }

menu{ws};	{ yylval.number = NODE_MENU; return NODETYPE; }
command{ws};	{ yylval.number = NODE_COMMAND; return NODETYPE; }

index{ws};	{ yylval.number = W_INDEX; return FIELDTYPE; }
hline{ws};	{ yylval.number = W_HLINE; return FIELDTYPE; }
varname{ws};	{ yylval.number = W_VARNAME; return FIELDTYPE; }
descript{ws};	{ yylval.number = W_DESC; return FIELDTYPE; }
node_desc{ws};	{ yylval.number = W_NDESC; return FIELDTYPE; }
menu_name{ws};	{ yylval.number = W_MENU; return FIELDTYPE; }
node_name{ws};	{ yylval.number = W_NODE; return FIELDTYPE; }
separator{ws};	{ yylval.number = W_SEP; return FIELDTYPE; }
input{ws};	{ yylval.number = W_INPUT; return FIELDTYPE; }
link{ws};	{ yylval.number = W_LINK; return FIELDTYPE; }

required{ws};	{ yylval.number = FIELD_REQUIRED; return FIELDFLAGS; }

%%

