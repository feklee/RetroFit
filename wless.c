// TODO: keep rereading file to displaY?
// tail -f source:
// - http://git.savannah.gnu.org/cgit/coreutils.git/tree/src/tail.c

// partly from old imacs.c
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#include "jio.h"

// lazy include no .h
#include "graphics.c"

#define LOADING_FILE ".w/Cache/loading.html.ANSI"



// generic functions?


void message(char* format, ...) {
  va_list argp;
  va_start(argp, format);

  save();
  // last line
  gotorc(screen_rows-2, 0);
  int sbg= B(black), sfg= C(green);
  vprintf(format, argp);
  B(sbg); C(sfg);
  clearend();
  restore();
  fflush(stdout);
}


// set k=NO_REDRAW to not update screen
// (good for showing temporary information like menus/hilite)
#define NO_REDRAW -1

// --- limits
int nlines= 0, nright= 10;

int ntab= 1; // number of newly openeed tabs + last

int rows;

// --- state
FILE *fhistory, *fbookmarks;

int top, right, tab, start_tab;

char *_search= NULL;
int _only= 0;

char *hit= NULL; // FREE!
// DONT free (partof hit)
char *file= NULL;
char *url= NULL;

dstr *line= NULL;


int incdec(int v, int k, int ikey, int dkey, int min, int max, int min2val, int max2val) {
  if (k==ikey) v++;
  if (k==dkey) v--;
  // fix
  if (v<min) v= min2val;
  if (v>max) v= max2val;
  return v;
}

#define COUNT(var, ikey, dkey, limit) var= incdec(var, k, ikey, dkey, 0, limit-1, 0, limit-1)
#define COUNT_WRAP(var, ikey, dkey, limit) var= incdec(var, k, ikey, dkey, 0, limit-1, limit-1, 0)

///////////////////////////////////
// bookmarks + links

// TODO: dynamic array? or read from file and match on every keystroke?
// (some newspapers have 26*26*5++ links! [cee] )
#define LINKS_MAX 25*25*25
int nlinks= 0;
char *links[LINKS_MAX] = {0};

void trunclinks(int n) {
  // free old links
  while(nlinks > n) {
    char *l= links[--nlinks];
    if (l) free(l);
    links[nlinks]= NULL;
  }
}

FILE *fopenext(char *fname, char *ext, char *mode) {
  char tmp[strlen(fname)+1+strlen(ext)+1];
  strcpy(tmp, fname);
  char *ldot= strrchr(tmp, '.');
  if (ldot)
    strcpy(ldot, ext);
  else
    strcat(tmp, ext);
  //printf("....open: %s\n", tmp); key();
  return fopen(tmp, mode);
}

// Load bookmarks/keyboards from FILE
// Returns number of items added
int loadshortcuts(char *file) {
  FILE *flinks;

  // .ansi file - extract links
  if (endswith(file, ".ANSI")) {
    flinks= fopenext(file, ".LINKS", "r");
  } else {
    flinks= fopen(file, "r");
  }
  // TODO: display error message?
  if (!flinks) return 0;

  // TODO: this doesn't work with popen
  char *lk;
  while (lk=fgetline(flinks)) {
    if (nlinks<LINKS_MAX)
      links[nlinks++]= lk;
    else
      error(LINKS_MAX>=nlinks, 77, "Getmore links! %d");
  }
  fclose(flinks);
}

// TODO: search prefixkey, or string
void listshortcuts() {
  clear();
  printf("\n=== BOOKMARKS ===\n\n");
  for(int i=0; i<nlinks; i++) 
    printf("%s\n", links[i]);
  printf("\n===Press key to continue\n");
  key();
}

// TODO: research universal XML bookmarks format called XBEL, also supported by e.g. Galeon, various "always-have-my-bookmarks" websites and number of universal bookmark converters.

void logbookmark(int k, char *s) {
  //log(bms, url, offset, top, s);
  gotorc(screen_rows-1, 0);
  cleareos();
  dstr *ds= dstrprintf(NULL, "%s %s %d %d %c%s\n",
    isotime(), url,  -1, top, k, s);
  // print to screen w/o newline
  fputs(ds->s, fbookmarks);
  free(ds);
  message("Saved bookmark '%c' = '%s'", k, s);
}

