/*
    ettercap -- text only GUI

    Copyright (C) ALoR & NaGA

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

    $Id: ec_text.c,v 1.4 2003/10/13 10:43:50 alor Exp $
*/

#include <ec.h>
#include <ec_poll.h>
#include <ec_ui.h>
#include <ec_threads.h>
#include <ec_hook.h>
#include <ec_interfaces.h>
#include <ec_format.h>
#include <ec_plugins.h>

#include <termios.h>

/* globals */

struct termios old_tc;
struct termios new_tc;


/* proto */

void set_text_interface(void);
void text_interface(void);
static void text_init(void);
static void text_cleanup(void);
static void text_msg(const char *msg);
static void text_error(const char *msg);
static void text_fatal_error(const char *msg);
static void text_input(const char *title, char *input, size_t n);
static void text_help(void);
static void text_progress(int value, int max);
static void text_run_plugin(void);
static void text_stats(void);
static void text_stop_cont(void);
static void text_hosts_list(void);
static void text_profile_list(void);
static void text_visualization(void);

/*******************************************/


void set_text_interface(void)
{
   struct ui_ops ops;

   /* wipe the struct */
   memset(&ops, 0, sizeof(ops));

   /* register the functions */
   ops.init = &text_init;
   ops.start = &text_interface;
   ops.cleanup = &text_cleanup;
   ops.msg = &text_msg;
   ops.error = &text_error;
   ops.fatal_error = &text_fatal_error;
   ops.input = &text_input;
   ops.progress = &text_progress;
   ops.type = UI_TEXT;
   
   ui_register(&ops);
   
   /*
    * add the hook to dispatcher to print the
    * packets in the right format
    */
   hook_add(HOOK_DISPATCHER, text_print_packet);
}

/*
 * set the terminal as non blocking 
 */

static void text_init(void)
{
   /* taken from readchar.c, by M. Andreoli (2000) */
   
   tcgetattr(0, &old_tc);
   new_tc = old_tc;
   new_tc.c_lflag &= ~(ECHO | ICANON);   /* raw output */
   new_tc.c_cc[VTIME] = 1;

   tcsetattr(0, TCSANOW, &new_tc);
}

/*
 * reset to the previous state
 */

static void text_cleanup(void)
{
   /* flush the last user messages */
   ui_msg_flush(MSG_ALL);

   fprintf(stdout, "\n");
   
   tcsetattr(0, TCSANOW, &old_tc);
}

/*
 * print a USER_MSG()
 */

static void text_msg(const char *msg)
{
   /* avoid implicit format bugs */
   fprintf(stdout, "%s", msg);
   /* allow non buffered messages */
   fflush(stdout);
}


/*
 * print an error
 */
static void text_error(const char *msg)
{
   /* avoid implicit format bugs */
   fprintf(stdout, "\nFATAL: %s\n\n", msg);
   /* allow non buffered messages */
   fflush(stdout);
}


/*
 * handle a fatal error and exit
 */
static void text_fatal_error(const char *msg)
{
   /* avoid implicit format bugs */
   fprintf(stdout, "\nFATAL: %s\n\n", msg);
   /* allow non buffered messages */
   fflush(stdout);

   /* exit without calling atexit() */
   _exit(-1);
}


/*
 * display the 'title' and get the 'input' from the user
 */
static void text_input(const char *title, char *input, size_t n)
{
   char *p;
   
   /* display the title */
   fprintf(stdout, "%s", title);
   fflush(stdout);

   /* repristinate the buffer input */
   tcsetattr(0, TCSANOW, &old_tc);

   /* flush the buffer */
   fflush(stdin);
   
   /* get the user input */
   fgets(input, n, stdin);

   /* trim the \n */
   if ((p = strrchr(input, '\n')) != NULL)
      *p = '\0';

   /* disable buffered input */
   tcsetattr(0, TCSANOW, &new_tc);
}

/* 
 * implement the progress bar 
 */
static void text_progress(int value, int max)
{
   float percent;
   int i;
  
   /* calculate the percent */
   percent = (float)(value)*100/(max);
            
   /* 
    * we use stderr to avoid scrambling of 
    * logfile generated by: ./ettercap -C > logfile 
    */
         
   switch(value % 4) {
      case 0:
         fprintf(stderr, "\r| |");
      break;
      case 1:
         fprintf(stderr, "\r/ |");
      break;
      case 2:
         fprintf(stderr, "\r- |");
      break;
      case 3:
         fprintf(stderr, "\r\\ |");
      break;
   }

   /* fill the bar */
   for (i=0; i < percent/2; i++)
      fprintf(stderr, "=");

   fprintf(stderr, ">");

   /* fill the empty part of the bar */
   for(; i < 50; i++)
      fprintf(stderr, " ");
                              
   fprintf(stderr, "| %6.2f %%", percent );

   fflush(stderr);

   if (value == max) 
      fprintf(stderr, "\r* |==================================================>| 100.00 %%\n\n");
                     
}


