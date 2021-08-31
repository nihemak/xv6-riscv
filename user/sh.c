// Shell.

#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "user/user.h"

// Parsed command representation
#define CMD_TYPE_EXEC 1
#define CMD_TYPE_REDIRECT 2
#define CMD_TYPE_PIPE 3
#define CMD_TYPE_LIST 4
#define CMD_TYPE_BACKGROUND 5

#define MAXARGS 10

struct cmd {
  int type;
};

struct exec_cmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redirect_cmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipe_cmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct list_cmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct background_cmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char *);
struct cmd *parse_cmd(char *);

void run_exec_cmd(struct cmd *);
void run_redirect_cmd(struct cmd *);
void run_list_cmd(struct cmd *);
void run_pipe_cmd(struct cmd *);
void run_background_cmd(struct cmd *);

// Execute cmd.  Never returns.
void run_cmd(struct cmd *cmd) {
  if (cmd == 0) exit(1);

  switch (cmd->type) {
    case CMD_TYPE_EXEC:
      run_exec_cmd(cmd);
      break;
    case CMD_TYPE_REDIRECT:
      run_redirect_cmd(cmd);
      break;
    case CMD_TYPE_LIST:
      run_list_cmd(cmd);
      break;
    case CMD_TYPE_PIPE:
      run_pipe_cmd(cmd);
      break;
    case CMD_TYPE_BACKGROUND:
      run_background_cmd(cmd);
      break;
    default:
      panic("runcmd");
  }
  exit(0);
}

void run_exec_cmd(struct cmd *cmd) {
  struct exec_cmd *ecmd;
  ecmd = (struct exec_cmd *)cmd;
  if (ecmd->argv[0] == 0) exit(1);
  exec(ecmd->argv[0], ecmd->argv);
  fprintf(2, "exec %s failed\n", ecmd->argv[0]);
}

void run_redirect_cmd(struct cmd *cmd) {
  struct redirect_cmd *rcmd;
  rcmd = (struct redirect_cmd *)cmd;
  close(rcmd->fd);
  if (open(rcmd->file, rcmd->mode) < 0) {
    fprintf(2, "open %s failed\n", rcmd->file);
    exit(1);
  }
  run_cmd(rcmd->cmd);
}

void run_list_cmd(struct cmd *cmd) {
  struct list_cmd *lcmd;
  lcmd = (struct list_cmd *)cmd;
  if (fork1() == 0) run_cmd(lcmd->left);
  wait(0);
  run_cmd(lcmd->right);
}

void run_pipe_cmd(struct cmd *cmd) {
  int p[2];
  struct pipe_cmd *pcmd;
  pcmd = (struct pipe_cmd *)cmd;
  if (pipe(p) < 0) panic("pipe");
  if (fork1() == 0) {
    close(1);
    dup(p[1]);
    close(p[0]);
    close(p[1]);
    run_cmd(pcmd->left);
  }
  if (fork1() == 0) {
    close(0);
    dup(p[0]);
    close(p[0]);
    close(p[1]);
    run_cmd(pcmd->right);
  }
  close(p[0]);
  close(p[1]);
  wait(0);
  wait(0);
}

void run_background_cmd(struct cmd *cmd) {
  struct background_cmd *bcmd;
  bcmd = (struct background_cmd *)cmd;
  if (fork1() == 0) run_cmd(bcmd->cmd);
}

int get_cmd(char *buf, int nbuf) {
  fprintf(2, "$ ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if (buf[0] == 0) return -1;  // EOF
  return 0;
}

int main(void) {
  static char input[100];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (get_cmd(input, sizeof(input)) >= 0) {
    if (input[0] == 'c' && input[1] == 'd' && input[2] == ' ') {
      // Chdir must be called by the parent, not the child.
      char *dir = input + 3;
      dir[strlen(dir) - 1] = 0;  // chop \n
      if (chdir(dir) < 0) fprintf(2, "cannot cd %s\n", dir);
      continue;
    }
    if (fork1() == 0) run_cmd(parse_cmd(input));
    wait(0);
  }
  exit(0);
}

void panic(char *s) {
  fprintf(2, "%s\n", s);
  exit(1);
}

int fork1(void) {
  int pid;

  pid = fork();
  if (pid == -1) panic("fork");
  return pid;
}

// PAGEBREAK!
// Constructors

struct cmd *exec_cmd(void) {
  struct exec_cmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = CMD_TYPE_EXEC;
  return (struct cmd *)cmd;
}

struct cmd *redirect_cmd(struct cmd *subcmd, char *file, char *efile, int mode,
                         int fd) {
  struct redirect_cmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = CMD_TYPE_REDIRECT;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *pipe_cmd(struct cmd *left, struct cmd *right) {
  struct pipe_cmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = CMD_TYPE_PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *list_cmd(struct cmd *left, struct cmd *right) {
  struct list_cmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = CMD_TYPE_LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *background_cmd(struct cmd *subcmd) {
  struct background_cmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = CMD_TYPE_BACKGROUND;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
// PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int get_token(char **ps, char *es, char **q, char **eq) {
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s)) s++;
  if (q) *q = s;
  ret = *s;
  switch (*s) {
    case 0:
      break;
    case '|':
    case '(':
    case ')':
    case ';':
    case '&':
    case '<':
      s++;
      break;
    case '>':
      s++;
      if (*s == '>') {
        ret = '+';
        s++;
      }
      break;
    default:
      ret = 'a';
      while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s)) s++;
      break;
  }
  if (eq) *eq = s;

  while (s < es && strchr(whitespace, *s)) s++;
  *ps = s;
  return ret;
}