void listbookmarks(char *url, char *s) {
  char *line;
  clear();
  C(black); B(white);
  if (url)
    printf("./wbookmarks %s", url);
  else if (s && *s)
    printf("./wbookmarks %s", s);
  else 
    printf("./wbookmarks");
  clearend(); printf("\n\n");
  C(white); B(black);
  
  fseek(fbookmarks, 0, SEEK_SET);
cursoron();
  int mcount= 0;
  while(line= fgetline(fbookmarks)) {
    char *m=NULL;
    if ((!url || !*url || strstr(line, url))
        && (!s || !*s || (m= strcasestr(line, s)))) {

      // it's a match!
      //printf("= %s\n", line);

      char *date, *u, *data;
      int offset, top;
      int n= sscanf(line, "%ms %ms %d %d %m[^]",
        &date, &u, &offset, &top, &data);

      // simplify url to show
      char *simple= u;
      simple= sskip(simple, "https://");
      simple= sskip(simple, "http://");
      simple= sskip(simple, "www.");
      simple= strunc(simple, "?");
      simple= sdel(simple, ".html");
      simple= sdel(simple, ".htm");

      //printf("matched=%d (5)\n", n);
      if (n == 5 && data[0]!='=') {
        mcount++;

        // --- got a matched result

        const int right= 16;
        int cols= screen_cols-right-2;

        if (strlen(data)<=cols)
          printf("%-*s ", cols, data);
        else
          printf("%-*s ", cols, "");

        // print right column
        if (url) {
          C(yellow);
          printf("%.16s\n", isoago(date));
        } else {
          C(cyan);
          if (strlen(simple)>right)
            printf("%.13s...\n", simple);
          else
            printf("%.16s\n", simple);
        }
        C(white);

        if (strlen(data)>cols)
          printf("%s\n", data);

      }

      free(line);
      free(date); free(u); free(data);
    } else {
      free(line);
    }
  }

  printf("\n%d Matching Lines", mcount);
  printf("\n(press key to continue)");
  fflush(stdout);
}

void bookmark(int k, char *text) {
  int cpos= -1; // TODO

  if (k=='*' || k==CTRL+'D') {
    // TODO: to save the seekpos too!
    logbookmark('*', "");
    k= NO_REDRAW;
    return;
  }

  // also logs searches!
  logbookmark(k, text);

  // search
  if (k=='=') {
    // TODO: make a loop around it allowing "incremental" search
    listbookmarks(NULL, text);
    k= NO_REDRAW;
  }
}

void search(int k, char* text) {
  if (!text || !strlen(text)) {
    FREE(_search);
  } else {
    _search= strdup(text);
  }
}

// start download in background
void download(char* url, int force) {
  if (!url || !*url) return;
  char *end= strpbrk(url, " \t\n");
  int ulen= end? end-url : strlen(url);

  // TODO: srip the script?
  dstr *cmd= dstrprintf(NULL, "./wdownload %s \"%.*s\" %d %d &",
    force?"-d":"", ulen, url, screen_rows, screen_cols);
  system(cmd->s);
  free(cmd);

  // wait enough for .whistory to be updated...  and .TMP to be created
  usleep(1000*1000);
}

void reload(char* url) {
  download(url, 1);
}

FILE *openOrReloadAnsi() {
  // wait for open of ANSI file
  FILE *fansi= fopen(file?file:".stdout", "r");
  FILE *ftmp= fopenext(file, ".TMP", "r");
  
  // file missing in cache
  // TODO: don't do every time...
  if (!fansi && !ftmp) {
    drawReloading();
    gotorc(1, 0);
    download(url, 0);
    ftmp= fopenext(file, ".TMP", "r");
  }
  // wait if have .TMP till not there
  // that signals the end of .ANSI created
  gotorc(1, 0);
  int zlast= 0;
  while (ftmp && !haskey()) {
    usleep(300*1000);
      
    fseek(ftmp, 0, SEEK_END);
    int z= ftell(ftmp);
    if (z!=zlast)
      printf(" %d ", z);
    zlast= z;
    fclose(ftmp);
      
    putchar('>'); fflush(stdout);
    
    ftmp= fopenext(file, ".TMP", "r");
  }
  if (ftmp) fclose(ftmp);

  fansi= fopen(file?file:".stdout", "r");
  nlines= fansi? flines(fansi) : -1;
  return fansi;
}