/* the interface */

void text_interface(void)
{
   DEBUG_MSG("text_interface");

   /* it is difficult to be interactive while reading from file... */
   if (!GBL_OPTIONS->read) {
      USER_MSG("\nText only Interface activated...\n");
      USER_MSG("Hit 'h' for inline help\n\n");
   }
   
   /* flush all the messages */
   ui_msg_flush(MSG_ALL);
  
   /* if we have to activate a plugin */
   if (GBL_OPTIONS->plugin) {
      /* 
       * execute the plugin and close the interface if 
       * the plugin was not found or it has completed
       * its execution
       */
      if (text_plugin(GBL_OPTIONS->plugin) != PLUGIN_RUNNING)
         /* end the interface */
         return;
   }
 
  
   /* neverending loop for user input */
   LOOP {
   
      CANCELLATION_POINT();
      
      /* XXX --  1 millisecond is too slow 
       * but 0 will eat up all the CPU time...
       * 
       * FIND A SOLUTION !!
       * maybe an  "else usleep(1);"
       * or increase the number of USER_MSG processed
       */
        
      /* if there is a pending char to be read */
      if (ec_poll_read(fileno(stdin), 1)) {
         
         char ch = 0;
         ch = getchar();
         switch(ch) {
            case 'H':
            case 'h':
               text_help();
               break;
            case 'P':
            case 'p':
               text_run_plugin();
               break;
            case 'S':
            case 's':
               text_stats();
               break;
            case 'L':
            case 'l':
               text_hosts_list();
               break;
            case 'V':
            case 'v':
               text_visualization();
               break;
            case 'O':
            case 'o':
               text_profile_list();
               break;
            case 'C':
            case 'c':
               text_connections();
               break;
            case ' ':
               text_stop_cont();
               break;
            case 'Q':
            case 'q':
               USER_MSG("Closing text interface...\n\n");
               return;
               break;
         }
                                                                           
      }

      /* print pending USER_MSG messages */
      ui_msg_flush(10);
                                 
   }
  
   /* NOT REACHED */
   
}

/* print the help screen */

static void text_help(void)
{
   USER_MSG("\nInline help:\n\n");
   USER_MSG(" [vV]      - change the visualization mode\n");
   USER_MSG(" [pP]      - activate a plugin\n");
   USER_MSG(" [lL]      - print the hosts list\n");
   USER_MSG(" [oO]      - print the profiles list\n");
   USER_MSG(" [cC]      - print the connections list\n");
   USER_MSG(" [sS]      - print interfaces statistics\n");
   USER_MSG(" [<space>] - stop/cont printing packets\n");
   USER_MSG(" [qQ]      - quit\n\n");
}
               
/* 
 * stops or continues to print packets
 * it is another way to control the -q option
 */

static void text_stop_cont(void)
{
   /* revert the quiet option */   
   GBL_OPTIONS->quiet = (GBL_OPTIONS->quiet) ? 0 : 1; 

   if (GBL_OPTIONS->quiet)
      fprintf(stderr, "\nPacket visualization stopped...\n");
   else
      fprintf(stderr, "\nPacket visualization restarted...\n");
}


/*
 * display a list of plugin, and prompt 
 * the user for a plugin to run.
 */
static void text_run_plugin(void)
{
   char name[20];
   int restore = 0;
   char *p;
      
   /* there are no plugins */
   if (text_plugin("list") == -ENOTFOUND)
      return;
   
   /* stop the visualization while the plugin interface is running */
   if (!GBL_OPTIONS->quiet) {
      text_stop_cont();
      restore = 1;
   }
   
   /* print the messages created by text_plugin */
   ui_msg_flush(MSG_ALL);
      
   /* repristinate the buffer input */
   tcsetattr(0, TCSANOW, &old_tc);

   fprintf(stdout, "Plugin name (0 to quit): ");
   fflush(stdout);
   
   /* flush the buffer */
   fflush(stdin);
   
   /* get the user input */
   fgets(name, 20, stdin);

   /* trim the \n */
   if ((p = strrchr(name, '\n')) != NULL)
      *p = '\0';
  
   /* disable buffered input */
   tcsetattr(0, TCSANOW, &new_tc);
   
   if (!strcmp(name, "0")) {
      text_stop_cont();
      return;
   }

   /* run the plugin */
   text_plugin(name);
   
   /* continue the visualization */
   if (restore)
      text_stop_cont();
   
}

/*
 * print the interface statistics 
 */
