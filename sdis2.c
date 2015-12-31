#define _XOPEN_SOURCE 600
#include <SDL2/SDL.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/select.h>
#include <time.h>
#include <sys/ioctl.h>
#define __USE_BSD
#define _BSD_SOURCE
#include <termios.h>

#include "font.h"
#include "config.h"

struct termios orig;
struct termios new;
int reset_term = 0;

void handle_sigint(int i) {
  SDL_Quit();
  if(reset_term)
    tcsetattr(0, TCSANOW, &orig);
  exit(0);
}

SDL_Window *win;
SDL_Renderer *ren;

unsigned int pixbuf[128][256];

#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 21

unsigned char chars[SCREEN_WIDTH][SCREEN_HEIGHT];
int cx = 1, cy = 0;
int scroll_top = 0, scroll_bottom = SCREEN_HEIGHT;
int rev = 0;

void update_pixbuf() {
  int x, y, gx, gy;
  for(x = 0; x < SCREEN_WIDTH; x++) {
    for(y = 0; y < SCREEN_HEIGHT; y++) {
      for(gx = 0; gx < 4; gx++) {
	for(gy = 0; gy < 6; gy++) {
	  pixbuf[y * 6 + gy][x * 4 + gx] = bitmap[chars[x][y] & 0x7f][gy] & 1 << (3 - gx);
	  if(chars[x][y] & 0x80) pixbuf[y * 6 + gy][x * 4 + gx] = !pixbuf[y * 6 + gy][x * 4 + gx];
	}
      }
    }
  }
}

#define VT_STATE_READ1 0
#define VT_STATE_SELCS 1
#define VT_STATE_RDECALN 2
#define VT_STATE_DEFG0 3
#define VT_STATE_DEFG1 4
#define VT_STATE_OSC 5
#define VT_STATE_OSC_P 6
#define VT_STATE_CSI 7
#define VT_STATE_CSI_DROPCHAR 8

int vt100_escape = 0;
int vt100_argp, vt100_args[16];
int vt100_state;

void scroll(int direction) {
  printf("Scrolling... (%d %d)\n", scroll_top, scroll_bottom);
  int x, y;
  if(direction) {
    for(y = scroll_bottom; y > scroll_top; y--) {
      for(x = 0; x < SCREEN_WIDTH; x++) {
	chars[x][y] = chars[x][y-1];
      }
    }
    for(x = 0; x < SCREEN_WIDTH; x++) chars[x][scroll_top] = 0;
  } else {
    for(y = scroll_top; y < scroll_bottom - 1; y++) {
      for(x = 0; x < SCREEN_WIDTH; x++) {
	chars[x][y] = chars[x][y+1];
      }
    }
    for(x = 0; x < SCREEN_WIDTH; x++) chars[x][scroll_bottom] = 0;
  }
}