// --- Display
void display(int k) {

  // -- header
  reset();
  gotorc(0, 0);
  B(black); C(white);
  printf("./w %.*s", screen_cols-16, url);
  fflush(stdout);

  FILE *fansi= openOrReloadAnsi();
  if (!fansi) return;
    
  // --- print header for real!
  reset();
  gotorc(0, 0);
  if (url) {
    // TODO: app header reserved for tab-info?
    char *u= url, col;
    col= printf("./w ");
    // nprintf !!!
    char parts[15];
    int w= snprintf(parts, sizeof(parts), " L%d %d/%d", top, (top+2)/rows+1, (nlines+rows/2)/rows);
    while (*u) {
      // TODO: unicode?
      putchar(*u++);
      col++;
      if (col+1 >= screen_cols-w) break;
    }
    // space out
    while (col++ < screen_cols-w) putchar(' ');
    printf("%s", parts);
  }

  // -- main content
  B(white); C(black);
  gotorc(1, 0);
  fflush(stdout);

  if (fansi) {
    int c, n=top;
    fseek(fansi, 0, SEEK_SET);
    dstr *ln= dstrncat(NULL, NULL, 160);
    while(c= fgetc(fansi)) {
      // TODO: cleanup when make the hidden lines simplier...
      if (c=='\n' || c==EOF) {

        char *s= ln->s;
        char *f= _search ? strcasestr(s, _search) : NULL, *found= f;
        if (_only && !f) *s= 0;

        while(f) {
          printf("%.*s", f-s, s);
          B(red); C(white);
          printf("%.*s", strlen(_search), f);
          s= f+strlen(_search);
          // guess
          B(white); C(black);
          // find next
          f= strcasestr(s, _search);
        }
        // print remainding (or all if no match)
        printf("%s", s);
        ln->s[0]= 0;

        if (c==EOF) break;

        c= fgetc(fansi);
        if (c!='\n' && c!='#') {
          if (n<0 && (!_only || found)) putchar('\n');
          if (n>=0 || !_only || found) 
            n--;
        } else {
          while(c=='\n' || c=='#') {
            // skip comment line
            if (c=='#')
              while((c= fgetc(fansi)) != EOF && c!='\n');
            c= fgetc(fansi);
            if (c==EOF) break;
          }
        }
        clearend();
      }

      // print actual content
      if (n<0) {
        //putchar(c);
        char ch= c;
        ln= dstrncat(ln, &c, 1);
      }
      if (n<=-rows) break;
    }
    B(black); C(white); cleareos();
    fclose(fansi);
  } 

  // read keyboard shortcuts, page links
  trunclinks(0);
  loadshortcuts(".wkeys");
  loadshortcuts(file);

  //loadbookmarks(file?file:".wlinks");

//  reset();
  clearend(); C(white); B(black); clearend();
  cleareos();

  // -- footer
  reset();

  // TODO: set message and show for X s?
  if (0) message("%3d %3d/%d = %d %d", top, right, tab, ntab-1, 4711, 12);
  
  gotorc(screen_rows-1, 0);
}

// --- ACTIONS

// update status
void deltab() {
}

void visited() {
}

// queue/stack readers
void push(int t) {
}

int pop() {
  return 0;
}

void delpush(int t) {
}

int delpop() {
  return 0;
}

void read_next() { // till end
}

void queue_read() {
}

// creates a new tab from URL.
//
// Note: URL can be terminated by whitespace. This allows this
// function to be called with a pointer of an url within another
// string.
//
// Return newly opened tab number
int newtab(char* url) {
  reload(url);

  // open new tab, go to
  return ntab++;
}
      
void opentab() {
  gotorc(0, 0);
  char* u= input("./w ");
  if (!u) return;
  push(tab);
  tab= newtab(u);
}

