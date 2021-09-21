// Shell.

#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "user/user.h"

#define MAX_ARGS 10

struct Command {
  // Execute cmd.  Never returns.
  void (*execute)(struct Command *);
};

int fork_or_panic(void);  // Fork but panics on failure.
void panic(char *);

int get_input(char *, int);
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
    if (fork_or_panic() == 0) {  // child
      struct Command *cmd = Parser_parse(input);
      cmd->execute(cmd);
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

int fork_or_panic(void) {
  int pid = fork();
  if (pid == -1) panic("fork");
  return pid;
}

// PAGEBREAK!
// Constructors

struct ExecCommand {
  struct Command base;  // parent
  char *argv[MAX_ARGS];
  char *argv_end[MAX_ARGS];
};

void ExecCommand_main(struct Command *base) {
  struct ExecCommand *cmd = (struct ExecCommand *)base;
  if (cmd->argv[0] == 0) exit(1);
  for (int i = 0; cmd->argv[i]; i++) *cmd->argv_end[i] = 0;  // nul terminate
  exec(cmd->argv[0], cmd->argv);
  fprintf(2, "exec %s failed\n", cmd->argv[0]);
  exit(0);
}

struct Command *ExecCommand_new(void) {
  struct ExecCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.execute = ExecCommand_main;
  return &(cmd->base);
}

struct RedirectCommand {
  struct Command base;  // parent
  struct Command *cmd;
  char *file_name;
  char *file_name_end;
  int mode;
  int fd;
};

void RedirectCommand_main(struct Command *base) {
  struct RedirectCommand *cmd = (struct RedirectCommand *)base;
  // re-open this mean that fd to file_name
  close(cmd->fd);
  *cmd->file_name_end = 0;  // nul terminate
  if (open(cmd->file_name, cmd->mode) < 0) {
    fprintf(2, "open %s failed\n", cmd->file_name);
    exit(1);
  }
  cmd->cmd->execute(cmd->cmd);
  exit(0);
}

struct Command *RedirectCommand_new(struct Command *sub_cmd, char *file_name,
                                    char *file_name_end, int mode, int fd) {
  struct RedirectCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.execute = RedirectCommand_main;
  cmd->cmd = sub_cmd;
  cmd->file_name = file_name;
  cmd->file_name_end = file_name_end;
  cmd->mode = mode;
  cmd->fd = fd;
  return &(cmd->base);
}

struct PipeCommand {
  struct Command base;  // parent
  struct Command *left;
  struct Command *right;
};

void PipeCommand_main(struct Command *base) {
  int p[2];
  struct PipeCommand *cmd = (struct PipeCommand *)base;
  if (pipe(p) < 0) panic("pipe");
  if (fork_or_panic() == 0) {  // child to left
    close(1 /* stdout */);
    dup(p[1]); /* pipe[1] to stdout(1) */
    close(p[0]);
    close(p[1]);
    cmd->left->execute(cmd->left);
  }
  if (fork_or_panic() == 0) {  // child to right
    close(0 /* stdin */);
    dup(p[0]); /* pipe[0] to stdin(0) */
    close(p[0]);
    close(p[1]);
    cmd->right->execute(cmd->right);
  }
  close(p[0]);
  close(p[1]);
  wait(0);
  wait(0);
  exit(0);
}

struct Command *PipeCommand_new(struct Command *left, struct Command *right) {
  struct PipeCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.execute = PipeCommand_main;
  cmd->left = left;
  cmd->right = right;
  return &(cmd->base);
}

struct ListCommand {
  struct Command base;  // parent
  struct Command *left;
  struct Command *right;
};

void ListCommand_main(struct Command *base) {
  struct ListCommand *cmd = (struct ListCommand *)base;
  if (fork_or_panic() == 0) cmd->left->execute(cmd->left);  // child
  wait(0);
  cmd->right->execute(cmd->right);
  exit(0);
}

struct Command *ListCommand_new(struct Command *left, struct Command *right) {
  struct ListCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.execute = ListCommand_main;
  cmd->left = left;
  cmd->right = right;
  return &(cmd->base);
}

struct BackgroundCommand {
  struct Command base;  // parent
  struct Command *cmd;
};

void BackgroundCommand_main(struct Command *base) {
  struct BackgroundCommand *cmd = (struct BackgroundCommand *)base;
  if (fork_or_panic() == 0) cmd->cmd->execute(cmd->cmd);  // child
  exit(0);
}

struct Command *BackgroundCommand_new(struct Command *sub_cmd) {
  struct BackgroundCommand *cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->base.execute = BackgroundCommand_main;
  cmd->cmd = sub_cmd;
  return &(cmd->base);
}

// PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

void skip_whitespaces(char **ptr_string, char *string_end) {
  char *string = *ptr_string;
  while (string < string_end && strchr(whitespace, *string)) string++;
  *ptr_string = string;
  return;
}

void skip_argument(char **ptr_string, char *string_end) {
  char *string = *ptr_string;
  while (string < string_end && !strchr(whitespace, *string) &&
         !strchr(symbols, *string))
    string++;
  *ptr_string = string;
  return;
}

int get_token(char **ptr_string, char *string_end, char **ptr_token,
              char **ptr_token_end) {
  char *string = *ptr_string;
  skip_whitespaces(&string, string_end);
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
      string++;  // skip_symbol
      break;
    case '>':
      string++;  // skip_symbol
      if (*string == '>') {
        token_kind = '+';  // >>
        string++;          // skip_symbol
      }
      break;
    default:
      token_kind = 'a';
      skip_argument(&string, string_end);
      break;
  }
  if (ptr_token_end) *ptr_token_end = string;

  skip_whitespaces(&string, string_end);
  *ptr_string = string;
  return token_kind;
}

