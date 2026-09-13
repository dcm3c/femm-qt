/*
** $Id: lua.c,v 1.55 2000/10/20 16:36:32 roberto Exp $
** Lua stand-alone interpreter
** See Copyright Notice in lua.h
*/


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "lua.h"
#include "utf8_io.h"   /* femm_utf8_args: decode a Unicode command line on Windows */

#include "luadebug.h"
#include "lualib.h"
#include "luasocket.h"


static lua_State *L = NULL;


#ifndef PROMPT
#define PROMPT		"> "
#endif

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif


/*
** global options
*/
struct Options {
  int toclose;
  int stacksize;
};


typedef void (*handler)(int);  /* type for signal actions */

static void laction (int i);


static lua_Hook old_linehook = NULL;
static lua_Hook old_callhook = NULL;


static void userinit (void) {
  lua_baselibopen(L);
  lua_iolibopen(L);
  lua_strlibopen(L);
  lua_mathlibopen(L);
  lua_dblibopen(L);
  lua_socketlibopen(L);
}


static handler lreset (void) {
  return signal(SIGINT, laction);
}


static void lstop (void) {
  lua_setlinehook(L, old_linehook);
  lua_setcallhook(L, old_callhook);
  lreset();
  lua_error(L, "interrupted!");
}


static void laction (int i) {
  (void)i;  /* to avoid warnings */
  signal(SIGINT, SIG_DFL); /* if another SIGINT happens before lstop,
                              terminate process (default action) */
  old_linehook = lua_setlinehook(L, (lua_Hook)lstop);
  old_callhook = lua_setcallhook(L, (lua_Hook)lstop);
}


static int ldo (int (*f)(lua_State *l, const char *), const char *name) {
  int res;
  handler h = lreset();
  int top = lua_gettop(L);
  res = f(L, name);  /* dostring | dofile */
  lua_settop(L, top);  /* remove eventual results */
  signal(SIGINT, h);  /* restore old action */
  /* Lua gives no message in such cases, so lua.c provides one */
  if (res == LUA_ERRMEM) {
    fprintf(stderr, "lua: memory allocation error\n");
  }
  else if (res == LUA_ERRERR)
    fprintf(stderr, "lua: error in error message\n");
  return res;
}


static void print_message (void) {
  fprintf(stderr,
  "usage: lua [options].  Available options are:\n"
  "  -        execute stdin as a file\n"
  "  -c       close Lua when exiting\n"
  "  -e stat  execute string `stat'\n"
  "  -f name  execute file `name' with remaining arguments in table `arg'\n"
  "  -i       enter interactive mode with prompt\n"
  "  -q       enter interactive mode without prompt\n"
  "  -sNUM    set stack size to NUM (must be the first option)\n"
  "  -v       print version information\n"
  "  a=b      set global `a' to string `b'\n"
  "  name     execute file `name'\n"
);
}


static void print_version (void) {
  printf("%.80s  %.80s\n", LUA_VERSION, LUA_COPYRIGHT);
}


static void assign (char *arg) {
  char *eq = strchr(arg, '=');
  *eq = '\0';  /* spilt `arg' in two strings (name & value) */
  lua_pushstring(L, eq+1);
  lua_setglobal(L, arg);
}


static void getargs (char *argv[]) {
  int i;
  lua_newtable(L);
  for (i=0; argv[i]; i++) {
    /* arg[i] = argv[i] */
    lua_pushnumber(L, i);
    lua_pushstring(L, argv[i]);
    lua_settable(L, -3);
  }
  /* arg.n = maximum index in table `arg' */
  lua_pushstring(L, "n");
  lua_pushnumber(L, i-1);
  lua_settable(L, -3);
}


static int l_getargs (lua_State *l) {
  char **argv = (char **)lua_touserdata(l, -1);
  getargs(argv);
  return 1;
}


/*
** `arg' is two things at once, and FEMM needs both.
**
** This interpreter binds the script's command-line arguments to the global
** `arg', which is the standard Lua 4 convention -- and in THIS build `arg' is
** also the complex phase function from the maths library, because lua4
** compiles CComplex as its number type. Setting the table plainly therefore
** destroyed arg(z) for the whole run.
**
** That is a FEMM 4.2 compatibility break, not a curiosity. In FEMM 4.2 scripts
** ran inside femm's own embedded interpreter, which never set `arg', so arg(z)
** worked. Through femm-lua a script is run with -f, so it did not, and a 4.2
** script asking for the phase of a complex result got "attempt to call global
** `arg' (a table value)" -- from a line that had been correct for 20 years.
**
** Lua 4 can have both: a tag method for the "function" event makes a non-
** function value callable. So the table is given its own tag, and calling it
** forwards to whatever `arg' was before -- the maths function -- with the
** table itself dropped from the arguments.
**
** Stack on entry, per this interpreter's C-closure convention (see l_getargs
** above, which reads its upvalue at -1): the called value, then the call's own
** arguments, then the upvalue.
*/
static int l_arg_call (lua_State *l) {
  int top = lua_gettop(l);      /* [1]=the arg table, [2..top-1]=args, [top]=fn */
  int i;
  lua_pushvalue(l, top);        /* the original arg(), to be called */
  for (i = 2; i < top; i++)
    lua_pushvalue(l, i);
  lua_call(l, top - 2, 1);
  return 1;
}


/*
** Bind `arg' to the argument table without losing arg() the function.
** If `arg' was not a function to begin with -- a build without the complex
** maths library -- this degrades to exactly what it replaced.
*/
static void setargs_global (void) {
  lua_getglobal(L, "arg");
  if (lua_isfunction(L, -1)) {
    int tag = lua_newtag(L);
    lua_pushcclosure(L, l_arg_call, 1);   /* consumes the old arg() */
    lua_settagmethod(L, tag, "function");
    lua_settag(L, tag);                   /* tag the table on the stack top */
  }
  else {
    lua_pop(L, 1);                        /* nothing worth preserving */
  }
  lua_setglobal(L, "arg");
}


