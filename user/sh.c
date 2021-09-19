// Shell.

#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "user/user.h"

#define MAX_ARGS 10

struct Command {
  // Execute cmd.  Never returns.
  void (*run)(struct Command *);
  // NUL-terminate all the counted strings.
  struct Command *(*nul_terminate)(struct Command *);
};

struct ExecCommand {
  struct Command base;  // parent
  char *argv[MAX_ARGS];
  char *eargv[MAX_ARGS];
};

struct RedirectCommand {
  struct Command base;  // parent
  struct Command *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct PipeCommand {
  struct Command base;  // parent
  struct Command *left;
  struct Command *right;
};

struct ListCommand {
  struct Command base;  // parent
  struct Command *left;
  struct Command *right;
};

struct BackgroundCommand {
  struct Command base;  // parent
  struct Command *cmd;
};

int get_input(char *, int);
int fork_or_die(void);  // Fork but panics on failure.
void panic(char *);
struct Command *Parser_parse(char *);

int main(void) {
  // Ensure that three file descriptors are open.
  int fd;
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  static char input[100];
  while (get_input(input, sizeof(input)) >= 0) {
    if (input[0] == 'c' && input[1] == 'd' && input[2] == ' ') {
      // Chdir must be called by the parent, not the child.
      char *directory = input + strlen("cd ");
      directory[strlen(directory) - 1] = 0;  // chop \n
      if (chdir(directory) < 0) fprintf(2, "cannot cd %s\n", directory);
      continue;
    }
    if (fork_or_die() == 0) {
      struct Command *cmd = Parser_parse(input);
      cmd->run(cmd);
    }
    wait(0);
  }
  exit(0);
}

int get_input(char *input, int size) {
  fprintf(2, "$ ");
  memset(input, 0, size);
  gets(input, size);
  if (input[0] == 0) return -1;  // EOF
  return 0;
}

void panic(char *s) {
  fprintf(2, "%s\n", s);
  exit(1);
}

int fork_or_die(void) {
  int pid = fork();
  if (pid == -1) panic("fork");
  return pid;
}

// PAGEBREAK!
// Constructors

void ExecCommand_run(struct Command *);
struct Command *ExecCommand_nul_terminate(struct Command *);

struct Command *ExecCommand_new(void) {
  struct ExecCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.run = ExecCommand_run;
  cmd->base.nul_terminate = ExecCommand_nul_terminate;
  return &(cmd->base);
}

void ExecCommand_run(struct Command *base) {
  struct ExecCommand *cmd = (struct ExecCommand *)base;
  if (cmd->argv[0] == 0) exit(1);
  exec(cmd->argv[0], cmd->argv);
  fprintf(2, "exec %s failed\n", cmd->argv[0]);
  exit(0);
}

struct Command *ExecCommand_nul_terminate(struct Command *base) {
  struct ExecCommand *cmd = (struct ExecCommand *)base;
  for (int i = 0; cmd->argv[i]; i++) *cmd->eargv[i] = 0;
  return &(cmd->base);
}

void RedirectCommand_run(struct Command *);
struct Command *RedirectCommand_nul_terminate(struct Command *);

struct Command *RedirectCommand_new(struct Command *sub_cmd, char *file,
                                    char *efile, int mode, int fd) {
  struct RedirectCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.run = RedirectCommand_run;
  cmd->base.nul_terminate = RedirectCommand_nul_terminate;
  cmd->cmd = sub_cmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return &(cmd->base);
}

void RedirectCommand_run(struct Command *base) {
  struct RedirectCommand *cmd = (struct RedirectCommand *)base;
  close(cmd->fd);
  if (open(cmd->file, cmd->mode) < 0) {
    fprintf(2, "open %s failed\n", cmd->file);
    exit(1);
  }
  cmd->cmd->run(cmd->cmd);
  exit(0);
}

struct Command *RedirectCommand_nul_terminate(struct Command *base) {
  struct RedirectCommand *cmd = (struct RedirectCommand *)base;
  cmd->cmd->nul_terminate(cmd->cmd);
  *cmd->efile = 0;
  return &(cmd->base);
}

void PipeCommand_run(struct Command *);
struct Command *PipeCommand_nul_terminate(struct Command *);

struct Command *PipeCommand_new(struct Command *left, struct Command *right) {
  struct PipeCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.run = PipeCommand_run;
  cmd->base.nul_terminate = PipeCommand_nul_terminate;
  cmd->left = left;
  cmd->right = right;
  return &(cmd->base);
}

void PipeCommand_run(struct Command *base) {
  int p[2];
  struct PipeCommand *cmd = (struct PipeCommand *)base;
  if (pipe(p) < 0) panic("pipe");
  if (fork_or_die() == 0) {
    close(1);
    dup(p[1]);
    close(p[0]);
    close(p[1]);
    cmd->left->run(cmd->left);
  }
  if (fork_or_die() == 0) {
    close(0);
    dup(p[0]);
    close(p[0]);
    close(p[1]);
    cmd->right->run(cmd->right);
  }
  close(p[0]);
  close(p[1]);
  wait(0);
  wait(0);
  exit(0);
}

struct Command *PipeCommand_nul_terminate(struct Command *base) {
  struct PipeCommand *cmd = (struct PipeCommand *)base;
  cmd->left->nul_terminate(cmd->left);
  cmd->right->nul_terminate(cmd->right);
  return &(cmd->base);
}

void ListCommand_run(struct Command *);
struct Command *ListCommand_nul_terminate(struct Command *);

struct Command *ListCommand_new(struct Command *left, struct Command *right) {
  struct ListCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.run = ListCommand_run;
  cmd->base.nul_terminate = ListCommand_nul_terminate;
  cmd->left = left;
  cmd->right = right;
  return &(cmd->base);
}

void ListCommand_run(struct Command *base) {
  struct ListCommand *cmd = (struct ListCommand *)base;
  if (fork_or_die() == 0) cmd->left->run(cmd->left);
  wait(0);
  cmd->right->run(cmd->right);
  exit(0);
}

struct Command *ListCommand_nul_terminate(struct Command *base) {
  struct ListCommand *cmd = (struct ListCommand *)base;
  cmd->left->nul_terminate(cmd->left);
  cmd->right->nul_terminate(cmd->right);
  return &(cmd->base);
}

void BackgroundCommand_run(struct Command *);
struct Command *BackgroundCommand_nul_terminate(struct Command *);

struct Command *BackgroundCommand_new(struct Command *sub_cmd) {
  struct BackgroundCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.run = BackgroundCommand_run;
  cmd->base.nul_terminate = BackgroundCommand_nul_terminate;
  cmd->cmd = sub_cmd;
  return &(cmd->base);
}

void BackgroundCommand_run(struct Command *base) {
  struct BackgroundCommand *cmd = (struct BackgroundCommand *)base;
  if (fork_or_die() == 0) cmd->cmd->run(cmd->cmd);
  exit(0);
}

struct Command *BackgroundCommand_nul_terminate(struct Command *base) {
  struct BackgroundCommand *cmd = (struct BackgroundCommand *)base;
  cmd->cmd->nul_terminate(cmd->cmd);
  return &(cmd->base);
}

// PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int get_token(char **ptr_string, char *string_end, char **ptr_token,
              char **ptr_token_end) {
  char *string = *ptr_string;
  while (string < string_end && strchr(whitespace, *string)) string++;
  if (ptr_token) *ptr_token = string;
  int token_kind = *string;
  switch (*string) {
    case 0:
      break;
    case '|':
    case '(':
    case ')':
    case ';':
    case '&':
    case '<':
      string++;
      break;
    case '>':
      string++;
      if (*string == '>') {
        token_kind = '+';
        string++;
      }
      break;
    default:
      token_kind = 'a';
      while (string < string_end && !strchr(whitespace, *string) &&
             !strchr(symbols, *string))
        string++;
      break;
  }
  if (ptr_token_end) *ptr_token_end = string;

  while (string < string_end && strchr(whitespace, *string)) string++;
  *ptr_string = string;
  return token_kind;
}

