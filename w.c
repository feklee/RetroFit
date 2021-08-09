//
//               ./w 
//
//    a minimal web-browser in C
//
//
//  (>) CC-BY 2021 Jonas S Karlsson
//
//          jsk@yesco.org
//

// various debug output
int trace= 0, trace_content= 0;

#define TRACE(exprs...) if (trace) printf(exprs);

// general formatting
const int rmargin= 1; // 1 or more (no 0)
const int lmargin= 2;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>

#include <unistr.h>
#include <uniname.h>
#include <wchar.h>

int rows= 24, cols= 80;

// https://stackoverflow.com/questions/1022957/getting-terminal-width-in-c
void getsize() {
  struct winsize w;
  ioctl(0, TIOCGWINSZ, &w);
  rows = w.ws_row;
  cols = w.ws_col;
  TRACE(printf("\t\trows=%d cols=%d\n", rows, cols));
}

// Dynamic STRings (see Play/dstrncat.c)
#define DSTR_STEP 64

typedef struct dstr {
  int max;
  char s[0];
} dstr;

// Usage: d= dstrncat(d, "foo", -1);
//   d, add: can be NULL (both->alloc N)
//   n: -1) strlen(add), or copy N chars
// 
// Returns: d or newly allocated dstr
dstr* dstrncat(dstr* d, char* add, int n) {
  int len= d ? strlen(d->s) : 0, max= d ? d->max : 0;
  n= (n<0 && add)? strlen(add) : n;
  if (!d || len+n+1>max) {
    max= ((max+n)/DSTR_STEP + 1)*DSTR_STEP*13/10;
    d= realloc(d, sizeof(dstr)+max);
    d->s[len]= 0; // if new allocated
  }
  d->max= max;
  if (add) strncat(d->s, add, n);
  return d;
}

// w3c colors
// - https://www.w3.org/wiki/CSS/Properties/color/keywords

// ansi
enum color{black, red, green, yellow, blue, magnenta, cyan, white, none};
enum color _fg= black, _bg= white;

void _color(c) {
  if (c >= 0) {
    printf("5;%dm", c);
  } else {
    c= -c;
    int b= c&0xff, g= (c>>8)&0xff, r=(c>>16);
    printf("2;%d;%d;%dm", r, g, b);
  }
}

void fg(int c) { printf("\e[38;"); _color(c); _fg= c; }
void bg(int c) { printf("\e[48;"); _color(c); _bg= c; }

int bold(int c /* 0-7 */) { return c+8; }
int rgb(int r, int g, int b /* 0-5 */) { return 0x10+ 36*r + 6*g + b; }
int gray(int c /* 0-7 */) { return 0xe8+  c; }
int RGB(int r, int g, int b /* 0-255 */) { return -(r*256+g)*256+b; }
void underline() { printf("\e[4m"); }
void end_underline() { printf("\e[24m"); }

// adjusted colors
void C(int n) { fg(n + 8*(n!=0 && n<8)); }
void B(int n) { bg(n); }

// hard space, hard newline
#define HS -32
#define HNL -10
#define SNL -11

// -- groups of tags according to format
#define SKIP " script head "

#define NL " br hr pre code h1 h2 h3 h4 h5 h6 h7 h8 h9 blockquote li dl dt dd table tr noscript address tbody "
#define XNL " /ul /ol hr tbody "
#define HD " h1 h2 h3 h4 h5 h6 "
//#define CENTER " center caption " // TODO

#define BD " b strong "
#define IT " i em caption "

#define HL " u s q cite ins del noscript abbr acronym "

#define PR " pre code "
#define TT " bdo kbd dfn samp var tt "

// thead tfoot tbody /tbody-optional!
// td/th: rowspan colspan

#define FM " form input textarea select option optgroup button label fieldset legend "

// attribute captures
#define TCONT " a th td "
#define TATTR " img a base iframe frame colgroup "