// Find match to key INPUT extract link and ./wdonwload it in, allocate new tab.
//
// Return 0 if no match
//        N the new tab number
//
// TODO: make it match a string!
int click(char *keys) {
  int len= strlen(keys);
  for(int i=0; i<nlinks; i++) {
    char *u = links[i];
    if (!strncmp(u, keys, len) && u[len]==' ' || u[len]=='\t') {
      u+= strlen(keys);
      // one key match found!
      //fprintf(stderr, "\n===========FOUND match key='%c' for:\n'%s'\n", k, links[i]);
      // TODO: make a safe system/popen
      // TODO: handle gracefully
      assert(!strchr(links[i], '\\'));
      assert(!strchr(links[i], '"'));
      assert(!strchr(links[i], '\''));
      assert(!strchr(links[i], '\`'));
      assert(!strchr(links[i], '\n'));

      // skip spaces
      while(*u && (*u==' ' || *u=='\t')) u++;
      return newtab(u);
    }
  }
  message("\[31mNo such link: %s", keys);
  return 0;
}

keycode command(keycode k, dstr *ds) {
  char *line= ds->s;
  int len= strlen(line);
  if (!*line) return k;

  // SEARCHING
  if (k==RETURN || k==CTRL+'S' || k==CTRL+'O') {
    switch(line[0]) {

    case '=':
      bookmark(line[0], &line[1]);
      if (line[0]!='=') line[0]= 0;
      return NO_REDRAW;

    case '^': // TODO: ^top search
    case '_': // TODO: _end search
      break;

    case '/':  case '&':
      // TODO: O only?
      _only= (k==CTRL+'O');
      search(line[0], &line[1]);
      k=0;
      break;
    default: // page search
      _only= (k==CTRL+'O');
      search(k, line);
      break;
    }
  }
  
  // ACTIONS (save store go)
  if (k==RETURN) {

    switch(line[0]) {

    case '#': case '@': case '$':
      bookmark(line[0], &line[1]);
      line[0]= 0;
      return NO_REDRAW;

    case '/':  case '&':
      search(line[0], &line[1]);
      // keep line to allow for more search
      break;
      
    case '!':
      // TODO: can't input ls -l *.html LOL
      // TODO: replace %u w URL
      system(&line[1]);
      return NO_REDRAW;

    case '|': // TODO: pipe HTML/text
    case '^': // TODO: move to top?
    case '_': // TODO: bury? _ underline something?
      break;
    }

    // all a-z, maybe link click?
    if (strspn(line, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")==len) {
      tab= click(line);
      line[0]= 0;
      return 0;
    }

    // url? or file? have /:.?
    if (strchr(line+1, '/') || strchr(line+1, ':') || strchr(line, '.')) {
      tab= newtab(line);
      line[0]= 0;
      return 0;
    }
  }

  return k;
}



////////////////////////////////////////
// CLICK ACTIONS

char *speech() {
  // show microphone
  gotorc((screen_rows-screen_cols/2)/2, 0);
  fflush(stdout);
  system("Graphics/imcat/imcat Graphics/microphone-wired.png");

  char *r= NULL;
  // only works on Android
  // ... -p  # gives continous output
  // TODO: but it seem buffered?
  // https://github.com/termux/termux-api/blob/master/app/src/main/java/com/termux/api/SpeechToTextAPI.java
  FILE *f= popen("termux-speech-to-text -p", "r");
  char buf[1024]= {0};
  if (f) {
    // read all lines, show, but pick last
    while (fgets(buf, sizeof(buf), f)) {
      printf("%s", buf);
      clearend();
    }

    r= strdup(buf);
    if (r && r[strlen(r)-1]=='\n')
      r[strlen(r)-1]= 0;
    fclose(f);
  }
  key();

  // remove pending strokes (duplicate click?)
  while(haskey()) key();

  return r;
}

// Definition of clickable regions
char LEVELS[]= "N n   CC   s S";
char DIST[]= "W CCC E";

void showClickableRegions(int l, int d) {
  for(int rr=0; rr<screen_rows; rr++){
    for(int cc=0; cc<screen_cols; cc++){
      int ll= (sizeof(LEVELS)-1)*rr/screen_rows;
      int dd= (sizeof(DIST)-1)*cc/screen_cols;
      if (LEVELS[ll]==' ' || DIST[dd]==' ') continue;
      //if (ll!=-1 && ll!=l && dd!=d) continue;
      gotorc(rr, cc);
      B((ll==l && dd==d)? red : green);
      spc();
    }
  }

  // mark all regions
  for(int rr=0; rr<screen_rows; rr++){
    for(int cc=0; cc<screen_cols; cc++){
      int ll= (sizeof(LEVELS)-1)*rr/screen_rows;
      int dd= (sizeof(DIST)-1)*cc/screen_cols;
      if (ll!=l && dd!=d) continue;
      gotorc(rr, cc);
      B(red);spc();
    }
  }
}

int clickDispatch(int k) {
  // TODO: make function/macro/API?
  int b= (k>>16) & 0xff, r= (k>>8) & 0xff, c= k & 0xff;
  int save_k= k;
  k= NO_REDRAW;

  // Adjusted Row and Column (calibrate?)
  int ar= r-1, ac= c-1;
  if (ar<0) ar= 0; if (ac<0) ac= 0;
  if (ar>=screen_rows) ar= screen_rows-1;
  if (ac>=screen_cols) ac= screen_cols-1;

  // continue if not clickable region
  int l= (sizeof(LEVELS)-1)*ar/screen_rows;
  int d= (sizeof(DIST)-1)*ac/screen_cols;
  if (LEVELS[l]==' ' || DIST[d]==' ')
    return NO_REDRAW;

  showClickableRegions(l, d);

  // show exactly where clicked
  gotorc(ar, ac-1);
  B(red);C(white);

  char dir[3]={LEVELS[l], DIST[d],0};
  printf("%s",dir);

  // --- Click Buttons

  // MENU      SPEACH      CLOSE
  if (!strcmp(dir, "NW")) ;
  if (!strcmp(dir, "NC")) {
    char *r= speech();
    if (r) {
      // append to current edits
      char *s= line->s;
      if (s[0] && s[strlen(s)-1]!=' ')
        line= dstrncat(line, " ", -1);
      line= dstrncat(line, r, -1);
      free(r);
    }
    return 0; // redraw
  }
  if (!strcmp(dir, "NE")) drawX(),k= LEFT;

  // xxxx      xxxx        xxxx
  if (!strcmp(dir, "nW")) ;
  if (!strcmp(dir, "nC")) ;
  if (!strcmp(dir, "nE")) ;

  // HistoryBACK  ???   HistoryFORW
  if (!strcmp(dir, "CW")) k= LEFT;
  //   (CC: main content area)
  if (!strcmp(dir, "CE")) k= RIGHT;

  // xxxx      xxxx        PAGE UP
  if (!strcmp(dir, "sW")) ;
  if (!strcmp(dir, "sC")) ;
  if (!strcmp(dir, "sE")) k= META+' ';
  // xxxx      xxxx        PAGE DOWN
  if (!strcmp(dir, "SE")) k= ' ';

  // show a click block
  if(1)
    for(int rr=-3; rr<=1; rr++) {
      for(int cc=-5; cc<=3; cc++) {
        gotorc(r+rr, c+cc);
        if (k && MOUSE_DOWN)
          B(red);
        else 
          B(green);
        putchar(' ');
      }
    }

  // Show exact position of click
  B(white); C(black); gotorc(ar, ac-2);
  printf(" %c%c ", LEVELS[l], DIST[d]);
  restore();

  // simplify for up/down counters
  if (save_k & SCROLL) k = save_k & SCROLL;

  return k;
}




////////////////////////////////////////
// DRAG SCROLLERS ACTIONS

void scrollBookmarks() {
  // first show current page bookmark info
  // second scroll bookmarks file backwards (reverse order)
  // can scroll a page info in 3 rows movement! (number to indicate?)
  // IDEA: have a diagonal background, this would make scrolling clear!
}

void scrollHistory() {
  // Bottom Left
  // scrollback of list in time, show when last visited?
  // date headers like ./whi?
}

void scrollStack() {
}

void scrollReadings() {
}

void scrollTabs() {
  // this is more like normal viewing
  // show an alternative history!
  // ALT-LEFT / ALT-RIGTH ? LOL
  // this is pages in the order you saw them, so like the browser BACK/FORWARD

  // Now, WHAT happens when you create a branch by going back, clicking on a link. There is no forward from that branch?
}

void showScroll(int k, int r, int c) {
  int lxy, kxy=k & 0x0000ffff;
  int n= 0, up= 0, dn= 0;

  B(black); C(white);
  // loop until different event
  do {
    // count scrolls
    if (k & SCROLL_UP) up++;
    if (k & SCROLL_DOWN) dn++;
    n++;

    int d=dn-up;

    // draw line follow scroll
    gotorc(r-up+dn, c);
    B(yellow); spc(); B(black);

    // print info
    gotorc(0, 0);
    printf("---SCROLL n=%d dn=%d up=%d     ", n, dn, up);
    fflush(stdout);
            
    // location is stored in 2 lowest
    lxy= kxy;
    k= key();
    gotorc(r-up+dn, c);
    spc();
    kxy = k & 0x0000ffff;
  } while(lxy==kxy);
}

int touchDispatch(int k) {
  int b= (k>>16) & 0xff, r= (k>>8) & 0xff, c= k & 0xff;

  // Adjusted Row and Column
  int ar= r-1, ac= c-1;

  // % start areas of interest
  int pr= r*100/screen_rows;
  int pc= c*100/screen_cols;

  int top= pr<15, bottom= pr>85;
  int left= pc<20, right= pc>80;

  int center= !left && !right;
  int middle= !top && !bottom;

  // to differentiate new event
  static int lastk= 0;
  lastk= k;;

  // Drag down actions:
  if (top && right) {
    color COLORS[]={
      yellow, green, cyan, blue, magenta, red, black};
    // Not probable
    // Maybe a scroll-pick-hilite-links?
    char* TEXT[]={
      "CLOSE", "STAR", "HASHTAB", "UNDO",
      "LIST", "HISTORY", "QUIT"};

    drawPullDownMenu(COLORS, TEXT, ALEN(COLORS));

    // wait for other event
    while((k= key()) == lastk);

  }
  //else if (top && center) scrollStack();
  //else if (top && left) scrollBookmarks();

  else if (middle && left) {
    // history: back & forward
    k= (k & SCROLL_UP) ? LEFT : RIGHT;
  } else if (middle && center) {
    // reserved: content scroll
  } else if (middle && right) {
    // tabs: next & prev
    k= CTRL+ ((k & SCROLL_UP) ? LEFT : RIGHT);
  }
  //else if (bottom && left) scrollHistory();
  else if (bottom && center) {
    scrollReadings();
  }
  //else if (bottom && right) scrollTabs();
  else {
    // No a drag-down defined

    showScroll(k, ar, ac);
    // Show screen till next event
    k= NO_REDRAW;
  }
  return k;
}

////////////////////////////////////////
//

void loadPageMetaData() {
  int t= start_tab+tab;
  if (hit) {
    free(hit);
    file= url= hit= NULL;
  }

  hit= fgetlinenum(fhistory, t);
  if (hit) {
    const char *W= "#=W ";
    // extract file
    file= strstr(hit, W);
    if (file) {
      int c;
      file+= strlen(W);
      // skip spaces
      while(*file==' ') file++;

      url= file;
      // skip URL
      while(*file && *file!=' ') file++;
      if (*file) *file++= 0;

      // skip spaces
      while(*file==' ') file++;
    }
    error(!file, 10, "history log entry bad: '%s'\n", hit);
  } else {
    // TODO: add a way to reload or signal when done!
    file= LOADING_FILE;

    //error(!hit, 10, "history log entry not found: %d", t);
  }
}

// CTRL
// ==== 
// ^A    bookmarks
// ^B    ? back (viewd tabs), chrome-bookmarks
// ^C    EXIT
// ^D    ? del-tab, chrome: bookmark
// ^E    ? emacs - edit file
// ^F    ? forward (viewed tabs), chrome: search bar
// ^G    cancel (clear-line/draw), chrome: next match ^S-G previous
// ^H    help, chrome: history
// ^I    TAB
// ^J    RETURN (can't change?), chrome: download-manager
// ^K    KILL tab
// ^L    ? list?    chrome: location - no need!
// ^M    RETURN
// ^N    next-line, chrome: new window
// ^O    ? , chrome: open file on computer
// ^P    prev-line, chrome: print file
// ^Q    qutable info about page
// ^R    reload
// ^S    search, emacs: search, search-next
// ^T    ? tabs list, chrome: new tab (we don't need)
// ^U    ?    , chrome: display HTML
// ^V    page-down, M-V page-up
// ^W    close-tab/window (chrome: close tab)
// ^X    list-links/shortcuts
// ^Y    ? yank (undo tab kill)
// ^Z    ZUSPEND/ZLEEP

keycode keyAction(keycode k) {
  if (k==NO_REDRAW) return k;

  int kc= k & ~META & ~ CTRL;

  // -- bookmarks
  // w3m: Esc-b	View bookmarks
  // w3m: Esc-a	To bookmark
  // elinks: v load bookmark, ESc b
  // elinks: a, ESC a add current bookmark
  // ^S_B: show/hide bookmarks (chrome)
  // ^S_A: open bookmarks manager (chrome)
  // ^D - save current page as bookmark
  // ^S-D- save all open tabs as "folder" (chrome)
  if (k==CTRL+'D' || (k<127 && strchr("=*#$", k))) {
    bookmark(k, line->s);
    k= NO_REDRAW;
  }
  if (k==CTRL+'A') listbookmarks(NULL, NULL),k=NO_REDRAW;
  if (k==CTRL+'Q') listbookmarks(url, NULL),k=NO_REDRAW;

  if (k==CTRL+'X') listshortcuts(),k=NO_REDRAW;

  // -- page action
  if (k==CTRL+'R') {
    drawReloading();
    reload(url);
    k= NO_REDRAW;
  }
  // chrome: CTRL-P: print current webbpage ? save?
  // chrome: CTRL-S: save current webpage
  // chrome: ESC: stop loading webpage
  // chrome: ^U - display HTML
  // chrome: ^O - open file on computer
  // chrome: ALT-link (mouse) download link

  // chrome:F7 : turn on caret browsing

  // -- search
  // chrome: ^F, F3: search bar
  // chrome: ^G: next match
  // chrome: ^S-G: prev match

  // -- small navigation
  COUNT(top, DOWN, UP, nlines);
  COUNT(top, CTRL+'N', CTRL+'P', nlines);
  COUNT(top, RETURN,META+RETURN, nlines);
  COUNT(top, SCROLL_UP, SCROLL_DOWN, nlines);

  // -- big navigation
  if (k=='<' || kc==',') top= 0; // top
  if (k=='>' || kc=='.') top= nlines;
  if (k==META+'V' || k==META+' ' || k==BACKSPACE || k==DEL) top-= rows;
  if (k==CTRL+'V' || k==' ') top+= rows;

  if (top>nlines-rows) top= nlines-rows;
  if (top<0) top= 0;

  // -- TABS
  if (k=='?' || k==CTRL+'H' || k==FUNC+1) {
    push(tab);
    // open already existing?
    tab= newtab("wless.html");
  }

  // TODO: cleanup (a-z A-Z used in editing), now abc RET is how to select link
  // clicking (new tab) a-z0-9
  //  a  open link in new tab and go
  //  A  open in background tab
  //  M-A open shortcut page 
  //
  // TODO: could use M-a too?
  if (isalnum(kc) && (k<127 || kc<='Z') && !(k&TERM)) {
    char keys[2]={kc,0};
    if (k==toupper(kc)) { // A-Z
      // open behind
      click(keys);
    } else { // a-z, M-A -- M-Z
      // open now
      push(tab);
      tab= click(keys);
    }
  }

  // chrome: ^1-8 goto tab #
  // chrome: ^9 go to righ most tab

  // pop from deleted (chrome: ^S-T
  if (k==CTRL+'Y') {
    push(tab);
    //tab= delpop();
  }

  if (k==CTRL+'W') { // close tab (^F4)
    delpush(tab);
    deltab();
    //tab= pop();
  }
  if (k==CTRL+'K') { // kill forward
    delpush(tab);
    deltab();
    //tab++;
  }
  if (k==CTRL+'D') { // delete tab back
    delpush(tab);
    deltab();
    //tab++;
  }

  // TODO: open empty has no meaning...
  //if (k==CTRL+'T') {
  //push(tab);
  //tab= ntab++;
  //}

  //COUNT(top, CTRL+'F', CTRL+'B', nlines);
  // chrome: ^TAB; goto next open tab
  // chrome: ^S-TAB: goto prev open tab
  // chrome: M-LEFT: back browsing history
  // chrome: M-RIGHT: forward browsing histor
  // chrome: ^H: history page in new tab
  if (k==LEFT) tab--;
  if (k==RIGHT) tab++;
  if (start_tab+tab<=1) tab= -start_tab+1;
  if (tab>=ntab) tab= ntab-1;
  //COUNT_WRAP(right, RIGHT, LEFT, nright);

  //TODO: field? nfield
  // remove "right"
  //COUNT_WRAP(field, TAB, S_TAB, nfield);

  // --- Chrome keybaord short-cut
  // - https://support.google.com/chrome/answer/157179?hl=en&co=GENIE.Platform%3DDesktop#zippy=%2Ctab-and-window-shortcuts

  // TODO: shortcuts "missing", to consider
  // chrome: ^J: downloads manager
  // chrome: S-ESC: task manager
  // chrome: ^S-J: open dev tools
  // chrome: ^S-DEL: open delete history
  // chrome: ^S-M: login as different user
  // chrome: M-S-i: feedback form
}

keycode editTillEvent() {
  int k;
  // loops capture menu drag events
  do {

    // EDIT
    gotorc(screen_rows-1, 0); cleareos();

    cursoron();

    //while(1) {
    k= edit(&line, -1, NULL, NULL, " <>*");
    //printf("\n>>> %s line>%s<\n", keystring(k), line->s);

    // Safe-way out!
    if (!strcmp(line->s, "QUIT")) exit(0);
    cursoroff();
    // clear line
    if (k==CTRL+'G' || !line->s[0]) {
      _only= 0;
      line->s[0]= 0;
      if (_search) FREE(_search);
    }

    // TOUCH DRAG
    if (k & SCROLL)
      k= touchDispatch(k);

    // not scrolling in borders...
    if (k & SCROLL) {
      // Simplify to SCROLL_UP/DOWN
      k &= SCROLL;
      break;
    }

    // CLICK
    if ((k & MOUSE) && !(k & SCROLL))
      k= clickDispatch(k);

  } while (k<0);

  return k;
}

// --- MAIN LOOP

int main(void) {
  jio();

  cursoroff();
  system("echo '`date --iso=ns` #=WLESS`' >> .wlog");
  screen_init();
  rows = screen_rows-1;

  // --- Open files for persistent state
  // (append+ will create if need)
  fhistory= fopen(".whistory", "a+");
  fbookmarks= fopen(".wbookmarks", "a+");
  start_tab= flines(fhistory);
  
  int k= 0, q=0, last_tab;
  // string for editing/command
  line= dstrncat(NULL, NULL, 1);
  while(1) {

    loadPageMetaData();

    // avoid update if events waiting
    if (!haskey() && k!=NO_REDRAW) {
      display(k);
      visited();
    }

    // - read special event & decode
    //while(1){
    k= editTillEvent(line);
    //printf("\n===> %s\n", keystring(k));}
    k= command(k, line);

    // -- system
    if (k==CTRL+'C') break;
    if (k==CTRL+'Z') kill(getpid(), SIGSTOP);

    // chrome: M-F, M-E: open menu
    // (I don't like it)

    k= keyAction(k);
  }
  printf("\r\n");

  gotorc(9999,9999);
  printf("\nExiting...\n");
  // implicity calls _jio_exit();
  return 0;
}