static void text_stats(void)
{
   DEBUG_MSG("text_stats (pcap) : %d %d %d", GBL_STATS->ps_recv, 
                                                GBL_STATS->ps_drop,
                                                GBL_STATS->ps_ifdrop);
   DEBUG_MSG("text_stats (BH) : [%d][%d] p/s -- [%d][%d] b/s", 
         GBL_STATS->bh.rate_adv, GBL_STATS->bh.rate_worst, 
         GBL_STATS->bh.thru_adv, GBL_STATS->bh.thru_worst); 
   
   DEBUG_MSG("text_stats (TH) : [%d][%d] p/s -- [%d][%d] b/s", 
         GBL_STATS->th.rate_adv, GBL_STATS->th.rate_worst, 
         GBL_STATS->th.thru_adv, GBL_STATS->th.thru_worst); 
   
   DEBUG_MSG("text_stats (queue) : %d %d", GBL_STATS->queue_curr, GBL_STATS->queue_max); 
  
   
   fprintf(stdout, "\n Received packets    : %lld\n", GBL_STATS->ps_recv);
   fprintf(stdout,   " Dropped packets     : %lld\n", GBL_STATS->ps_drop);
   fprintf(stdout,   " Lost percentage     : %.2f %%\n\n", 
         (GBL_STATS->ps_recv) ? (float)GBL_STATS->ps_drop * 100 / GBL_STATS->ps_recv
                              : 0 );
   
   fprintf(stdout,   " Current queue len   : %d\n", GBL_STATS->queue_curr);
   fprintf(stdout,   " Max queue len       : %d\n\n", GBL_STATS->queue_max);
   
   fprintf(stdout,   " Sampling rate       : %d\n\n", GBL_CONF->sampling_rate);
   
   fprintf(stdout,   " Bottom Half received packet : pck: %8lld  byte: %8lld\n", 
         GBL_STATS->bh.pck_recv, GBL_STATS->bh.pck_size);
   fprintf(stdout,   " Top Half received packet    : pck: %8lld  byte: %8lld\n", 
         GBL_STATS->th.pck_recv, GBL_STATS->th.pck_size);
   fprintf(stdout,   " Interesting packets         : %.2f %%\n\n",
         (GBL_STATS->bh.pck_recv) ? (float)GBL_STATS->th.pck_recv * 100 / GBL_STATS->bh.pck_recv : 0 );

   fprintf(stdout,   " Bottom Half packet rate : worst: %8d  adv: %8d p/s\n", 
         GBL_STATS->bh.rate_worst, GBL_STATS->bh.rate_adv);
   fprintf(stdout,   " Top Half packet rate    : worst: %8d  adv: %8d p/s\n\n", 
         GBL_STATS->th.rate_worst, GBL_STATS->th.rate_adv);
   
   fprintf(stdout,   " Bottom Half thruoutput  : worst: %8d  adv: %8d b/s\n", 
         GBL_STATS->bh.thru_worst, GBL_STATS->bh.thru_adv);
   fprintf(stdout,   " Top Half thruoutput     : worst: %8d  adv: %8d b/s\n\n", 
         GBL_STATS->th.thru_worst, GBL_STATS->th.thru_adv);
}

/*
 * prints the hosts list
 */

static void text_hosts_list(void)
{
   struct hosts_list *hl;
   char ip[MAX_ASCII_ADDR_LEN];
   char mac[MAX_ASCII_ADDR_LEN];
   int i = 1;

   fprintf(stdout, "\n\nHosts list:\n\n");
   
   /* print the list */
   LIST_FOREACH(hl, &GBL_HOSTLIST, next) {
      
      ip_addr_ntoa(&hl->ip, ip);
      mac_addr_ntoa(hl->mac, mac);
     
      if (hl->hostname)
         fprintf(stdout, "%d)\t%s\t%s\t%s\n", i++, ip, mac, hl->hostname);
      else
         fprintf(stdout, "%d)\t%s\t%s\n", i++, ip, mac);
         
   }

   fprintf(stdout, "\n\n");

}

/* 
 * prompt the user for the visualization mode
 */

static void text_visualization(void)
{
   char format[15];
   
   /* stop the packet printing */
   text_stop_cont();

   /* repristinate the buffere input */
   tcsetattr(0, TCSANOW, &old_tc);

   fprintf(stdout, "\n\nVisualization format: ");
   fflush(stdout);
   
   scanf("%15s", format);
  
   /* disable buffered input */
   tcsetattr(0, TCSANOW, &new_tc);
  
   /* set the format */
   set_format(format);   
   
   /* continue the packet printing */
   text_stop_cont();
}


/*
 * enter the profile interface 
 */

static void text_profile_list(void)
{
   int restore = 0;
   
   /* stop the visualization while the profiles interface is running */
   if (!GBL_OPTIONS->quiet) {
      text_stop_cont();
      restore = 1;
   }

   /* execute the profiles interface */
   text_profiles();

   /* continue the visualization */
   if (restore)
      text_stop_cont();
}

/* EOF */

// vim:ts=3:expandtab