#define ATTR " href src alt aria-label title aria-hidden name id type value size accesskey align valign span "

// -- template for getting HTML
// TODO: use "tee" to save to cache
// - https://www.gnu.org/software/coreutils/manual/html_node/tee-invocation.html#tee-invocation
// TODO: read headers from wget/curl and show loading status
#define WGET "wget -O - \"%s\" 2>/dev/null"

// generally used for parse() of symbols
typedef char TAG[32];

// - HTML Name Entities
#include "entities.h"

// decodes a HTML Named Entity to UTF-8
// name must be "&ID" (';' is optional)
//   - &amp;    - HTML named Entity
//   - &#4711;  - decimal numbered char
//   - &#xff21; - fullwidth 'A'
// Return unicode string 1-2 bytes, or NULL
// the string is static, so use it fast!
char* decode_entity(char* name) {
  TAG fnd= {0};
  strcpy(fnd, name);

  // return pointer to fixed static string
  static char result[2*4*2];
  memset(result, 0, sizeof(result));

  // numbered entity?
  uint32_t c = 0;
  assert(sizeof(int)==4);
  if (sscanf(fnd, "&#x%x;", &c) ||
      sscanf(fnd, "&#%i;", &c)) {
    size_t len=sizeof(result)-1;
    return u32_to_u8(&c, 1, result, &len);
  }

  // search for '&name; '
  fnd[strlen(fnd)]= ' ';
  TRACE("[>> \"%s\" <<]", fnd);

  char* m= strcasestr(ENTITIES, fnd);
  if (!m) return NULL;
  
  // skip '&name;? '
  m+= strlen(name)+1;
  char* p= result;
  // copy first char at least (might be '&')
  do {
    *p++ = *m++;
  } while ('&' != *m); // until '&...';
  TRACE("{$s}", result);
  return result;
}

// screen state
int _pre= 0, _ws= 1, _nl= 1, _tb= 0, _skip= 0, _indent= lmargin;
int _curx= 0, _cury= 0, _fullwidth= 0, _capture= 0, _table= 0;

void cls() {
  printf("\e[H[2J[3J");
  getsize();
  _cury= 0; _curx= 0; _ws= 1; _nl= 1;
}

void nl();
void indent();

// track visible chars printed
void inx() {
  _curx++; _nl= 0;
  if (!_pre && _curx+rmargin == cols) nl();
}

// _pc buffers word, and word-wrap+ENTITIES
#define WORDLEN 10
char word[WORDLEN+1] = {0};
int _overflow= 0;

void _pc(int c) {
  // TODO: break on '&' this doesn't work:
  //if (strlen(word) && (c<=' ' || c==';' || c=='\n' || c=='\r' || c=='\t' || c=='&')) {

  // output word (if at break char)
  if (c<=' ' || c==';' || isspace(c)) {
    // html entity?
    char* e_result= NULL;
    if (word[0]=='&') {
      if (c==';') word[strlen(word)]= c;
      e_result= decode_entity(word);
    }
    if (e_result) {
      printf("%s", e_result);
      //if (c!=';') putchar(c);
    } else {
      // no entity, just output word
      printf("%s", word);
      if (c>=0) putchar(c);
    }
    memset(word, 0, sizeof(word));
    _overflow= 0;
    //_ws= (c==' '||c=='\t'); // TODO: doesn't matter!

  } else if (_overflow) {
    if (_curx+rmargin+1 >= cols) {
      putchar('-');
      nl();
    }
    indent();
    putchar(c); inx();

  } else { // add char to word
    int l= strlen(word);
    if (l>=WORDLEN) {
      _overflow= 1;
      // flush
      for(int i=0; i<strlen(word); i++)
        putchar(word[i]);

      putchar(c); inx();
      memset(word, 0, sizeof(word));
      return;
    }
    word[l]= c;

    // word too long for this line?
    if (_curx+rmargin+1 >= cols) {
      nl();
      indent(); // this affects <li> second line indent
      _curx+= strlen(word);
    }

    // html entity?
    //   TODO: this isn't correct
    //   complex: &amp followed by ';' or NOT!
    if (0 && word[0]=='&') {
      char* e= decode_entity(word);
      printf(" { %s } ", e);
    }
  }
}