int peek(char **ptr_string, char *string_end, char *candidate_tokens) {
  char *string = *ptr_string;
  while (string < string_end && strchr(whitespace, *string)) string++;
  *ptr_string = string;
  return *string && strchr(candidate_tokens, *string);
}

struct Command *Parser_parse_line(char **, char *);
struct Command *Parser_parse_pipe(char **, char *);
struct Command *Parser_parse_exec(char **, char *);
struct Command *Parser_parse_redirects(struct Command *, char **, char *);
struct Command *Parser_parse_block(char **, char *);

struct Command *Parser_parse(char *string) {
  char *string_end = string + strlen(string);
  struct Command *cmd = Parser_parse_line(&string, string_end);
  peek(&string, string_end, "");  // skip_whitespace
  if (string != string_end) {
    fprintf(2, "leftovers: %s\n", string);
    panic("syntax");
  }
  cmd->nul_terminate(cmd);
  return cmd;
}

struct Command *Parser_parse_line(char **ptr_string, char *string_end) {
  struct Command *cmd = Parser_parse_pipe(ptr_string, string_end);
  while (peek(ptr_string, string_end, "&")) {
    get_token(ptr_string, string_end, 0, 0);
    cmd = BackgroundCommand_new(cmd);
  }
  if (peek(ptr_string, string_end, ";")) {
    get_token(ptr_string, string_end, 0, 0);
    cmd = ListCommand_new(cmd, Parser_parse_line(ptr_string, string_end));
  }
  return cmd;
}