int move_next_token_and_check(char **ptr_string, char *string_end,
                              char *check_tokens) {
  skip_whitespaces(ptr_string, string_end);
  return **ptr_string && strchr(check_tokens, **ptr_string);
}

struct Command *Parser_parse_line(char **, char *);
struct Command *Parser_parse_pipe(char **, char *);
struct Command *Parser_parse_exec(char **, char *);
struct Command *Parser_parse_redirects(struct Command *, char **, char *);
struct Command *Parser_parse_block(char **, char *);

struct Command *Parser_parse(char *string) {
  char *string_end = string + strlen(string);
  struct Command *cmd = Parser_parse_line(&string, string_end);
  skip_whitespaces(&string, string_end);
  if (string != string_end) {
    fprintf(2, "leftovers: %s\n", string);
    panic("syntax");
  }
  return cmd;
}

struct Command *Parser_parse_line(char **ptr_string, char *string_end) {
  struct Command *cmd = Parser_parse_pipe(ptr_string, string_end);
  while (move_next_token_and_check(ptr_string, string_end, "&")) {
    get_token(ptr_string, string_end, 0, 0);
    cmd = BackgroundCommand_new(cmd);
  }
  if (move_next_token_and_check(ptr_string, string_end, ";")) {
    get_token(ptr_string, string_end, 0, 0);
    cmd = ListCommand_new(cmd, Parser_parse_line(ptr_string, string_end));
  }
  return cmd;
}

struct Command *Parser_parse_pipe(char **ptr_string, char *string_end) {
  struct Command *cmd = Parser_parse_exec(ptr_string, string_end);
  if (move_next_token_and_check(ptr_string, string_end, "|")) {
    get_token(ptr_string, string_end, 0, 0);
    cmd = PipeCommand_new(cmd, Parser_parse_pipe(ptr_string, string_end));
  }
  return cmd;
}

struct Command *Parser_parse_exec(char **ptr_string, char *string_end) {
  if (move_next_token_and_check(ptr_string, string_end, "("))
    return Parser_parse_block(ptr_string, string_end);

  struct ExecCommand *exec_cmd = (struct ExecCommand *)ExecCommand_new();
  struct Command *cmd =
      Parser_parse_redirects(&(exec_cmd->base), ptr_string, string_end);
  int argc = 0;
  while (!move_next_token_and_check(ptr_string, string_end, "|)&;")) {
    char *token, *token_end;
    int token_kind = get_token(ptr_string, string_end, &token, &token_end);
    if (token_kind == 0) break;
    if (token_kind != 'a') panic("syntax");
    exec_cmd->argv[argc] = token;
    exec_cmd->argv_end[argc] = token_end;
    argc++;
    if (argc >= MAX_ARGS) panic("too many args");
    cmd = Parser_parse_redirects(cmd, ptr_string, string_end);
  }
  exec_cmd->argv[argc] = exec_cmd->argv_end[argc] = 0;
  return cmd;
}

struct Command *Parser_parse_block(char **ptr_string, char *string_end) {
  if (!move_next_token_and_check(ptr_string, string_end, "("))
    panic("parseblock");
  get_token(ptr_string, string_end, 0, 0);
  struct Command *cmd = Parser_parse_line(ptr_string, string_end);
  if (!move_next_token_and_check(ptr_string, string_end, ")"))
    panic("syntax - missing )");
  get_token(ptr_string, string_end, 0, 0);
  cmd = Parser_parse_redirects(cmd, ptr_string, string_end);
  return cmd;
}

struct Command *Parser_parse_redirects(struct Command *cmd, char **ptr_string,
                                       char *string_end) {
  while (move_next_token_and_check(ptr_string, string_end, "<>")) {
    int token_kind = get_token(ptr_string, string_end, 0, 0);
    char *token, *token_end;
    if (get_token(ptr_string, string_end, &token, &token_end) != 'a')
      panic("missing file for redirection");
    switch (token_kind) {
      case '<':
        cmd =
            RedirectCommand_new(cmd, token, token_end, O_RDONLY, 0 /* stdin */);
        break;
      case '>':
        cmd =
            RedirectCommand_new(cmd, token, token_end,
                                O_WRONLY | O_CREATE | O_TRUNC, 1 /* stdout */);
        break;
      case '+':  // >>
        cmd = RedirectCommand_new(cmd, token, token_end, O_WRONLY | O_CREATE,
                                  1 /* stdout */);
        break;
    }
  }
  return cmd;
}