void nl() {
  printf("\e[K\n"); // workaround bug!
  B(_bg); C(_fg); // fix "less -r" scroll up
  _cury++; _curx= 0; _ws= 1; _nl= 1;
}

void indent() {
  while(_curx < _indent) {
    putchar(' '); inx();
  }
  _ws= 1;
}

// TODO: need a stack? <a> inside <td>?
typedef struct tcol {
  int i, span;
  bool head;
  char* s;
  int w, h, len; // in chars
  char align; // l(eft) r(ight) c(enter) '.' for decimal
  char* tag; // TODO: remove
} tcol;

dstr* content = NULL;
dstr *table = NULL;
int tdn= 0, tdi= 1, ty=0, tx=0, tp= 0;

#define TD_MAX 1000
tcol tds[TD_MAX] = {0};
int incell= 0;

void handle_trd(int isRow, int isEndTag, TAG tag) {
  static bool head= false;
  assert(tdn < TD_MAX);

  // end
  if (incell) {
    // store previous cells info
    // TODO: should set indent=0 ???
    tcol* t= &tds[tdn++];
    t->i= tdi;
    t->span= 1;
    t->s= strdup(table->s+tp);
    t->len= (_cury-ty)*cols + _curx-tx;
    t->w= strlen(t->s); // < as \n
    t->h= _cury-ty;;
    t->tag= strdup(tag);
    t->head= head;
    //B(green); printf("[%s]", t->tag); B(white);

    tdi++;
    incell= 0; head= false;
  }

  if (strstr(" td th ", tag)) incell= 1;
  head= strstr(" th ", tag);

  //C(blue); printf("[%s]", tag); C(black);

  // ok, now we're ready for new element
  ty= _cury; tx= _curx;
  tp= strlen(table->s);

  if (isRow) {
    // insert a zero elemetn for each new line
    memset(&tds[tdn++], 0, sizeof(tcol));
    tdi= 1;
  }
}

