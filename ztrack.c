/* ztrack - simple curses-based pseudo-3D driving game
 * public domain by Russell Marks 951025
 * v1.0 - 951027 - first working version :-)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <curses.h>


#ifdef HAVE_RANDOM
#define rand random
#define srand srandom
#endif

#if defined(ACS_HLINE) && !defined(NO_COLOUR) && !defined(NO_COLOR)
#define HAVE_COLOUR
#endif


/* fixed-point defs */
typedef int fixed;
#define FIX_UP(x)	((x)<<10)	/* move from int to fixed (x1024) */
#define FIX_DOWN(x)	((x)>>10)	/* move from fixed to int */


/* max other cars
 * they only exist from 4 'positions' in front to 4 behind */
#define MAX_ENEMY	10

/* since the object of the enemy is to crash into the player,
 * make their top speed lower.
 */
#define MAX_PLYR_SPEED		FIX_UP(150)
#define MAX_ENEMY_SPEED		FIX_UP(135)

/* how much we accel/decel per frame */
#define PLYR_ACCEL		FIX_UP(2)
#define PLYR_DECEL		FIX_UP(4)
#define ENEMY_ACCEL		FIX_UP(4)
#define ENEMY_DECEL		FIX_UP(2)

/* how much to divide by to turn speeds into amount to add to rel. pos.
 * basically, lower = faster
 */
#define SPEED_FUDGE		200

/* response time (in frames) of computer lane-changing */
#define ENEMY_RESP_TIME		5		/* half-sec */


/* car 'sprites' in carspr[] */
#include "carspr.h"



char clouds[3][80];

int ttyfd,colour=0;

/* the other cars' positions are given relative to the player.
 * this saves messing about with a track, etc.
 * in comments below, all absolute unless specified.
 */
struct enemy_t {
  int active;		/* non-zero if this element in use */
  int lane;		/* lane position */
  fixed ypos;		/* relative position along the road; */
  /* -ve = behind, +ve = in front, 0 = level. onscreen positions
   * are FIX_DOWN(ypos)==4,3,2,1. */
  fixed speed,target;	/* current and target speed */
  int timer,tlane;	/* timer must reach 0 before lane=tlane */
  } enemy[MAX_ENEMY];


/* accel/decel settings */
#define SPDMODE_ACCEL	  1
#define SPDMODE_CONST	  0
#define SPDMODE_DECEL	(-1)



/* this is pretty simple at the moment */
void use_colour(colnum,other_attr)
int colnum,other_attr;
{
#ifdef HAVE_COLOUR
if(colnum==0)
  attrset(A_NORMAL);
else
  if(colour)
    attrset(COLOR_PAIR(colnum)|other_attr);
  else
    attrset(other_attr);
#endif
}


void clearscreen()
{
use_colour(COLOR_WHITE,A_NORMAL);	/* just in case */
wclear(stdscr);
move(0,0); refresh();
}


void screenon()
{
initscr();
cbreak(); noecho();
if((ttyfd=open("/dev/tty",O_RDONLY|O_NONBLOCK))<0)
  return;
}


void screenoff()
{
close(ttyfd);
clearscreen();
echo(); nocbreak();
endwin();
putchar('\n');
}


int getnbkey()
{
unsigned char c;

if(read(ttyfd,&c,1)==-1)
  return(-1);
else
  return((int)c);
}


void randomize()
{
srand(time(NULL));
}


void define_colours()
{
#ifndef HAVE_COLOUR
/* this is a nop without colour support */
#else
int f;

/* make it use colour */
start_color();

colour=has_colors();

if(!colour) return;

/* declare colour pairs we need */
/* we do it like this; every colour available is allocated to
 * the colour_number pair, with black as the background.
 * (black on black is pointless and isn't defined)
 * then the use_colour routine can be used;
 *  use_colour(COLOR_RED,A_BOLD);
 * would give bright red. attrset(A_NORMAL) should be used to
 * reset to normal text.
 *
 * we assume the colours are defined to 1..7 which is losing but
 * makes things really easy. :-)
 */

for(f=1;f<=7;f++)
  init_pair(f,f,0);
#endif
}



void init()
{
randomize();

/* init clouds, these are only setup once */
strcpy(clouds[0],
"_,,/////////////,,,,____              \
      ,,,////////////, ,,                ");
strcpy(clouds[1],
"     ''   ''           ---=======---- \
           '   '''' '                    ");
strcpy(clouds[2],
"    ---==========----____,,,-----     \
     ----============--                  ");

use_colour(COLOR_CYAN,A_NORMAL);

mvaddstr(0,0,clouds[0]);
mvaddstr(1,0,clouds[1]);
mvaddstr(2,0,clouds[2]);
}