static int file_input (const char *argv) {
  int result = ldo(lua_dofile, argv);
  if (result) {
    if (result == LUA_ERRFILE) {
      fprintf(stderr, "lua: cannot execute file ");
      perror(argv);
    }
    return EXIT_FAILURE;
  }
  else
    return EXIT_SUCCESS;
}


/* maximum length of an input string */
#ifndef MAXINPUT
#define MAXINPUT	BUFSIZ
#endif

static void manual_input (int version, int prompt) {
  int cont = 1;
  if (version) print_version();
  while (cont) {
    char buffer[MAXINPUT];
    int i = 0;
    if (prompt) {
      const char *s;
      lua_getglobal(L, "_PROMPT");
      s = lua_tostring(L, -1);
      if (!s || s[0] == '\0') s = PROMPT;
      fprintf(stdout, "%s", s);
      fflush(stdout);
      lua_pop(L, 1);  /* remove global */
    }
    for(;;) {
      int c = getchar();
      if (c == EOF) {
        cont = 0;
        break;
      }
      else if (c == '\n') {
        if (i>0 && buffer[i-1] == '\\')
          buffer[i-1] = '\n';
        else break;
      }
      else if (i >= MAXINPUT-1) {
        fprintf(stderr, "lua: input line too long\n");
        break;
      }
      else buffer[i++] = (char)c;
    }
    buffer[i] = '\0';
    ldo(lua_dostring, buffer);
    lua_settop(L, 0);  /* remove eventual results */
  }
  printf("\n");
}


static int handle_argv (char *argv[], struct Options *opt) {
  if (opt->stacksize > 0) argv++;  /* skip option `-s' (if present) */
  if (*argv == NULL) {  /* no more arguments? */
    if (isatty(0)) {
      manual_input(1, 1);
    }
    else
      ldo(lua_dofile, NULL);  /* executes stdin as a file */
  }
  else {  /* other arguments; loop over them */
    int i;
    for (i = 0; argv[i] != NULL; i++) {
      if (argv[i][0] != '-') {  /* not an option? */
        if (strchr(argv[i], '='))
          assign(argv[i]);
        else
          if (file_input(argv[i]) != EXIT_SUCCESS)
            return EXIT_FAILURE;  /* stop if file fails */
        }
        else switch (argv[i][1]) {  /* option */
          case 0: {
            ldo(lua_dofile, NULL);  /* executes stdin as a file */
            break;
          }
          case 'i': {
            manual_input(0, 1);
            break;
          }
          case 'q': {
            manual_input(0, 0);
            break;
          }
          case 'c': {
            opt->toclose = 1;
            break;
          }
          case 'v': {
            print_version();
            break;
          }
          case 'e': {
            i++;
            if (argv[i] == NULL) {
              print_message();
              return EXIT_FAILURE;
            }
            if (ldo(lua_dostring, argv[i]) != 0) {
              fprintf(stderr, "lua: error running argument `%.99s'\n", argv[i]);
              return EXIT_FAILURE;
            }
            break;
          }
          case 'f': {
            i++;
            if (argv[i] == NULL) {
              print_message();
              return EXIT_FAILURE;
            }
            getargs(argv+i);  /* collect remaining arguments */
            setargs_global(); /* ...without destroying arg() the function */
            return file_input(argv[i]);  /* stop scanning arguments */
          }
          case 's': {
            fprintf(stderr, "lua: stack size (`-s') must be the first option\n");
            return EXIT_FAILURE;
          }
          default: {
            print_message();
            return EXIT_FAILURE;
          }
        }
    }
  }
  return EXIT_SUCCESS;
}


static void getstacksize (int argc, char *argv[], struct Options *opt) {
  if (argc >= 2 && argv[1][0] == '-' && argv[1][1] == 's') {
    int stacksize = atoi(&argv[1][2]);
    if (stacksize <= 0) {
      fprintf(stderr, "lua: invalid stack size ('%.20s')\n", &argv[1][2]);
      exit(EXIT_FAILURE);
    }
    opt->stacksize = stacksize;
  }
  else
    opt->stacksize = 0;  /* no stack size */
}


static void register_getargs (char *argv[]) {
  lua_pushuserdata(L, argv);
  lua_pushcclosure(L, l_getargs, 1);
  lua_setglobal(L, "getargs");
}


int main (int argc, char *argv[]) {
  struct Options opt;
  int status;
#ifdef _WIN32
  /* Decode the Windows command line to UTF-8 so a Unicode script path or name
     passed on the command line opens correctly. The CRT hands a narrow argv in
     the ANSI code page, which mangles anything outside it (e.g. an umlaut in a
     folder). femm_utf8_args re-fetches the wide command line and transcodes.
     The vectors live for all of main(), so the char* views stay valid (they are
     also stashed by register_getargs for the Lua `arg` table). */
  std::vector<std::string> uargs = femm_utf8_args(argc, argv);
  std::vector<char *> uargv;
  for (size_t i = 0; i < uargs.size(); ++i)
    uargv.push_back(const_cast<char *>(uargs[i].c_str()));
  uargv.push_back(NULL);
  argc = (int)uargs.size();
  argv = uargv.data();
#endif
  opt.toclose = 0;
  getstacksize(argc, argv, &opt);  /* handle option `-s' */
  L = lua_open(opt.stacksize);  /* create state */
  userinit();  /* open libraries */
  register_getargs(argv);  /* create `getargs' function */
  status = handle_argv(argv+1, &opt);
  if (opt.toclose)
    lua_close(L);
  return status;
}

