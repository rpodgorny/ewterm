#ifndef EW__MML_H
#define EW__MML_H

struct mml_param {
  char *name;
  char *value;
};

struct mml_command {
  char *command;
  int params;
  struct mml_param param[256];
};

struct mml_command *parse_mml_command(char *);
void free_mml_command(struct mml_command *);

void sort_mml_command(struct mml_command *, int, struct mml_param[]);
int mml_param_valid(struct mml_param *);

char *dequote(char *);

#endif