void stashcursor()
{
move(23,0);
}


void drawscrn()
{
stashcursor();
refresh();
}


/* draw landscape depending on whether in lane 0, 1 or 2 */
void landscape(int lane)
{
int f,x;

use_colour(COLOR_GREEN,A_NORMAL);

/* reset line 3 to spaces and 4 to underscores */
move(3,0); for(f=0;f<79;f++) addch(' ');
move(4,0); for(f=0;f<79;f++) addch('_');

/* now draw in the 'landscape' with a suitable offset */
x=10-(lane-1)*3;
mvaddstr(3,x+11,"___");
mvaddstr(4,x,",-^---._,-'   ^^--..__________,._______________..------..");
}


/* clear main part of the screen, where the track and cars are drawn */
void clear_main()
{
int f;

for(f=5;f<21;f++)
  move(f,0),clrtoeol();
}


void mycar(int damaged)
{
use_colour(COLOR_MAGENTA,A_BOLD);

if(damaged)	/* non-zero if car has crashed */
  {
  mvaddstr(21,20," /     _                    __       \\ ");
  mvaddstr(22,20,"/    ,' `-~~~'`-__/^---__,-^  ^--.    \\");
  /* also crack wing mirrors */
  use_colour(COLOR_YELLOW,A_BOLD);
  mvaddstr(22,10,"[-xXXx-]");
  mvaddstr(22,61,"[-xXXx-]");
  }
else
  {
  mvaddstr(21,20," /     _________________________     \\ ");
  mvaddstr(22,20,"/    ,'                         `.    \\");
  }
}


void draw_lanes(int lane)
{
int x,y;

use_colour(COLOR_MAGENTA,A_NORMAL);

if(lane==2)
  {
  /* border 1, leftmost */
  mvaddch(10,0,'\'');
  for(y=9,x=1;y>=5;y--,x+=6)
    mvaddstr(y,x,"__---'");
  }

if(lane>=1)
  {
  /* border 2 */
  mvaddstr(16,0,"-'");
  for(y=15,x=2;y>=5;y--,x+=3)
    mvaddstr(y,x,"_-'");
  }
  
/* borders 3 and 4, middle two, always drawn */
use_colour(COLOR_MAGENTA,A_BOLD);
for(y=22,x=20;y>=5;y--,x++)
  mvaddch(y,x,'/');
for(y=22,x=58;y>=5;y--,x--)
  mvaddch(y,x,'\\');

use_colour(COLOR_MAGENTA,A_NORMAL);

if(lane<=1)
  {
  /* border 5 */
  mvaddstr(16,77,"`-");
  for(y=15,x=74;y>=5;y--,x-=3)
    mvaddstr(y,x,"`-_");
  }
  
if(lane==0)
  {
  /* border 6, rightmost*/
  mvaddch(10,78,'`');
  for(y=9,x=72;y>=5;y--,x-=6)
    mvaddstr(y,x,"`---__");
  }
}


/* init enemy car array - just blank it */
void init_enemies()
{
int f;

for(f=0;f<MAX_ENEMY;f++) enemy[f].active=0;
}


/* draw sprite from carspr */
void draw_car_sprite(int x,int y)	/* x,y pos in carspr[] array */
{
int a,b,ox,oy,lastx;
struct carspr_t *cp;
char *ptr;

use_colour(COLOR_YELLOW,(y>1)?A_BOLD:A_NORMAL);

cp=&carspr[y][x];

for(b=0,oy=cp->y;b<MAX_SPR_Y;b++,oy++)
  {
  ptr=cp->d[b];
  move(oy,cp->x);
  for(a=0,lastx=ox=cp->x;a<MAX_SPR_X;a++,ox++,ptr++)
    if(*ptr!=32)
      {
      /* we optimise cursor movement by hand in case curses is a bit dim */
      if(lastx!=ox) move(oy,ox),lastx=ox;
      addch(*ptr);
      lastx++;
      }
  }
}


/* draw car, if possible */
void draw_car(struct enemy_t *car,int lane)
{
int x,y;

if(!car->active) return;
y=FIX_DOWN(car->ypos);
if(y<1 || y>4) return;

x=car->lane-lane;

/* if we got here, we can draw it. */
draw_car_sprite(x+2,4-y);
}