void renderTable() {
  C(green); B(black);
  printf("\n----------TABLE----------\n");
  //printf("%s\n<<<---STRING\n", table->s);
#define MAX_COLS 11
  int w[MAX_COLS]= {0}, h[MAX_COLS]= {0};
  int sum_w[MAX_COLS]= {0}, sum_h[MAX_COLS]= {0};
  int rows= 0;

  // print data
  for(int i=0; i<tdn; i++) {
    tcol* t= &tds[i];

    if (!t->i) rows++;
 
    { int a= w[t->i], b= t->w;
      sum_w[t->i]+= b;
      w[t->i] = a > b ? a: b;
    }
 
    { int a= h[t->i], b= t->h;
      sum_h[t->i]+= b;
      h[t->i] = a > b ? a: b;
    }
 
    printf("===%2d: i=%1d span=%1d h=%2d w=%2d l=%3d '%s' ... tag=%s\n",
           i, t->i, t->span, t->h, t->w, t->len,t->s?t->s:"(NULL)", t->tag);
  }

  // print stats
  printf("\nROWS=%d\n", rows);
  printf("\niiii::::: ");
  for(int i= 0; i<MAX_COLS; i++) printf("%3d ", i);
  printf("\nWIDTH::: ");
  for(int i= 0; i<MAX_COLS; i++) printf("%3d ", w[i]);

  int a[MAX_COLS]= {0};
  for(int i= 0; i<MAX_COLS; i++)
    a[i]= (sum_w[i]+rows/2)/rows;

  printf("\n   AVG:: ");
  for(int i= 0; i<MAX_COLS; i++) printf("%3d ",  a[i]);

  printf("\n  A.SUM: "); int width= 0;
  for(int i= 0; i<MAX_COLS; i++) printf("%3d ",  width+=(sum_w[i]+rows/2)/rows);

  printf("\n\nHEIGHT:: ");
  for(int i= 0; i<MAX_COLS; i++) printf("%3d ", h[i]);
  printf("\n   AVG:: ");
  for(int i= 0; i<MAX_COLS; i++) printf("%3d ", (sum_h[i]+rows/2)/rows);
  printf("\n---END\n", table->s);
  C(black); B(white);

  // init format
  // TODO: make it max of td in column
  //   keep td width separate
  int f[MAX_COLS]= {0};
  for(int i= 0; i<MAX_COLS; i++) {
    int v= a[i];
    if (v) f[i]= v < 3 ? 3 : v;
  }

  // TODO: if have more space allocate
  // according to "bigger" (title?)
  //
  // shrink, starting with biggest columns
  // TODO: distribute more evenly, maybe
  //   remove 2 from biggest, 1 from next
  int overflows= 0;

  //const char* sbaseletter="𝗮";
  const char* sbaseletter="₀";
  //const char* sbaseletter="ₐ";
  wchar_t baseletter;
  size_t dummyl;
  u8_to_u32(sbaseletter, strlen(sbaseletter), &baseletter, &dummyl);
  assert(dummyl==1);
  printf("-------baseletter=%x\n", baseletter);

  wchar_t letter[MAX_COLS]= {0};
  
  int total= 0, tcolumns= 0;
  for(int i=0; i<MAX_COLS; i++) {
    total+= f[i];
    if (f[i]) tcolumns++;
    printf("--%d %d\n", i, f[i]);
  }

  while (total>cols-tcolumns) {
    printf("\n--------MINIMIZE------ %d %d %d\n", total, cols, tcolumns);
    // find biggest
    int big= -1, max= 0;
    total= 0;
    for(int i=0; i<MAX_COLS; i++) {
      int v= f[i];
      total+= v;
      if (v>max) max= v, big= i;
    }
    // decrease allocation
    if (big < 0) break; // screwed
    f[big]--;
  }

  // actually render
  int row= 0;
  for(int i=0; i<tdn; i++) {
    tcol* t= &tds[i];

    if (!t->i) putchar('\n');
 
    char* s= t->s;
    if (s) {
      if (t->head) underline();

      for(int j=f[t->i]; j>0; j--) {
        //if (*s && *s!=10 && (*s<32 || *s>128)) printf("[%d]", *s);
        if (!*s) putchar(' ');
        if (!*s && t->head) end_underline();
        else if (*s == 10) ;
        else if (*s == 13) ;
        else if (*s == (char)HNL) ;
        else if (*s == (char)SNL) printf("↲");

        else if (*s == '\n') {
          do s++; while (*(s+1) && (*s==(char)HNL || *s==(char)SNL));
          printf("↲");
        }
        else putchar(*s);

          if (*s) s++;
        }
        if (t->head) end_underline();

        // overflow?
        if (!*s) { // not overflow
          putchar(' ');
        } else {
          if (!t->head) {// <TD>
            printf("↲");
            // TODL: save s for next overflow line! print
          } else {
            overflows++;
            letter[t->i]= baseletter+overflows-1;
            putwchar(letter[t->i]);
          } C(fg); B(bg);
        }
    }
    row++;
  }

  printf("\n");
  if (overflows) {
    underline();
    printf("\nLegends");
    end_underline();
    nl();
  }
  // print overflow data
  for(int i=0; i<MAX_COLS; i++) {
    wchar_t l= letter[i];
    if (!l) continue;
    printf("  ");
    putwchar(l); //putchar(' ');
    // hmmm td[i] - lots off assumption
    tcol* t= &tds[i];
    assert(t->i==i); // it's header?
    // TODO: make a function of printer above...
    printf("%s", t->s);
  }
  if (overflows) printf("\n");

  // cleanup
  free(table); table= NULL;

  tdn= 0, tdi= 1, ty=0, tx=0, tp= 0;
  incell= 0;
  memset(tds, 0, sizeof(tds));
}