int peek(char **ps, char *es, char *toks) {
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s)) s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parse_line(char **, char *);
struct cmd *parse_pipe(char **, char *);
struct cmd *parse_exec(char **, char *);
struct cmd *nul_terminate(struct cmd *);

struct cmd *parse_cmd(char *s) {
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parse_line(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nul_terminate(cmd);
  return cmd;
}

struct cmd *parse_line(char **ps, char *es) {
  struct cmd *cmd;

  cmd = parse_pipe(ps, es);
  while (peek(ps, es, "&")) {
    get_token(ps, es, 0, 0);
    cmd = background_cmd(cmd);
  }
  if (peek(ps, es, ";")) {
    get_token(ps, es, 0, 0);
    cmd = list_cmd(cmd, parse_line(ps, es));
  }
  return cmd;
}

struct cmd *parse_pipe(char **ps, char *es) {
  struct cmd *cmd;

  cmd = parse_exec(ps, es);
  if (peek(ps, es, "|")) {
    get_token(ps, es, 0, 0);
    cmd = pipe_cmd(cmd, parse_pipe(ps, es));
  }
  return cmd;
}

struct cmd *parse_redirects(struct cmd *cmd, char **ps, char *es) {
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = get_token(ps, es, 0, 0);
    if (get_token(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok) {
      case '<':
        cmd = redirect_cmd(cmd, q, eq, O_RDONLY, 0);
        break;
      case '>':
        cmd = redirect_cmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
        break;
      case '+':  // >>
        cmd = redirect_cmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
        break;
    }
  }
  return cmd;
}

struct cmd *parse_block(char **ps, char *es) {
  struct cmd *cmd;

  if (!peek(ps, es, "(")) panic("parseblock");
  get_token(ps, es, 0, 0);
  cmd = parse_line(ps, es);
  if (!peek(ps, es, ")")) panic("syntax - missing )");
  get_token(ps, es, 0, 0);
  cmd = parse_redirects(cmd, ps, es);
  return cmd;
}

struct cmd *parse_exec(char **ps, char *es) {
  char *q, *eq;
  int tok, argc;
  struct exec_cmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "(")) return parse_block(ps, es);

  ret = exec_cmd();
  cmd = (struct exec_cmd *)ret;

  argc = 0;
  ret = parse_redirects(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    if ((tok = get_token(ps, es, &q, &eq)) == 0) break;
    if (tok != 'a') panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS) panic("too many args");
    ret = parse_redirects(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *nul_terminate(struct cmd *cmd) {
  int i;
  struct background_cmd *bcmd;
  struct exec_cmd *ecmd;
  struct list_cmd *lcmd;
  struct pipe_cmd *pcmd;
  struct redirect_cmd *rcmd;

  if (cmd == 0) return 0;

  switch (cmd->type) {
    case CMD_TYPE_EXEC:
      ecmd = (struct exec_cmd *)cmd;
      for (i = 0; ecmd->argv[i]; i++) *ecmd->eargv[i] = 0;
      break;

    case CMD_TYPE_REDIRECT:
      rcmd = (struct redirect_cmd *)cmd;
      nul_terminate(rcmd->cmd);
      *rcmd->efile = 0;
      break;

    case CMD_TYPE_PIPE:
      pcmd = (struct pipe_cmd *)cmd;
      nul_terminate(pcmd->left);
      nul_terminate(pcmd->right);
      break;

    case CMD_TYPE_LIST:
      lcmd = (struct list_cmd *)cmd;
      nul_terminate(lcmd->left);
      nul_terminate(lcmd->right);
      break;

    case CMD_TYPE_BACKGROUND:
      bcmd = (struct background_cmd *)cmd;
      nul_terminate(bcmd->cmd);
      break;
  }
  return cmd;
}