/* returns non-zero if enemy at stated pos.
 * the ypos is rounded with FIX_DOWN(), like it is for onscreen pos.
 */
int enemy_at(fixed ypos,int lane)
{
int f,y=FIX_DOWN(ypos);

for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active)
    if(enemy[f].lane==lane && FIX_DOWN(enemy[f].ypos)==y)
      return(1);

return(0);
}


void create_enemy(fixed plyr_speed)
{
int f,tmp;

/* find a blank space in the array */
for(f=0;f<MAX_ENEMY;f++)
  if(!enemy[f].active)
    {
    enemy[f].lane=rand()%3;
    enemy[f].timer=0;
    tmp=rand()%MAX_ENEMY_SPEED;
    if(tmp<70) tmp=70;
    enemy[f].speed=enemy[f].target=tmp;
    /* if we're going faster than player, start us behind them,
     * else in front.
     */
    enemy[f].ypos=(enemy[f].speed>plyr_speed)?FIX_UP(-4):FIX_UP(4);
    /* if there's one where we want to be, forget it */
    if(enemy_at(enemy[f].ypos,enemy[f].lane)) return;
    /* otherwise, it's now valid */
    enemy[f].active=1;
    return;
    }
}


/* do all the enemy stuff
 * return value is whether player tail-gated a car this go or not
 * (that's the only collision which is allowed)
 */
int do_enemy(int plyr_lane,fixed plyr_speed)
{
int f,tmp;

/* first, correct relative positions according to player speed
 * and enemy speed.
 */
for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active)
    enemy[f].ypos+=(enemy[f].speed - plyr_speed)/SPEED_FUDGE;

/* drop any which have fallen off the edge of the world */
for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active)
    {
    if(enemy[f].ypos>=FIX_UP(5) || enemy[f].ypos<=FIX_UP(-5))
      enemy[f].active=0;
    }

/* possibly create a new one */
/* want to make it more likely with increased plyr speed...? */
if(rand()%7==0) create_enemy(plyr_speed);

/* now let them make their move */
for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active)
    {
    /* countdown lane-change response time timeout */
    if(enemy[f].timer)
      {
      enemy[f].timer--;
      if(enemy[f].timer==0)
        {
        /* don't allow if some enemy already there or if player there */
        if(!enemy_at(enemy[f].ypos,enemy[f].tlane) &&
           FIX_DOWN(enemy[f].ypos)!=0)
          enemy[f].lane=enemy[f].tlane;
        }
      }
    
    /* if we're behind player and in same lane, change */
    if(enemy[f].ypos<=FIX_UP(-1) && enemy[f].lane==plyr_lane &&
        enemy[f].timer==0)
      {
      tmp=(enemy[f].lane+1)%3;
      if(rand()&1) tmp=(enemy[f].lane+4)%3;	/* i.e. lane-1 mod 3 */
      enemy[f].timer=ENEMY_RESP_TIME;
      enemy[f].tlane=tmp;
      }

#if 0		/* this is really seriously crap */
    /* now target speed stuff. */
    /* if (far) behind, aim for player speed + 10. */
    if(enemy[f].ypos<FIX_UP(-2)) enemy[f].target=plyr_speed+FIX_UP(10);
    
    /* if (far) in front, aim for player speed. */
    if(enemy[f].ypos>FIX_UP( 2)) enemy[f].target=plyr_speed;
#endif
    }

#if 0		/* not needed for now */
/* do speed add/sub to aim for target */
for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active && enemy[f].speed!=enemy[f].target)
    {
    tmp=enemy[f].target-enemy[f].speed;
    if(tmp>0)
      {
      /* too slow, accelerate */
      if(tmp>ENEMY_ACCEL) tmp=ENEMY_ACCEL;
      enemy[f].speed+=tmp;
      }
    else
      {
      /* too fast, decelerate */
      tmp=-tmp;
      if(tmp>ENEMY_DECEL) tmp=ENEMY_DECEL;
      enemy[f].speed-=tmp;
      }
    }
#endif

/* check for 'impossible' things we need to fix */
for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active)
    {
    /* if they've run into the back of the player, push them back */
    if(enemy[f].ypos>=0 && enemy[f].ypos<FIX_UP(1) &&
       enemy[f].lane==plyr_lane && enemy[f].speed>plyr_speed)
      enemy[f].ypos=FIX_UP(-1);
    /* XXX that can result in two enemy on same spot :-( */
    }