void p(int c) {
  char b= c;
  if (_table)
    table= dstrncat(table, &b, 1);
  if (_capture)
    content= dstrncat(content, &b, 1);

  if (_skip) return;

  // preformatted
  if (_pre) {
    _pc(c); inx();
    if (c=='\n') _curx= 0;
    return;
  }

  // soft+hard chars
  if (c<0) {
    // soft char
    if (c==SNL) {
      if (!_nl) nl();
    } else {
      // hard chars
      c= -c;
      if (c=='\n') {
        nl();
      } else {
        _pc(c); _ws= 0; inx();
      }
    }
    return;
  }

  if (_fullwidth) _pc(-1);
  // collapse whitespace
  int tb= (c=='\t'); // TODO: table?
  int ws= (c==' '||tb||c=='\n'||c=='\r');
  if (ws) {
    if (!_curx) return;
    if (!_ws || _pre) {
      _pc(' '); inx();
      if (_fullwidth) { _pc(' '); inx(); }
    }
    _ws= 1; if (_tb) _tb= tb;
    return;
  }

  // visible chars
  indent();
  if (_fullwidth) {
    // cheat, no word-wrap, print now!
    if (c<128) {
      putwchar(0xff01 + c-33); inx();
    } else {
      putchar(c);
    }
  } else {
    _pc(c);
  }
  inx(); _ws= 0; _tb= 0;
}

// steps one char in input stream
// == true if "have next" (not EOF)
// c is set to character read
#define STEP ((c= fgetc(f)) != EOF)

// parse chars till one of ENDCHARS
// S: if not NULL, lowercase char, append
// Returns non-matching char
int parse(FILE* f, char* endchars, char* s) {
  int c; char* origs= s;
  if (s) *s++ = ' ';
  while (STEP && (!strchr(endchars, c))) {
    if (s) {
      *s++= tolower(c);
      // TODO: error here for google.com
      if (s-origs > sizeof(TAG)) {
        printf("\n\n%%%%TAG==%s<<<\n", origs);
        exit(7);
      }
    }
  }
  if (s) { *s++ = ' '; *s= 0; }
  return c<0? 0: c;
}

void addContent();

void process(TAG *end);

// Use HI macro! (passes in tag)

#define HI(tags, fg, bg) hi(&tag, tags, fg, bg)

// hilight TAG if it's part of TAGS
// using FG color and BG color, and other
// formatting.

// After the matching </TAG> is reached:
// restore colors and undo formatting.
void hi(TAG *tag, char* tags, enum color fg, enum color bg) {
  static int level= 0;
  if (!tag || !*tag || !strstr(tags, *tag)) return;

  level++;
  TRACE("--->%d %s %d %d\n", level, tag?*tag:NULL, fg, bg);

  // save colors
  int sfg= _fg, sbg= _bg, spre= _pre, sskip= _skip, sindent=_indent; {
    // - START highlight
    if (fg != none) _fg= fg;
    if (bg != none) _bg= bg;
    if (strstr(PR, tag)) _pre= 1;
    if (strstr(SKIP, tag)) _skip= 1;

    if (strstr(" ul ol ", tag)) _indent+= 2;

    // underline links!
    if (strstr(" a ", tag)) {
      underline(); C(_fg);
      _capture++;
    }
    if (strstr(" table ", tag)) {
      table= dstrncat(NULL, NULL, 1024);
      _table++;
    }
    // italics
    if (strstr(IT, tag)) { printf("\e[3m"); C(_fg); };
    // fullwidth
    if (strstr(HD, tag)) _fullwidth++;

    // content
    C(_fg); B(_bg);
    if (strstr(" h1 ", tag)) p(HNL);
    if (strstr(HD, tag)) p(HS);
    if (strstr(TT, tag)) p(HS);
    
    // find end tag (recurse)
    TAG end = {' ', '/'};
    strcpy(end+2, *tag+1);
    process(end);

    // end content

    // - ENDing highlight/formatting
    if (strstr(TT, tag)) p(HS);
    // off underline links!
    if (strstr(" a ", tag)) {
      end_underline();
      if (!--_capture) addContent();
    }
    if (strstr(" table ", tag)) {
      if (!--_table) renderTable();
    }
    // off italics
    if (strstr(IT, tag)) printf("\e[23m");
    // off fullwidth
    if (strstr(HD, tag)) _fullwidth--;

    // restore saved state (colors/pre/skip)
  } _pre= spre; _skip= sskip; _indent= sindent;
  if (strstr(NL, tag)) p(SNL);
  C(sfg); B(sbg); 

  level--;
  TRACE("<--%d %s\n", level, tag?*tag:NULL);
}