void vtchar(char c) {
  int i;
  printf("%c", c);
  switch(vt100_state) {
  case VT_STATE_READ1:
    switch(c) {
    case 'c': // RIS
    case 'D': // IND
    case 'E': // HTS
    case 'M': // RI
    case 'Z': // DECID
    case '7': // DECSC
    case '8': // DECRC
    case '>': // DECPNM
    case '=': // DECPAM
      vt100_state = -1;
      break;
    case '%':
      vt100_state = VT_STATE_SELCS;
      break;
    case '#':
      vt100_state = VT_STATE_RDECALN;
      break;
    case '(':
      vt100_state = VT_STATE_DEFG0;
      break;
    case ')':
      vt100_state = VT_STATE_DEFG1;
      break;
    case ']':
      vt100_state = VT_STATE_OSC;
      break;
    case '[':
      vt100_state = VT_STATE_CSI;
    }
    break;
  case VT_STATE_SELCS:
    switch(c) {
    case '@': // Select default character set
    case 'G': // Select UTF-8
    case '8': // Select UTF-8
      vt100_state = -1;
      break;
    }
    break;
  case VT_STATE_RDECALN:
    switch(c) {
    case '8': // DECALN
      vt100_state = -1;
      break;
    }
    break;
  case VT_STATE_DEFG0:
    switch(c) {
    case 'B': // select default graphics mapping
    case '0': // select VT100 graphics mapping
    case 'U': // select null graphics mapping
    case 'K': // select user graphics mapping
      vt100_state = -1;
      break;
    }
    break;
  case VT_STATE_DEFG1:
    switch(c) {
    case 'B': // select default graphics mapping
    case '0': // select VT100 graphics mapping
    case 'U': // select null graphics mapping
    case 'K': // select user graphics mapping
      vt100_state = -1;
      break;
    }
    break;
  case VT_STATE_OSC:
    if(c == 'P') {
      vt100_argp = 0;
      vt100_state = VT_STATE_OSC_P;
    } else if(c == 'R') {
      vt100_state = -1;
    } else if(c == '\a') {
      vt100_state = -1;
    }
    break;
  case VT_STATE_OSC_P:
    if(vt100_argp < 6) vt100_argp++;
    else vt100_state = -1;
    break;
  case VT_STATE_CSI:
    switch(c) {
    case '[':
      vt100_state = VT_STATE_CSI_DROPCHAR;
      break;
    case ';':
      vt100_argp++;
      vt100_args[vt100_argp] = 0;
      break;
    case '@': // ICH
    case 'E': // CNL
    case 'F': // CPL
    case 'L': // IL
    case 'M': // DL
    case 'X': // ECH
    case 'a': // HPR
    case 'c': // DA
    case 'f': // HVP
    case 'g': // TBC
    case 'n': // DSR
    case 's': // save cursor
    case 'u': // restore cursor
    case '`': // HPA
      printf("Err: unknown CS %c\n", c);
      vt100_state = -1;
      exit(1);
      break;
    case 'A': // CUU
      cy -= vt100_args[0] ? vt100_args[0] : 1;
      if(cy < 0) cy = 0;
      vt100_state = -1;
      break;
    case 'B': // CUD
      cy += vt100_args[0] ? vt100_args[0] : 1;
      if(cy >= SCREEN_HEIGHT - 1) cy = SCREEN_HEIGHT - 1;
      vt100_state = -1;
      break;
    case 'C': // CUF
      cx += vt100_args[0] ? vt100_args[0] : 1;
      if(cx >= SCREEN_WIDTH - 1) cx = SCREEN_WIDTH - 1;
      vt100_state = -1;
      break;
    case 'D': // CUB
      cx -= vt100_args[0] ? vt100_args[0] : 1;
      if(cx < 0) cx = 0;
      vt100_state = -1;
      break;
    case 'G': // CHA
      cx = vt100_args[0];
      vt100_state = -1;
      break;
    case 'H': // CUP
      cx = vt100_args[1] - 1;
      if(cx > SCREEN_WIDTH - 1) cx = SCREEN_WIDTH - 1;
      if(cx < 0) cx = 0;
      cy = vt100_args[0] - 1;
      if(cy > SCREEN_HEIGHT - 1) cy = SCREEN_HEIGHT - 1;
      if(cy < 0) cy = 0;
      vt100_state = -1;
      break;
    case 'J': // ED
      switch(vt100_args[0]) {
      case 1:
	memset(chars, 0, cy * (SCREEN_WIDTH - 1) + cx);
	break;
      case 2:
	memset(chars, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
	break;
      }
      vt100_state = -1;
      break;
    case 'K': // EL
      switch(vt100_args[0]) {
      case 0:
	for(i = 0; i < ((SCREEN_WIDTH - 1) - cx); i++)
	  chars[cx + i][cy] = 0;
	break;
      case 1:
	for(i = 0; i < cx; i++)
	  chars[i][cy] = 0;
	break;
      case 2:
	for(i = 0; i < SCREEN_WIDTH; i++)
	  chars[i][cy] = 0;
	break;
      }
      vt100_state = -1;
      break;
    case 'd': // VPA
      cy = vt100_args[0];
      if(cy > SCREEN_HEIGHT - 1) cy = SCREEN_HEIGHT - 1;
      vt100_state = -1;
      break;
    case 'e': // VPR
      cy += vt100_args[0];
      if(cy > SCREEN_HEIGHT - 1) cy = SCREEN_HEIGHT - 1;
      vt100_state = -1;
      break;
    case 'P': // DCH
      for(i = cx; i < vt100_args[0]; i++) {
	chars[i][cy] = 0;
      }
      vt100_state = -1;
      break;
    case 'S':
      for(i = 0; i < vt100_args[0]; i++)
	scroll(0);
      vt100_state = -1;
      break;
    case 'T':
      for(i = 0; i < vt100_args[0]; i++)
	scroll(1);
      vt100_state = -1;
      break;
    case 'h': // SM
    case 'l': // RM
      vt100_state = -1;
      break;
    case 'm': // SGR
      switch(vt100_args[0]) {
      case 7:
      case 44:
	rev = 1;
	break;
      case 0:
      case 27:
      case 40:
	rev = 0;
	break;
      }
      vt100_state = -1;
      break;
    case 'r': // DECSTBM
      scroll_top = vt100_args[0];
      scroll_bottom = vt100_args[1] - 1;
      vt100_state = -1;
      break;
    case '?': // DPM
      break;
    case '>': // ?
      while(chars[cx][cy] && cx < SCREEN_WIDTH) cx++;
      vt100_state = -1;
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      vt100_args[vt100_argp] *= 10;
      vt100_args[vt100_argp] += c - '0';
      break;
    default:
      printf("What is %c?\n", c);
      exit(1);
      break;
    }
    break;
  case VT_STATE_CSI_DROPCHAR:
    vt100_state = -1;
    break;
  }
  if(vt100_state < 0) {
    vt100_escape = 0;
    printf("\n");
  }
}

void cputc(char c) {
  if(!c) return;
  if(vt100_escape) {
    vtchar(c);
    update_pixbuf();
    return;
  }
  if(c >= ' ' || c == '\n')
    putchar(c);
  if(c == '\n') {
    cx = 0;
    cy++;
  } else if(c == 0x1b) {
    vt100_escape = 1;
    vt100_state = VT_STATE_READ1;
    vt100_argp = 0;
    int i;
    for(i = 0; i < 16; i++)
      vt100_args[i] = 0;
    return;
  } else if(c == '\b' && cx > 0) {
    cx--;
  } else {
    if(rev) c |= 0x80;
    chars[cx++][cy] = c;
  }
  if(cx > SCREEN_WIDTH - 1) {
    cx = 0;
    cy++;
  }
  if(cy > scroll_bottom) {
    scroll(0);
    cy = scroll_bottom;
  }
  update_pixbuf();
  //  printf("curs: %d %d\n", cx, cy);
}

int main(int argc, char **argv) {
  SDL_Init(SDL_INIT_EVERYTHING);
#ifdef FULLSCREEN
  win = SDL_CreateWindow("sdis2", SDL_WINDOWPOS_CENTERED_DISPLAY(1), SDL_WINDOWPOS_CENTERED_DISPLAY(1), 256, 126, SDL_WINDOW_BORDERLESS | SDL_WINDOW_MAXIMIZED);
#else
  win = SDL_CreateWindow("sdis2", SDL_WINDOWPOS_CENTERED_DISPLAY(0), SDL_WINDOWPOS_CENTERED_DISPLAY(0), 256, 126, 0);
#endif
  ren = SDL_CreateRenderer(win, -1, 0);
  SDL_Texture *texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 256, 126);
  signal(SIGINT, handle_sigint);
  unsigned int pixels[126][256];
  bzero(pixels, 256 * 126 * 4);
  SDL_Event e;
  int q = 0;
  int fdm, fds;
  char inputbuf[80];
  fdm = posix_openpt(O_RDWR);
  if(fdm < 0) {
    perror("posix_openpt");
    exit(1);
  }
  if(grantpt(fdm) < 0) {
    perror("grantpt");
    exit(1);
  }
  if(unlockpt(fdm)) {
    perror("unlockpt");
    exit(1);
  }
  fds = open(ptsname(fdm), O_RDWR);
  if(fork()) {
    close(fds);
    tcgetattr(0, &orig);
    reset_term = 1;
    new = orig;
    new.c_lflag &= ~(ICANON);
    new.c_cc[VMIN] = 0;
    new.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &new);
    tcsetattr(fdm, TCSANOW, &new);
    fd_set fd_in;
    struct timeval zero = {0, 0};
    while(!q) {
      // first, poll our file descriptor
      FD_ZERO(&fd_in);
      FD_SET(fdm, &fd_in);
      FD_SET(0, &fd_in);
      select(fdm + 1, &fd_in, NULL, NULL, &zero);
      if(FD_ISSET(fdm, &fd_in)) {
	int n = read(fdm, inputbuf, 80), i;
	for(i = 0; i < n; i++)
	  cputc(inputbuf[i]);
      }
      if(FD_ISSET(0, &fd_in)) {
	int n = read(0, inputbuf, 80);
	if(n > 0) {
	  write(fdm, inputbuf, n);
	}
      }
      while(SDL_PollEvent(&e)) {
	if(e.type == SDL_QUIT)
	  q = 1;
      }
      int x, y;
      for(x = 0; x < 256; x++) {
	for(y = 0; y < 126; y++) {
#ifdef FULLSCREEN
	  pixels[y][x] = (y * 2) << 16;
	  pixels[y][x] |= x << 8;
	  pixels[y][x] |= !pixbuf[y][x] * 255;
	  pixels[y][x] |= 0xff000000;
#else
	  pixels[y][x] = (y * 2) << 16;
	  pixels[y][x] |= x << 8;
	  pixels[y][x] |= !pixbuf[y][x] * 255;
	  pixels[y][x] |= 0xff000000;
#endif
	}
      }
      SDL_UpdateTexture(texture, NULL, pixels, 256 * 4);
      SDL_RenderClear(ren);
      SDL_RenderCopy(ren, texture, NULL, NULL);
      SDL_RenderPresent(ren);
      struct timespec req = {0, 10000000};
      nanosleep(&req, NULL);
    }
  } else {
    close(fdm);
    tcgetattr(fds, &orig);
    new = orig;
    cfmakeraw(&new);
    new.c_cc[VMIN] = 1;
    new.c_cc[VTIME] = 0;
    new.c_cc[VEOF] = 0x04; // C-d
    new.c_cc[VERASE] = 0x7f; // DEL
    new.c_cc[VINTR] = 0x03; // C-c
    new.c_cc[VQUIT] = 0x1c; // C-\
    new.c_cc[VSUSP] = 0x1a; // C-z
    tcsetattr(fds, TCSANOW, &new);
    close(0);
    close(1);
    close(2);
    dup(fds);
    dup(fds);
    dup(fds);
    close(fds);
    setsid();
    ioctl(0, TIOCSCTTY, 1);
    struct winsize ws = {SCREEN_HEIGHT - 1, SCREEN_WIDTH - 1, 0, 0};
    ioctl(0, TIOCSWINSZ, &ws);
    printf("Initialized\n");
    setenv("TERMTYPE", "dumb", 1);
    execl("/usr/bin/irssi", "irssi", "-c", IRC_SERVER, "-n", IRC_NAME, NULL);
  }
}
	  