/* draw any cars onscreen */
for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active)
    draw_car(&enemy[f],plyr_lane);


/* see if a car hit the player from the front.
 * we interpret this as FIX_DOWN(ypos)==0 and enemy speed < player speed.
 * and same lane, of course. :-)
 * this MUST be the last bit in the routine, as it may return.
 */
for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active)
    {
    if(FIX_DOWN(enemy[f].ypos)==0 && enemy[f].speed<plyr_speed &&
       enemy[f].lane==plyr_lane)
      return(1);
    }

return(0);
}



/* test sprites */
void test()
{
int a,b;

clear_main();
landscape(1);

for(a=0;a<3;a++) draw_lanes(a);
  
for(b=0;b<4;b++)
  for(a=0;a<5;a++)
    draw_car_sprite(a,b);

drawscrn();
while(getnbkey()==-1) usleep(100000);
}


void draw_mirrors(int lane)
{
static char mir_none[]="[      ]",mir_car[]="[ .##. ]";
int f,l=0,r=0;

for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active)
    if(FIX_DOWN(enemy[f].ypos)==0)
      if(enemy[f].lane==lane-1)
        l=1;
      else
        if(enemy[f].lane==lane+1)
          r=1;

use_colour(COLOR_YELLOW,A_BOLD);

mvaddstr(22,10,l?mir_car:mir_none);
mvaddstr(22,61,r?mir_car:mir_none);
}



#ifdef DEBUG
void draw_map(int lane)
{
int y,x,f,yp;

use_colour(COLOR_WHITE,A_BOLD);

for(y=0;y<9;y++)
  for(x=0;x<3;x++)
    mvaddch(y,x,'.');

for(f=0;f<MAX_ENEMY;f++)
  if(enemy[f].active && (yp=FIX_DOWN(enemy[f].ypos))<=4 && yp>=-4)
    mvaddch(4-yp,enemy[f].lane,'x');

mvaddch(4,lane,'*');
}
#endif


void draw_status(int score,int lives,fixed speed,int spd_mode)
{
use_colour(COLOR_CYAN,A_NORMAL);
mvprintw(23,12,"< score: %08d   cars: %d   speed: %3d mph   [%s] >",
	score,lives,FIX_DOWN(speed),
        (spd_mode==SPDMODE_CONST)?"const":
         (spd_mode==SPDMODE_ACCEL)?"accel":"decel");
}


/* 'scroll' string right. assumes len>=2 */
void rscroll(char *str)
{
int len=strlen(str);
char tmp;

tmp=str[len-1];
memmove(str+1,str,len-1);
*str=tmp;
}


void move_clouds()
{
static int move3rd=0;

use_colour(COLOR_CYAN,A_NORMAL);

rscroll(clouds[0]); mvaddstr(0,0,clouds[0]);
rscroll(clouds[1]); mvaddstr(1,0,clouds[1]);

if(move3rd) rscroll(clouds[2]),mvaddstr(2,0,clouds[2]);

move3rd=!move3rd;
}


void lose_input()
{
while(getnbkey()!=-1);
}



