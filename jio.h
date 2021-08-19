// 
//             jio.h
//             
//          an IO library            
//
//
// (>) CC-BY 2021 Jonas S Karlsson
//
//         jsk@yesco.org
//

// misc system IO

#define TRACE(exprs...) if (trace) printf(exprs);

#define error(exp, exitcode, msg...) \
  do if (exp) { fprintf(stderr, "%%ERROR: %s:%d in %s(...)\n", __FILE__, __LINE__, __func__); fprintf(stderr, msg); fputc('\n', stderr); char*z=NULL; *z=42; kill(getpid(), SIGABRT); exit(exitcode); } while(0)

////////////////////////////////////////
// - ansi screen

extern int screen_rows, screen_cols;

void screen_init();

void reset();
void clear();
void clearend();
void cleareos();

void gotorc(int r, int c);
void cursoroff();
void cursoron();
void inverse(int on);
void fgcolor(int c);
void bgcolor(int c);
void savescreen();
void restorescreen();
void insertmode(int on);
void _color(int c);
void fg(int c);
void bg(int c);
int bold(int c /* 0-7 */);
int rgb(int r, int g, int b /* 0-5 */);
int gray(int c /* 0-7 */);
int RGB(int r, int g, int b /* 0-255 */);
void underline();
void end_underline();

// adjusted colors
void C(int n);
void B(int n);

// - higher level colors
enum color{black, red, green, yellow, blue, magnenta, cyan, white, none};

extern enum color _fg,  _bg;

////////////////////////////////////////
// - keyboard

// 'A' CTRL+'A' META+'A' FUNCTION+7
//    0- 31  :  ^@ ^A...
//   32-126  :  ' ' .. 'Z{|}~'
//  127      :  BACKSPACE
// -- Hi bit set == META
//    1- 12  :  F1 .. F12
//   32- 64  : (special keys)
//   65- 96  :  M-A .. M-Z
// -- 256 (9th bit set) = TERM
//   TERM+'A': UP
//        'B': DOWN
//        'C': RIGHT
//        'D': LEFT
//   TERM+'Z': DEL
enum { RETURN='M'-64, TAB='I'-64, ESC=27, BACKSPACE=127, CTRL=-64, META=128, TERM=256, FUNCTION=META, UP=TERM+'A', DOWN, RIGHT, LEFT, S_TAB=META+'Z', DEL=TERM+'3'};

int key();
char* keystring(int c);
void testkeys();

////////////////////////////////////////
// - files

int flines(FILE* f);
char* fgetline(FILE* f);
char* fgetlinenum(FILE* f, long line);

////////////////////////////////////////
// - strings

int endswith(char* s, char* end);

int isinsideutf8(int c);
int isstartutf8(int c);
int isutf8(int c);

int isfullwidth(int c);
int iszerowidth(int c);

////////////////////////////////////////
// Dynamic STRings (see Play/dstrncat.c)

#define DSTR_STEP 64

typedef struct dstr {
  int max;
  char s[0];
} dstr;

dstr* dstrncat(dstr* d, char* add, int n);
dstr* dstrprintf(dstr* d, char* fmt, ...);