FILE* f;

int skipspace() {
  int c;
  while (STEP && isspace(c));
  return c;
}

// capture deatails of an entity rendering
typedef struct pos{
  int x, y;
} pos;

typedef struct entity {
  TAG tag;
  //dhash* attr;
  dstr* content;

  pos start, end;
} entity;

entity* lastentity= NULL;

void newTag(TAG tag) {
  entity* e= calloc(sizeof(link), 0);
  memcpy(e->tag, tag, sizeof(tag));

  lastentity= e; // TODO: add to list/stack?
}

void addAttr(TAG attr, dstr* val) {
  entity* e= lastentity;
  //else if (strstr(" href src ")) e->url= val;
  free(val);
}

void addContent() {
  if (trace_content) {
    printf("\n---%s y=%d x=%d", lastentity->tag, _cury, _curx);
    printf("\n---content: \"%s\" y=%d, x=%d", content->s, _cury, _curx);
  }

  if (lastentity) {
    lastentity->content= content;
    lastentity->end= (pos){_cury, _curx};
  } else {
    free(content);
  }
  content= NULL;
}

void process(TAG *end) {
  int c;
  while (STEP) {

    if (c!='<') { // content
      p(c);

    } else { // '<' tag start
      TAG tag= {0};
      _pc(-1); // flush word

      // parse tag
      c= parse(f, "> \n\r", tag);
      TRACE("\n---%s\n", tag);

      // comment
      if (strstr(" !-- ", tag)) {
        // shift 3 characters
        char com[4] = "1234";
        while (STEP) {
          strcpy(&com[0], &com[1]);
          com[2]= c; // add at end
          if (!strcmp("-->", com)) break;
        }
        continue;
      }
      
      // process attributes till '>'
      // TODO:move out to function
      if (c!='>') {
        // <TAG attr>
        if (strstr(TATTR, tag)) {
          newTag(tag);
          // TODO: CSS cheat: match nay
          // - color:\s*\S+[ ;}]
          // - background(-color)?:\s*\S+[ ;}]
          // - width/height/max-height/min-height
          // - linebreak/hypens/overflow/clip/white-space/word-break/word-spacing/word-wrap
          // - left/right/top/bottom
          // - text-align/align-content
          // - clear/break-before/break-after
          // - float
          // - display/visibility/
          // - font-size/font-weight/text-decoration/text-shadow/
          // - text-indent/text-justify/text-overflow
          // - table-layout
          while (STEP) {
            ungetc(skipspace(f), f); //hmm
            // read attribute
            TAG attr= {0};
            c= parse(f, "= >\"\'", attr);
            if (c=='>' ||c==EOF) break;
            // do we want it?
            if (strstr, ATTR, attr) {
              int q= skipspace(f);
              // TODO: move out?
              // merge w parse?
              dstr *v = NULL;
              if (q=='"' || q=='\'') {
                // id='foo' id="foo"
                while (STEP && c!=q)
                  v = dstrncat(v, &c, 1);
                ungetc(' ', f);
              } else {
                // id=foo
                while (STEP && !isspace(c) && c!='>')
                  v = dstrncat(v, &c, 1);
              }
              addAttr(attr, v);
            }
          }
        }
      }
      if (c!='>') c= parse(f, ">", NULL);

      // pre action for tags (and </tag>)
      // TODO: /th /td /tr tags are optional.. :-(
//      if (strstr(" /th /td  ", tag)) { /*p(-'\t'); */ p(' '); p('|'); p(' '); }
//      if (strstr(" /tr ", tag)) p(SNL);

      if (strstr(XNL, tag)) p(HNL);
      if (strstr(" td th tr /td /th /tr /table ", tag)) {
        //if (strstr(" td th tr /table ", tag)) {
        end_underline();
        //printf("\n[===%s===]\n", tag);
        handle_trd(strstr(" tr ", tag), strchr(tag, '/'), tag);
      }

      // check if </endTAG>
      if (strstr(*end, tag)) return;

      // pre action for some tags
      if (strstr(NL, tag)) p(SNL);

      // table hacks
      // TODO: at TD TH set indent to _curx, reset at <tr></table> to saved before <table> can HI() store _indent?
      // TODO: inside td/th handle \n differently
      // <COLGROUP align="center">
      // <COLGROUP align="left">
      // <COLGROUP align="center" span="2">
      // <COLGROUP align="center" span="3">
      if (strstr(" th ", tag)) underline();
      if (strstr(" td th ", tag)) if (!_nl) ; // { p('|'); p(' '); }

      if (strstr(" p ", tag)) {
        if (_curx>_indent) p(HNL);
        indent(); p(HS);p(HS);
      }
      // TODO: dt
      if (strstr(" li dt ", tag)) {
        p(SNL);
        _indent-= 3; indent(); _indent+= 3;
        printf(" ● "); inx(); inx(); inx();
      }

      // these require action after
      HI(" h1 ", white, black);
      HI(" h2 ", black, green);
      HI(" h3 ", black, yellow);
      HI(" h4 ", black, blue);
      HI(" h5 ", black, cyan);
      HI(" h6 ", black, white);

      HI(BD, red, none);
      HI(IT, none, none);

      HI(HL, magnenta, none);

      HI(FM, yellow, black);
      HI(PR, green, black);
      HI(TT, black, rgb(3,3,3));

      HI(" a ", 27, none);

      // formatting only
      HI(" ul ol ", none, none);
      HI(SKIP, none, none);
      HI(" table ", none, none);
    }
  }
}