struct Command *Parser_parse_pipe(char **ptr_string, char *string_end) {
  struct Command *cmd = Parser_parse_exec(ptr_string, string_end);
  if (peek(ptr_string, string_end, "|")) {
    get_token(ptr_string, string_end, 0, 0);
    cmd = PipeCommand_new(cmd, Parser_parse_pipe(ptr_string, string_end));
  }
  return cmd;
}

struct Command *Parser_parse_exec(char **ptr_string, char *string_end) {
  if (peek(ptr_string, string_end, "("))
    return Parser_parse_block(ptr_string, string_end);

  struct ExecCommand *exec_cmd = (struct ExecCommand *)ExecCommand_new();
  struct Command *cmd =
      Parser_parse_redirects(&(exec_cmd->base), ptr_string, string_end);
  int argc = 0;
  while (!peek(ptr_string, string_end, "|)&;")) {
    char *token, *token_end;
    int token_kind = get_token(ptr_string, string_end, &token, &token_end);
    if (token_kind == 0) break;
    if (token_kind != 'a') panic("syntax");
    exec_cmd->argv[argc] = token;
    exec_cmd->eargv[argc] = token_end;
    argc++;
    if (argc >= MAX_ARGS) panic("too many args");
    cmd = Parser_parse_redirects(cmd, ptr_string, string_end);
  }
  exec_cmd->argv[argc] = 0;
  exec_cmd->eargv[argc] = 0;
  return cmd;
}

struct Command *Parser_parse_block(char **ptr_string, char *string_end) {
  if (!peek(ptr_string, string_end, "(")) panic("parseblock");
  get_token(ptr_string, string_end, 0, 0);
  struct Command *cmd = Parser_parse_line(ptr_string, string_end);
  if (!peek(ptr_string, string_end, ")")) panic("syntax - missing )");
  get_token(ptr_string, string_end, 0, 0);
  cmd = Parser_parse_redirects(cmd, ptr_string, string_end);
  return cmd;
}

struct Command *Parser_parse_redirects(struct Command *cmd, char **ptr_string,
                                       char *string_end) {
  while (peek(ptr_string, string_end, "<>")) {
    int token_kind = get_token(ptr_string, string_end, 0, 0);
    char *token, *token_end;
    if (get_token(ptr_string, string_end, &token, &token_end) != 'a')
      panic("missing file for redirection");
    switch (token_kind) {
      case '<':
        cmd = RedirectCommand_new(cmd, token, token_end, O_RDONLY, 0);
        break;
      case '>':
        cmd = RedirectCommand_new(cmd, token, token_end,
                                  O_WRONLY | O_CREATE | O_TRUNC, 1);
        break;
      case '+':  // >>
        cmd =
            RedirectCommand_new(cmd, token, token_end, O_WRONLY | O_CREATE, 1);
        break;
    }
  }
  return cmd;
}