/* returns final score */
int game()
{
int lane,plyr_hit,f;
unsigned int frames=0;
int quit=0;
int spd_mode;
fixed speed=0;
int score=0;
int lives=3;

/* setup initial screen */
clearscreen();
mycar(0);
move_clouds();		/* to make sure they're drawn */
move_clouds();

while(!quit && lives>0)
  {
  plyr_hit=0;
  speed=0; spd_mode=SPDMODE_CONST;
  init_enemies();
  lane=1;

  /* put a couple of cars about */
  for(f=0;f<3;f++) create_enemy(speed);

  lose_input();
  
  while(!quit && !plyr_hit)
    {
    switch(getnbkey())
      {
      case 'q': spd_mode=SPDMODE_ACCEL; break;
      case 'a': spd_mode=SPDMODE_DECEL; break;
      case 32:
      case   9: spd_mode=SPDMODE_CONST; break;	/* 9 = ^I = TAB */
      case 'o': if(lane>0) lane--; break;
      case 'p': if(lane<2) lane++; break;
      
      case 'X': case 27: quit=1;
      }
  
    /* deal with remaining player movement */
    switch(spd_mode)
      {
      case SPDMODE_ACCEL:
        speed+=PLYR_ACCEL; break;
      case SPDMODE_DECEL:
        speed-=PLYR_DECEL; break;
      /* as no 'friction', SPDMODE_CONST is a nop */
      }
    
    if(speed<0) speed=0;
    if(speed>MAX_PLYR_SPEED) speed=MAX_PLYR_SPEED;
  
    /* draw screen except enemy cars */  
    landscape(lane);
    clear_main();
    draw_lanes(lane);
  
    /* fix enemy positions relative, do their move, draw and check hit */
    plyr_hit=do_enemy(lane,speed);
    draw_mirrors(lane);
  
  #ifdef DEBUG
    draw_map(lane);
  #endif
    
    draw_status(score,lives,speed,spd_mode);
    if(frames%20==19) move_clouds();
      
    drawscrn();
    
    /* don't wait if hit; it looks odd */
    if(!plyr_hit) usleep(100000);
    frames++;
    score+=(FIX_DOWN(speed)/5);
    }
  
  if(plyr_hit)
    {
    lives--;
    draw_status(score,lives,0,spd_mode);
    mycar(1);			/* show crash */
    use_colour(COLOR_YELLOW,A_BOLD);
    mvaddstr(10,30,",------------------.");
    mvaddstr(11,30,"| CRASH! LIFE LOST |");
    mvaddstr(12,30,"`------------------'");
    lose_input(); drawscrn();
    sleep(2);
    if(lives==0)
      {
      mvaddstr(11,32,"G A M E  O V E R");
      lose_input(); drawscrn();
      sleep(2);
      }
    else
      mycar(0);
    }
  }

return(score);
}


/* returns non-zero if want to quit */
int title(int high)
{
static char logo[7][80]={
  "        ________________________________"
  "_       _ ,,,..   _______  __    __    ",
  "       /________     ____   ____   __   "
  "/ --==,' \\===''~ /  ____/ /  / ,' /    ",
  "----=======- ,-'  ,-'   /  /   /  /_/  /"
  "    ,'    \\     /  /     /  /,' ,'     ",
  "_________ ,-'  ,-'____ /  /__ /    ___/_"
  "_ ,'  ,'\\  \\ _ /  /____ /     ,'_______",
  "%%%%%%%,-'  ,-'%%%%%% /  /%% /  .  \\%%%%"
  ",'   '--`   \\%/  /%%%%%/  /\\  \\%%%%%%%%",
  "::::,-'  ,-'_______::/  /:::/  /:\\  \\_,'"
  "  ,-------.  `  /_____/  /::\\  \\:::::::",
  ".../______________/./__/.../__/...\\_____"
  ",'.........`.___________/....\\__\\......"};

int c;

clearscreen();

/* draw logo */
use_colour(COLOR_CYAN,A_NORMAL);
mvaddstr(0,0,logo[0]);
use_colour(COLOR_CYAN,A_BOLD);
mvaddstr(1,0,logo[1]);
use_colour(COLOR_YELLOW,A_BOLD);
mvaddstr(2,0,logo[2]);
mvaddstr(3,0,logo[3]);
use_colour(COLOR_MAGENTA,A_BOLD);
mvaddstr(4,0,logo[4]);
use_colour(COLOR_RED,A_BOLD);
mvaddstr(5,0,logo[5]);
use_colour(COLOR_GREEN,A_NORMAL);
mvaddstr(6,0,logo[6]);
use_colour(COLOR_CYAN,A_NORMAL);
mvaddstr(7,0,logo[7]);

use_colour(COLOR_GREEN,A_BOLD);
mvaddstr(10,18,"Press:");
use_colour(COLOR_YELLOW,A_BOLD);
mvaddstr(13,30,"[Space]      Play");
mvaddstr(15,30,"[Esc]        Quit");

use_colour(COLOR_CYAN,A_NORMAL);
mvaddstr(21,5,
  "Keys:  Q = accel mode  A = decel mode  Space = const (cruise) mode");
use_colour(COLOR_GREEN,A_NORMAL);
mvaddstr(22,5,
  "         O = move left a lane  P = move right a lane  Esc = quit");

use_colour(COLOR_GREEN,A_BOLD);
mvprintw(18,24,"Current high score: %08d",high);

drawscrn();

while((c=getnbkey())!=' ' && c!='q' && c!=27)
  usleep(200000);

return(c==' '?0:1);
}



int main()
{
int quit=0;
int score,high=0;

screenon();
define_colours();
init();

while(!quit)
  {
  quit=title(high);
  if(!quit)
    {
    score=game();
    if(score>high) high=score;
    }
  }

screenoff();
exit(0);
}
