#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "mml.h"

extern void stripspaces(char *);

struct mml_command *
parse_mml_command(char *str) {
	struct mml_command *cmd = malloc(sizeof(struct mml_command));
	char *cmdstr = strdup(str);
	char *ocmdstr = cmdstr;

	cmd->params = 0;
	cmd->command = cmdstr;

	/* Target can look even like AS DF:ABCD=EFGH,IJKL="=;, ",BFLM =1; */

	cmdstr = strchr(cmdstr, ':');
	if (cmdstr) {
		*cmdstr = '\0';
		cmdstr++;
		while (cmdstr && *cmdstr) {
			char *t2;

			cmd->param[cmd->params].name = cmdstr;
			cmd->param[cmd->params].value = NULL;
			t2 = strchr(cmdstr, '=');
			if (t2) {
				char *end, *end1, *end2;

				cmdstr = t2;
				*cmdstr = '\0';
				stripspaces(cmd->param[cmd->params].name);
				cmdstr++;
				cmd->param[cmd->params].value = cmdstr;

#if 1
				/* Quotes support */
				while (cmdstr) {
					char *quote = strchr(cmdstr, '"');

					end1 = strchr(cmdstr, ',');
					end2 = strchr(cmdstr, ';');
					if (!end2 || (end1 && end1 < end2)) end = end1; else end = end2;
					if (quote && end && quote < end) {
						quote = strchr(quote + 1, '"');
					} else break;
					cmdstr = quote;
					if (cmdstr) cmdstr++;
				}
#else
				/* No quotes support */
				end1 = strchr(cmdstr, ',');
				end2 = strchr(cmdstr, ';');
				if (!end2 || (end1 && end1 < end2)) end = end1; else end = end2;
#endif

				cmdstr = end;
				if (cmdstr) {
					*cmdstr = '\0';
					if (end == end1) cmdstr++;
				}

				cmd->param[cmd->params].name = strdup(cmd->param[cmd->params].name);
				cmd->param[cmd->params].value = strdup(cmd->param[cmd->params].value);
			} else {
				cmd->param[cmd->params].name = strdup(cmd->param[cmd->params].name);
				cmdstr = strchr(cmdstr, ',');
				if (cmdstr) *cmdstr = '\0', cmdstr++;
			}
			if (++cmd->params >= 256) break;
		}
	} else {
		char *x = strchr(ocmdstr, ';');
		if (x) *x = 0;
	}

	cmd->command = strdup(cmd->command);
	free(ocmdstr);
	return cmd;
}

int mml_param_valid(struct mml_param *param) {
	return (param->name && *param->name && param->value);
}

void sort_mml_command(struct mml_command *mml, int params, struct mml_param param[]) {
	int p;

	for (p = 0; p < mml->params; p++) {
		int q;

		if (!mml_param_valid(&mml->param[p])) continue;

		for (q = 0; q < params; q++) {
			if (!strcasecmp(mml->param[p].name, param[q].name)) {
				param[q].value = mml->param[p].value;
				break;
			}
		}
	}
}

void free_mml_command(struct mml_command *mml) {
	int p;

	for (p = 0; p < mml->params; p++) {
		if (mml->param[p].name) free(mml->param[p].name);
		if (mml->param[p].value) free(mml->param[p].value);
	}
	free(mml->command);
}

char *dequote(char *str) {
	int l = strlen(str);
	char *nstr = malloc(l + 1);
	char *p1, *p2;
	int inside = 0;

	nstr[l] = 0;

	for (p1 = str, p2 = nstr; *p1; p1++, p2++) {
		if (*p1 == '"') p2--, inside = !inside;
		else if (*p1 == ' ' && !inside) p2--;
		else *p2 = *p1;
	}
	*p2 = 0;

	return nstr;
}