int main(int argc, char**argv) {
  if (argc<2 || !strlen(argv[1])) {
    fprintf(stderr, "Usage:\n  ./w URL\n");
    return 0;
  }
  char* url= argv[1];
  TRACE("URL=%s\n", url);

  //cls();
  getsize();

  C(white); B(black);
  printf("🌍 ");
  B(8); // gray() ??? TODO
  //printf("🏠");
  putchar(' ');

  // simplify url shown
  char uu[strlen(url)+1];
  char* u= &uu;
  strcpy(u, url);

  if (strstr(u, "https://")==u) {
    printf("🔒"); u+= 8;
  } else if (strstr(u, "http://")==u) {
    int f=_fg;
    C(red);
    printf("🚩");
    fg(f);
    u+= 7;
  } 
  if (u[strlen(u)-1]=='/')
    u[strlen(u)-1]=0;
  printf("%s", u); nl();

  // get HTML
  char* wget= calloc(strlen(url) + strlen(WGET) + 1, 0);
  sprintf(wget, WGET, url);
  f= fopen(url, "r");
  if (!f) // && strstr(url, "http")==url)
    f= popen(wget, "r");
  if (!f) return 404; // TODO: better error

  // render HTML
  C(black); B(white);

  TAG dummy= {0};
  process(&dummy);

  p(HNL);
  C(white); B(black); p(HNL); p(HNL);

  pclose(f);
  return 0;
}
