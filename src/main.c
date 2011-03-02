/*
 *  Adaptive search
 *
 *  Copyright (C) 2002-2011 Daniel Diaz, Philippe Codognet and Salvador Abreu
 *
 *  main.c: benchmark main function
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined MPI
#include <mpi.h>
/*#include <unistd.h>*/                   /* sleep() */
#include <math.h>
#include <time.h>                         /* time() */
#include <sys/time.h>                     /* gettimeofday() */
#include <limits.h>                       /* To initiate first random seed */
#if defined PRINT_COSTS
#include <unistd.h>                       /* sleep() */
#include <sys/types.h>                    /* open()... */
#include <sys/stat.h>                     /* and S_IRUSR... */
#include <fcntl.h>                        /* and O_WDONLY */
#include <string.h>                       /* strdup() */
#endif
#endif /* MPI */

#include "ad_solver.h"

/*-----------*
 * Constants *
 *-----------*/

/*-------*
 * Types *
 *-------*/

/*------------------*
 * Global variables *
 *------------------*/

int nb_threads;			/* must be ld-global! */

static int count;
static int disp_mode;
static int check_valid;
static int read_initial;	/* 0=no, 1=yes, 2=all threads use the same (CELL specific) */


int param_needed;		/* overwritten by benches if an argument is needed (> 0 = integer, < 0 = file name) */
char *user_stat_name;		/* overwritten by benches if a user statistics is needed */
int (*user_stat_fct)(AdData *p_ad); /* overwritten by benches if a user statistics is needed */

#if defined PRINT_COSTS
  char * filename_pattern_print_cost ;
#endif

/*------------*
 * Prototypes *
 *------------*/

static void Set_Initial(AdData *p_ad);

static void Verify_Sol(AdData *p_ad);

static void Parse_Cmd_Line(int argc, char *argv[], AdData *p_ad);

#define Div_Round_Up(x, y)   (((x) + (y) - 1) / (y))

static void AS_set_p_ad_seed(AdData * p_ad) ;        /* Randomize p_ad->seed */

/* provided by each bench */

void Init_Parameters(AdData *p_ad);

int Check_Solution(AdData *p_ad);



#ifdef CELL

#define User_Time    Real_Time
#define Solve(p_ad)  SolveStub(p_ad)

void SolveStub(AdData *p_ad);

#else  /* !CELL */

void Solve(AdData *p_ad);

#endif	/* !CELL */

#if defined MPI               /* Should be used by all configuration! */
/* Carefull: input and P has to be in [0 1] */
double alea_chaos( double input, double P )
{
  if( input < P )
    return (input/P) ;
  else
    return (1-input)/(1-P) ;
}
#endif

/*
 *  MAIN
 *
 */

int
main(int argc, char *argv[])
{
  static AdData data;		/* to be init with 0 (debug only) */
  AdData *p_ad = &data;
  int i, user_stat = 0;

  double time_one0, time_one;
  double nb_same_var_by_iter, nb_same_var_by_iter_tot;

  int    nb_iter_cum;
  int    nb_local_min_cum;
  int    nb_swap_cum;
  int    nb_reset_cum;
  double nb_same_var_by_iter_cum;


  int    nb_restart_cum,              nb_restart_min,              nb_restart_max;
  double time_cum,                    time_min,                    time_max;

  int    nb_iter_tot_cum,             nb_iter_tot_min,             nb_iter_tot_max;
  int    nb_local_min_tot_cum,        nb_local_min_tot_min,        nb_local_min_tot_max;
  int    nb_swap_tot_cum,             nb_swap_tot_min,             nb_swap_tot_max;
  int    nb_reset_tot_cum,            nb_reset_tot_min,            nb_reset_tot_max;
  double nb_same_var_by_iter_tot_cum, nb_same_var_by_iter_tot_min, nb_same_var_by_iter_tot_max;

  int    user_stat_cum,               user_stat_min,               user_stat_max;
  char buff[256], str[32];

#if defined MPI
  int flag ;
  tegami * tmp_message ;
  long int print_seed ;
  double seed_chaos ;
  struct timeval tv ;
  char results[RESULTS_CHAR_MSG_SIZE] ;      /* To communicate performances */
  char recv_results[RESULTS_CHAR_MSG_SIZE] ; /* To recv perf, if p0 finishes */
  int nb_stocked_messages ;
#if defined PRINT_COSTS
  char * tmp_filename=NULL ;
#endif /* PRINT_COSTS */

  for( i=0 ; i<RESULTS_CHAR_MSG_SIZE ; i++ )
    results[i] = '\0' ;
#endif /* MPI */

  Parse_Cmd_Line(argc, argv, p_ad);

#if defined MPI
  /***************************** Init MPI ************************************/
  MPI_Init(&argc, &argv) ;
  MPI_Comm_rank(MPI_COMM_WORLD, &my_num);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

  /* Check every #count for messages (ending, information exchange) */
  assert( count_to_communication > 0 ) ;
  /* Init communication structures */
  list_allocated_msgs.next = NULL ;
  list_allocated_msgs.previous = NULL ;
  list_sent_msgs.next = NULL ;
  list_sent_msgs.previous = NULL ;
  list_recv_msgs.next = NULL ;
  list_recv_msgs.previous = NULL ;
#if defined YC_DEBUG_QUEUE
  list_sent_msgs.text = (char*)malloc(20*sizeof(char*)) ;
  snprintf(list_sent_msgs.text,20,"Sent msgs") ;
  list_sent_msgs.size = 0 ;
  list_sent_msgs.nb_max_msgs_used = 0 ;
  list_recv_msgs.text = (char*)malloc(20*sizeof(char*)) ;
  snprintf(list_recv_msgs.text,20,"Recv msgs") ;
  list_recv_msgs.size = 0 ;
  list_recv_msgs.nb_max_msgs_used = 0 ;
  list_allocated_msgs.text = (char*)malloc(20*sizeof(char*)) ;
  snprintf(list_allocated_msgs.text,20,"Allocated msgs") ;
#endif /* YC_DEBUG_QUEUE */

  /**************************** Initialize seed phase 1 **********************/
  /* Before MPI_init() to get the same seed for all procs */
  if (p_ad->seed < 0) {
    /* Take the number of nano */
    gettimeofday(&tv, NULL);
    /* print_seed = 
       p_ad->seed * ( my_num + 1 ) * ( tv.tv_usec * tv1.tv_usec ) ; */
    p_ad->seed = tv.tv_usec ;
    print_seed = p_ad->seed ;
  } else 
    /* print_seed = Randomize_Seed(p_ad->seed*(my_num+1)); */
    print_seed = p_ad->seed ;

  gettimeofday(&tv, NULL);
  DPRINTF("%ld.%ld: %d ; Print_seed par user %ld\n",
	  tv.tv_sec, tv.tv_usec, my_num, print_seed)

  PRINT0("Program: %s\n", argv[0])
  PRINT0("Number of procs used: %d\n",mpi_size)
  /* Print compilation options */
  PRINT0("Compilation options:\n")
#if defined MPI
  PRINT0("- MPI (So forced count to 1 (-b 1)!\n")
  count = 1 ;
#endif
#if defined DEBUG_MPI_ENDING
  PRINT0("- DEBUG_MPI_ENDING\n")
#endif
#if defined LOG_FILE
  PRINT0("- LOG_FILE\n")
#endif
#if defined NO_SCREEN_OUTPUT
  PRINT0("- NO_SCREEN_OUTPUT\n")
#endif
#if defined DISPLAY_0
  PRINT0("- DISPLAY_0\n")
#endif
#if defined DISPLAY_ALL
  PRINT0("- DISPLAY_ALL\n")
#endif
#if defined DEBUG
  PRINT0("- DEBUG\n")
#endif
#if defined YC_DEBUG_QUEUE
  PRINT0("- YC_DEBUG_QUEUE\n")
#endif
#if defined YC_DEBUG_PRINT_QUEUE
  PRINT0("- YC_DEBUG_PRINT_QUEUE\n")
#endif
#if defined PRINT_COSTS
  PRINT0("- PRINT_COSTS\n")
#endif
  /* Heuristic for communications */
#if defined COMM_COST
  PRINT0("With COMM_COST\n")
#elif defined ITER_COST
  PRINT0("With ITER_COST\n")
#elif defined COMM_SOL
  PRINT0("With COMM_SOL\n")
#else
  PRINT0("Without comm exept for terminaison\n")
#endif

  i=mpi_size ;
  nb_digits_nbprocs=0 ;
  do {
    i=i/10 ;
    nb_digits_nbprocs++ ;
  } while( i!=0 ) ;

#if defined PRINT_COSTS
  if( filename_pattern_print_cost==NULL ) {
    PRINT0("Please give a pattern in which to save costs\n\n")
    exit(-1) ;
  }
  tmp_filename=(char*)
    malloc(sizeof(char)*strlen(filename_pattern_print_cost) 
	   + 2
	   + nb_digits_nbprocs ) ;
  if( nb_digits_nbprocs > 3 ) {
    PRINT0("You use a number of procs sup to 999. "
	   "You must modify the code to adjust next line\n")
    exit(-1) ;
  }
  /* TODO: How can we bypass the static char to format output in nxt line? */
  sprintf(tmp_filename,"%s_p%03d", filename_pattern_print_cost,my_num) ;
  file_descriptor_print_cost = open(tmp_filename,
				    O_WRONLY | O_EXCL | O_CREAT,
				    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) ;
  if( file_descriptor_print_cost == -1 ) {
    DPRINTF("Cannot create file to print costs: file already exists?\n")
    return -1 ;
  }
#endif /* PRINT_COSTS */

  /**************************** Initialize seed phase 2 **********************/
  seed_chaos = ((double)(print_seed * (my_num+1)) / LONG_MAX) ;
  for( i=0 ; i<300 ; i++ )
    seed_chaos=alea_chaos(seed_chaos , 0.4) ;
  Randomize_Seed( (unsigned) (seed_chaos*LONG_MAX) ) ;
  gettimeofday(&tv, NULL);
  printf("%ld.%ld: %d -> Computed seed %u (LONG_MAX=%ld)\n",
	 tv.tv_sec, tv.tv_usec, my_num, (unsigned) (seed_chaos*LONG_MAX),
	 LONG_MAX) ;
  /**************************** Init messages *******************************/
  if( mpi_size == 2 )
    nb_stocked_messages = 20 ;
  else
    nb_stocked_messages = 4*(mpi_size)*(mpi_size) ;
  for( i=0 ; i<nb_stocked_messages ; i++ )
    push_tegami_on( (tegami*)malloc(sizeof(tegami)),
		    &list_allocated_msgs) ;
  PRINT0("Prepared %d messages!\n", nb_stocked_messages)
  /*************************** Launch async recv: will act as mailbox */
  the_message = get_tegami_from( &list_allocated_msgs) ;

#if defined YC_DEBUG_MPI
  gettimeofday(&tv, NULL);
  DPRINTF("%ld.%ld: %d launches MPI_Irecv(), any source\n",
	  tv.tv_sec, tv.tv_usec, my_num)
#endif /* YC_DEBUG_MPI */
  MPI_Irecv(&(the_message->message), SIZE_MESSAGE, MPI_INT,
	    MPI_ANY_SOURCE, 
	    MPI_ANY_TAG,
	    MPI_COMM_WORLD, &(the_message->handle)) ;
    
#else /* MPI */
  printf("Program: %s\n", argv[0]) ;
  if (p_ad->seed < 0)
    p_ad->seed = Randomize();
  else
    Randomize_Seed(p_ad->seed);
#endif /* MPI */

  p_ad->nb_var_to_reset = -1;
  p_ad->do_not_init = 0;
  p_ad->actual_value = NULL;
  p_ad->base_value = 0;
  p_ad->break_nl = 0;
  /* defaults */

  Init_Parameters(p_ad);

  if (p_ad->reset_limit >= p_ad->size)
    p_ad->reset_limit = p_ad->size - 1;

  setvbuf(stdout, NULL, _IOLBF, 0);
  //setlinebuf(stdout);

  if (p_ad->debug > 0 && !ad_has_debug)
    DPRINTF("Warning ad_solver is not compiled with debugging support\n")

  if (p_ad->log_file && !ad_has_log_file)
    DPRINTF("Warning ad_solver is not compiled with log file support\n")

  p_ad->size_in_bytes = p_ad->size * sizeof(int);
  p_ad->sol = malloc(p_ad->size_in_bytes);

  if (p_ad->nb_var_to_reset == -1)
    {
      p_ad->nb_var_to_reset = Div_Round_Up(p_ad->size * p_ad->reset_percent, 100);
      if (p_ad->nb_var_to_reset < 2)
	{
	  p_ad->nb_var_to_reset = 2;
	  printf("increasing nb var to reset since too small, now = %d\n", p_ad->nb_var_to_reset);
	}
    }

  /************ Print configuration information + specific initialization *****/  
  PRINT0("current random seed used: %u ->%u /- %u \n", (unsigned int)p_ad->seed, (unsigned int)print_seed, (unsigned)(seed_chaos*LONG_MAX))
  PRINT0("variables of loc min are frozen for: %d swaps\n",
	  p_ad->freeze_loc_min)
  PRINT0("variables swapped are frozen for: %d swaps\n", p_ad->freeze_swap)
  if (p_ad->reset_percent >= 0)
    PRINT0("%d %% = ", p_ad->reset_percent)
  PRINT0("%d variables are reset when %d variables are frozen\n", 
	 p_ad->nb_var_to_reset, p_ad->reset_limit)
  PRINT0("probability to select a local min (instead of staying on a plateau): ")
  if (p_ad->prob_select_loc_min >=0 && p_ad->prob_select_loc_min <= 100) {
    PRINT0("%d %%\n", p_ad->prob_select_loc_min)
  } else {
    PRINT0("not used\n")
  }
  PRINT0("abort when %d iterations are reached "
	 "and restart at most %d times\n",
	 p_ad->restart_limit, p_ad->restart_max)

#if defined MPI
  PRINT0("Perform communication tests every %d iterations\n",
	 count_to_communication )
#if (defined COMM_COST)||(defined ITER_COST)
  PRINT0("Prob communication = %d\n", proba_communication)
#endif
  PRINT0("===========================================\n\n")
#endif

  if (count <= 0) /* Note: MPI => count=1 */
    {
      Set_Initial(p_ad);

      AS_set_p_ad_seed(p_ad) ;
      time_one0 = (double) User_Time();
      Solve(p_ad);
      time_one = ((double) User_Time() - time_one0) / 1000;

      if (p_ad->exhaustive)
	DPRINTF("exhaustive search\n")
      
      if (count < 0)
	Display_Solution(p_ad);

      Verify_Sol(p_ad);

      if (p_ad->total_cost)
	DPRINTF("*** NOT SOLVED (cost of this pseudo-solution: %d) ***\n", p_ad->total_cost)

      if (count == 0)
	{
	  nb_same_var_by_iter = (double) p_ad->nb_same_var / p_ad->nb_iter;
	  nb_same_var_by_iter_tot = (double) p_ad->nb_same_var_tot / p_ad->nb_iter_tot;

	  printf("%5d %8.2f %8d %8d %8d %8d %8.1f %8d %8d %8d %8d %8.1f", 
		 p_ad->nb_restart, time_one, 
		 p_ad->nb_iter, p_ad->nb_local_min, p_ad->nb_swap, 
		 p_ad->nb_reset, nb_same_var_by_iter,
		 p_ad->nb_iter_tot, p_ad->nb_local_min_tot, p_ad->nb_swap_tot, 
		 p_ad->nb_reset_tot, nb_same_var_by_iter_tot);
	  if (user_stat_fct)
	    printf(" %8d", (*user_stat_fct)(p_ad));
	  printf("\n");
	}
      else
	{
	  printf("in %.2f secs (%d restarts, %d iters, %d loc min, %d swaps, %d resets)\n", 
		 time_one, p_ad->nb_restart, p_ad->nb_iter_tot, p_ad->nb_local_min_tot, 
		 p_ad->nb_swap_tot, p_ad->nb_reset_tot);
	}
      return 0 ;
    } /* (count <= 0) */

  PRINT0("\n")

  if (user_stat_name)
    sprintf(str, " %8s |", user_stat_name);
  else
    *str = '\0';

  sprintf(buff, "|Count|restart|     time |    iters |  loc min |    swaps "
	  "|   resets | same/iter|%s\n", str);

  if (param_needed > 0)
    PRINT0("%*d\n", (int) strlen(buff)/2, p_ad->param)
  else if (param_needed < 0)
    PRINT0("%*s\n", (int) strlen(buff)/2, p_ad->param_file)
    
  
  PRINT0("%s", buff)
  for(i = 0; buff[i] != '\n'; i++)
    if (buff[i] != '|')
      buff[i] = '-';
  PRINT0("%s", buff)
  PRINT0("\n\n")

  
  nb_restart_cum = time_cum = user_stat_cum = 0;

  nb_iter_cum = nb_local_min_cum = nb_swap_cum = nb_reset_cum = 0;
  nb_same_var_by_iter_cum = user_stat_cum = 0;


  nb_iter_tot_cum = nb_local_min_tot_cum = nb_swap_tot_cum = nb_reset_tot_cum = 0;
  nb_same_var_by_iter_tot_cum = 0;

  nb_restart_min = user_stat_min = (1 << 30);
  time_min = 1e100;
  
  nb_iter_tot_min = nb_local_min_tot_min = nb_swap_tot_min = nb_reset_tot_min = (1 << 30);
  nb_same_var_by_iter_tot_min = 1e100;

  nb_restart_max = user_stat_max = 0;
  time_max = 0;
 
  nb_iter_tot_max = nb_local_min_tot_max = nb_swap_tot_max = nb_reset_tot_max = 0;
  nb_same_var_by_iter_tot_max = 0;


  for(i = 1; i <= count; i++)
    {
      Set_Initial(p_ad);

      AS_set_p_ad_seed(p_ad) ;
      time_one0 = (double) User_Time();
      //if (i == 57) printf("\n\n\nseed ================ %d\n", p_ad->seed), xxx=1;
      Solve(p_ad);
      time_one = ((double) User_Time() - time_one0) / 1000;

      if (disp_mode == 2 && nb_restart_cum > 0)
	printf("\033[A\033[K");
      printf("\033[A\033[K\033[A\033[256D");


      Verify_Sol(p_ad);

      if (user_stat_fct)
	user_stat = (*user_stat_fct)(p_ad);


      nb_same_var_by_iter = (double) p_ad->nb_same_var / p_ad->nb_iter;
      nb_same_var_by_iter_tot = (double) p_ad->nb_same_var_tot / p_ad->nb_iter_tot;

      nb_restart_cum += p_ad->nb_restart;
      time_cum += time_one;
      nb_iter_cum += p_ad->nb_iter;
      nb_local_min_cum += p_ad->nb_local_min;
      nb_swap_cum += p_ad->nb_swap;
      nb_reset_cum += p_ad->nb_reset;
      nb_same_var_by_iter_cum += nb_same_var_by_iter;
      user_stat_cum += user_stat;

      nb_iter_tot_cum += p_ad->nb_iter_tot;
      nb_local_min_tot_cum += p_ad->nb_local_min_tot;
      nb_swap_tot_cum += p_ad->nb_swap_tot;
      nb_reset_tot_cum += p_ad->nb_reset_tot;
      nb_same_var_by_iter_tot_cum += nb_same_var_by_iter_tot;

      if (nb_restart_min > p_ad->nb_restart)
	nb_restart_min = p_ad->nb_restart;
      if (time_min > time_one)
	time_min = time_one;
      if (nb_iter_tot_min > p_ad->nb_iter_tot)
	nb_iter_tot_min = p_ad->nb_iter_tot;
      if (nb_local_min_tot_min > p_ad->nb_local_min_tot)
	nb_local_min_tot_min = p_ad->nb_local_min_tot;
      if (nb_swap_tot_min > p_ad->nb_swap_tot)
	nb_swap_tot_min = p_ad->nb_swap_tot;
      if (nb_reset_tot_min > p_ad->nb_reset_tot)
	nb_reset_tot_min = p_ad->nb_reset_tot;
      if (nb_same_var_by_iter_tot_min > nb_same_var_by_iter_tot)
	nb_same_var_by_iter_tot_min = nb_same_var_by_iter_tot;
      if (user_stat_min > user_stat)
	user_stat_min = user_stat;

      if (nb_restart_max < p_ad->nb_restart)
	nb_restart_max = p_ad->nb_restart;
      if (time_max < time_one)
	time_max = time_one;
      if (nb_iter_tot_max < p_ad->nb_iter_tot)
	nb_iter_tot_max = p_ad->nb_iter_tot;
      if (nb_local_min_tot_max < p_ad->nb_local_min_tot)
	nb_local_min_tot_max = p_ad->nb_local_min_tot;
      if (nb_swap_tot_max < p_ad->nb_swap_tot)
	nb_swap_tot_max = p_ad->nb_swap_tot;
      if (nb_reset_tot_max < p_ad->nb_reset_tot)
	nb_reset_tot_max = p_ad->nb_reset_tot;
      if (nb_same_var_by_iter_tot_max < nb_same_var_by_iter_tot)
	nb_same_var_by_iter_tot_max = nb_same_var_by_iter_tot;
      if (user_stat_max < user_stat)
	user_stat_max = user_stat;

#ifndef MPI
      switch(disp_mode)
	{
	case 0:			/* only last iter counters */
	case 2:			/* last iter followed by restart if needed */
	  printf("|%4d | %5d%c| %8.2f | %8d | %8d | %8d | %8d | %8.1f |",
		 i, p_ad->nb_restart, (p_ad->total_cost == 0) ? ' ' : 'N',
		 time_one,
		 p_ad->nb_iter, p_ad->nb_local_min, p_ad->nb_swap,
		 p_ad->nb_reset, nb_same_var_by_iter);
	  if (user_stat_fct)
	    printf(" %8d |", user_stat);
	  printf("\n");

	  if (disp_mode == 2 && p_ad->nb_restart > 0) 
	    {
	      printf("|     |       |          |"
		     " %8d | %8d | %8d | %8d | %8.1f |",
		     p_ad->nb_iter_tot, p_ad->nb_local_min_tot,
		     p_ad->nb_swap_tot,
		     p_ad->nb_reset_tot, nb_same_var_by_iter_tot);
	      if (user_stat_fct)
		printf("          |");
	      printf("\n");
	    }

	  printf("%s", buff);

	  printf("| avg | %5d | %8.2f | %8d | %8d | %8d | %8d | %8.1f |",
		 nb_restart_cum / i, time_cum / i,
		 nb_iter_cum / i, nb_local_min_cum / i, nb_swap_cum / i,
		 nb_reset_cum / i, nb_same_var_by_iter_cum / i);
	  if (user_stat_fct)
	    printf(" %8.2f |", (double) user_stat_cum / i);
	  printf("\n");


	  if (disp_mode == 2 && nb_restart_cum > 0) 
	    {
	      printf("|     |       |          |"
		     " %8d | %8d | %8d | %8d | %8.1f |",
		     nb_iter_tot_cum / i, nb_local_min_tot_cum / i,
		     nb_swap_tot_cum / i,
		     nb_reset_tot_cum / i, nb_same_var_by_iter_tot_cum / i);
	      if (user_stat_fct)
		printf("          |");
	      printf("\n");
	    }
	  break;

	case 1:			/* only total (restart + last iter) counters */
	  printf("|%4d | %5d%c| %8.2f | %8d | %8d | %8d | %8d | %8.1f |",
		 i, p_ad->nb_restart, (p_ad->total_cost == 0) ? ' ' : 'N',
		 time_one,
		 p_ad->nb_iter_tot, p_ad->nb_local_min_tot, p_ad->nb_swap_tot,
		 p_ad->nb_reset_tot, nb_same_var_by_iter_tot);
	  if (user_stat_fct)
	    printf(" %8d |", user_stat);
	  printf("\n");

	  printf("%s", buff);

	  printf("| avg | %5d | %8.2f | %8d | %8d | %8d | %8d | %8.1f |",
		 nb_restart_cum / i, time_cum / i,
		 nb_iter_tot_cum / i, nb_local_min_tot_cum / i,
		 nb_swap_tot_cum / i,
		 nb_reset_tot_cum / i, nb_same_var_by_iter_tot_cum / i);
	  if (user_stat_fct)
	    printf(" %8.2f |", (double) user_stat_cum / i);
	  printf("\n");
	  break;
	}
#else /* MPI */
      /* disp_mode equals 1 by default */
      /* Prepare what will be sent to 0, or maybe printed by 0 */
      snprintf(results, RESULTS_CHAR_MSG_SIZE - 1,
	       "|* %ld/(%d/%d) | %5d | %8.2f | %8d | %8d | %8d | %8d | %8.1f |",
	       print_seed,
	       my_num,
	       mpi_size,
	       nb_restart_cum / i, time_cum / i,
	       nb_iter_tot_cum / i, nb_local_min_tot_cum / i,
	       nb_swap_tot_cum / i,
	       nb_reset_tot_cum / i, nb_same_var_by_iter_tot_cum / i);
      /* TODO: use if(user_stat_fct)? What is that? */
#endif /* MPI */
    } /* for(i = 1; i <= count; i++) */

  if (count <= 0) /* YC->Daniel: is it really possible here? return 0 before.*/
    return 0;

  if( count > 1 ) { /* YC->Daniel: why this test has been removed? */
    printf("| min | %5d | %8.2f | %8d | %8d | %8d | %8d | %8.1f |",
	   nb_restart_min, time_min,
	   nb_iter_tot_min, nb_local_min_tot_min, nb_swap_tot_min,
	   nb_reset_tot_min, nb_same_var_by_iter_tot_min);
    if (user_stat_fct)
      printf(" %8d |", user_stat_min);
    printf("\n");

    printf("| max | %5d | %8.2f | %8d | %8d | %8d | %8d | %8.1f |",
	   nb_restart_max, time_max,
	   nb_iter_tot_max, nb_local_min_tot_max, nb_swap_tot_max,
	   nb_reset_tot_max, nb_same_var_by_iter_tot_max);
    if (user_stat_fct)
      printf(" %8d |", user_stat_max);
    printf("\n");
  }

#if defined MPI
  /* From now, time is not crucial anymore... */
  /* Perform a broadcast to kill everyone since I have the solution */
  
#if defined DEBUG_MPI_ENDING
  DPRINTF("Proc %d enters TERM MASTER finishing!\n", my_num)
#endif

  if( my_num != 0 ) { 
    /*********************************** Proc N ! ****************************/
    /****** Use *Log(n) + 1*  algo */
    /*** Send LS_KILLALL to 0 -> no real message => no need to init */
    /* But proc 0 can also be in that step, so Isend() mandatory! */
    tmp_message = get_tegami_from( &list_allocated_msgs) ;
    /*    tmp_message->message[1] = my_num ; */
#ifdef DEBUG_MPI_ENDING
    gettimeofday(&tv, NULL);
    DPRINTF("%ld.%ld: %d launches MPI_Isend(), LS_KILLALL to 0\n",
	    tv.tv_sec, tv.tv_usec, my_num)
#endif
    MPI_Isend(tmp_message->message, SIZE_MESSAGE, MPI_INT,
	      0,
	      LS_KILLALL, MPI_COMM_WORLD,
	      &(tmp_message->handle)) ;
    push_tegami_on( tmp_message, &list_sent_msgs) ;
    /* Loop on all received msg. Drop all except LS_KILLALL */
#ifdef DEBUG_MPI_ENDING
    gettimeofday(&tv, NULL);
    DPRINTF("%ld.%ld: %d loops on recvd msgs\n",
	    tv.tv_sec, tv.tv_usec, my_num)
#endif
    do {
#ifdef DEBUG_MPI_ENDING
	gettimeofday(&tv, NULL);
	DPRINTF("  - %ld.%ld: %d launches MPI_Wait()\n",
		tv.tv_sec, tv.tv_usec, my_num)
#endif
      MPI_Wait(&(the_message->handle), &(the_message->status)) ;
#ifdef DEBUG_MPI_ENDING
	gettimeofday(&tv, NULL);
	DPRINTF("  - %ld.%ld: %d recvd value (%d;%d) protocol %s from %d\n",
		tv.tv_sec, tv.tv_usec, my_num,
		the_message->message[0],
		the_message->message[1],
		protocole_name[the_message->status.MPI_TAG],
		the_message->status.MPI_SOURCE)
#endif
      if( the_message->status.MPI_TAG != LS_KILLALL ) {
	push_tegami_on( the_message, &list_allocated_msgs) ;
	the_message = get_tegami_from( &list_allocated_msgs) ;
#ifdef DEBUG_MPI_ENDING
	gettimeofday(&tv, NULL);
	DPRINTF("%ld.%ld: %d launches MPI_Irecv(), ANY_TAG, any source\n",
		tv.tv_sec, tv.tv_usec, my_num)
#endif
	MPI_Irecv(&(the_message->message), SIZE_MESSAGE, MPI_INT,
		  MPI_ANY_SOURCE, 
		  MPI_ANY_TAG,
		  MPI_COMM_WORLD, &(the_message->handle)) ;
      } 
    } while( the_message->status.MPI_TAG != LS_KILLALL ) ;
#ifdef DEBUG_MPI_ENDINF
    DPRINTF("%d received msg from %d that %d finished first\n\n",
	    my_num, the_message->status.MPI_SOURCE, the_message->message[1])
#endif 
    /* Kill sub-range proc */
    send_log_n(the_message->message, LS_KILLALL) ;
    /* Management of results! */
    if( the_message->message[1] == (unsigned)my_num ) { /* I'm the winner */
      /* Send results to 0 */
#ifdef DEBUG_MPI_ENDING
	gettimeofday(&tv, NULL);
	DPRINTF("%ld.%ld: %d launches MPI_Isend(), results to 0\n",
		tv.tv_sec, tv.tv_usec, my_num)
#endif
      MPI_Send( results, RESULTS_CHAR_MSG_SIZE, MPI_CHAR,
		0, SENDING_RESULTS,
		MPI_COMM_WORLD) ;
#ifdef DEBUG_MPI_ENDING
      gettimeofday(&tv, NULL);
      DPRINTF("%ld.%ld: %d calls MPI_Finalize()\n",
	      tv.tv_sec, tv.tv_usec, my_num)
#endif
#ifdef PRINT_COSTS
      print_costs() ;
#endif
      MPI_Finalize() ;
    }
  } else { 
    /*********************************** Proc 0 ! ****************************/
    /* Check if we received a LS_KILLALL before we finished the calculus */
#ifdef DEBUG_MPI_ENDING
    DPRINTF("%d checks if we received LS_KILLALL in last msgs...\n", my_num)
#endif

    do {
#ifdef DEBUG_MPI_ENDING
	gettimeofday(&tv, NULL);
	DPRINTF("%ld.%ld: %d launches MPI_Test()\n",
		tv.tv_sec, tv.tv_usec, my_num)
#endif
      MPI_Test(&(the_message->handle),
	       &flag,
	       &(the_message->status)) ;
      if( flag > 0 ) {                /* We received one! */
#ifdef DEBUG_MPI_ENDING
	gettimeofday(&tv, NULL);
	DPRINTF("%ld:%ld: %d received message %d protocol %s from %d\n",
		tv.tv_sec, tv.tv_usec,
		my_num,
		the_message->message[1],
		protocole_name[the_message->status.MPI_TAG],
		the_message->status.MPI_SOURCE)
#endif
	if( the_message->status.MPI_TAG == LS_KILLALL ) {
	  the_message->message[0] = mpi_size ;
	  the_message->message[1] = the_message->status.MPI_SOURCE ;
	  send_log_n( the_message->message, LS_KILLALL ) ;
	  /* Now, recv result from winner */
#ifdef DEBUG_MPI_ENDING
	  gettimeofday(&tv, NULL);
	  DPRINTF("%ld.%ld: %d launches MPI_Irecv() of results for"
		  " source %d\n",
		  tv.tv_sec, tv.tv_usec, my_num,
		  the_message->status.MPI_SOURCE)
#endif
	  MPI_Recv( recv_results, RESULTS_CHAR_MSG_SIZE, MPI_CHAR,
		    the_message->status.MPI_SOURCE, SENDING_RESULTS,
		    MPI_COMM_WORLD,
		    MPI_STATUS_IGNORE) ;
	  /**** Compare its result to our! */
#ifdef DEBUG_MPI_ENDING
	  DPRINTF("Recvd : %s\n", recv_results)
	  DPRINTF("  -> %s\n",
		  strchr(strchr(recv_results+1,'|')+1, '|')+1)
	  DPRINTF("Computed : %s\n", results)
	  DPRINTF("  ->%s\n",
		  strchr(strchr(results+1,'|')+1, '|')+1)
#endif
	  /* Search for 3rd | in string */
	  if( atof(strchr(strchr(&recv_results[1],'|')+1, '|')+1)
	      > atof(strchr(strchr(&results[1],'|')+1, '|')+1) )
	    printf("%s\n", results ) ;
	  else
	    printf("%s\n", recv_results ) ;

#ifdef PRINT_COSTS
	  print_costs() ;
	  gettimeofday(&tv, NULL);
	  printf("%ld.%ld: %d sleeps for %d secs\n",
		 tv.tv_sec, tv.tv_usec, my_num, mpi_size*3) ;
	  sleep(mpi_size*3) ;
#endif

	  gettimeofday(&tv, NULL);
	  printf("%ld.%ld: %d launches MPI_Abort()\n",
		 tv.tv_sec, tv.tv_usec, my_num) ;
	  MPI_Abort(MPI_COMM_WORLD, my_num) ;

	  dead_end_final() ;
	  exit(0) ;
	} else { /* Prepare test for new msg */
#ifdef DEBUG_MPI_ENDING
	  gettimeofday(&tv, NULL);
	  DPRINTF("%ld.%ld: %d launches MPI_Irecv(), any source\n",
		  tv.tv_sec, tv.tv_usec, my_num)
#endif
	  MPI_Irecv(&(the_message->message), SIZE_MESSAGE, MPI_INT,
		    MPI_ANY_SOURCE, 
		    MPI_ANY_TAG,
		    MPI_COMM_WORLD, &(the_message->handle)) ;
	}
      } 
    } while( flag > 0 ) ; /* exit when no msg recvd */
    /* From here, proc 0 winner */
    printf("%s\n", results) ;

#ifdef PRINT_COSTS
    print_costs() ;
    gettimeofday(&tv, NULL);
    printf("%ld.%ld: %d sleeps for %d secs\n",
	   tv.tv_sec, tv.tv_usec, my_num, mpi_size*3) ;
    sleep(mpi_size*3) ;
#endif

    gettimeofday(&tv, NULL);
    printf("%ld.%ld: %d launches MPI_Abort()\n",
	   tv.tv_sec, tv.tv_usec, my_num) ;
    MPI_Abort(MPI_COMM_WORLD, my_num) ;
    gettimeofday(&tv, NULL);
    printf("%ld.%ld: %d launches MPI_Abort() done\n",
	   tv.tv_sec, tv.tv_usec, my_num) ;

    /* Cancel Irecv */
#ifdef DEBUG_MPI_ENDING
    gettimeofday(&tv, NULL);
    DPRINTF("%ld.%ld: %d launches MPI_Cancel()\n",
	    tv.tv_sec, tv.tv_usec, my_num)
#endif
    MPI_Cancel( &(the_message->handle) ) ;
    MPI_Wait( &(the_message->handle), MPI_STATUS_IGNORE ) ;
#ifdef DEBUG_MPI_ENDING
    gettimeofday(&tv, NULL);
    DPRINTF("%ld.%ld: %d finished Waiting of canceled msg\n",
	    tv.tv_sec, tv.tv_usec, my_num)
#endif
    /* Reuse buffer */
    the_message->message[1] = 0 ;
    the_message->message[0] = mpi_size ;
    send_log_n( the_message->message, LS_KILLALL ) ;
    printf("%s\n", results) ;
#ifdef DEBUG_MPI_ENDING
    gettimeofday(&tv, NULL);
    DPRINTF("%ld.%ld: %d aborting\n",
	    tv.tv_sec, tv.tv_usec, my_num)
#endif  
  } /* Proc N // Proc 0 */
  
  dead_end_final() ;
#endif /* MPI */

  return 0;
}



void
Set_Initial(AdData *p_ad)
{
  int i;
  switch (read_initial)
    {
    case 0:
      break;

    case 1:
      printf("enter the initial configuration:\n");
      for(i = 0; i < p_ad->size; i++)
	if (scanf("%d", &p_ad->sol[i])) /* avoid gcc warning warn_unused_result */
	  {}
      getchar();		/* the last \n */
      Display_Solution(p_ad);
      i = Random_Permut_Check(p_ad->sol, p_ad->size, p_ad->actual_value, p_ad->base_value);
      if (i >= 0)
	{
	  fprintf(stderr, "not a valid permutation, error at [%d] = %d\n",
		 i, p_ad->sol[i]);
	  Random_Permut_Repair(p_ad->sol, p_ad->size, p_ad->actual_value, p_ad->base_value);
	  printf("possible repair:\n");
	  Display_Solution(p_ad);
	  exit(1);
	}
      p_ad->do_not_init = 1;
      break;

    case 2:
      Random_Permut(p_ad->sol, p_ad->size, p_ad->actual_value, p_ad->base_value);
      p_ad->do_not_init = 1;
      break;
    }

#if defined(DEBUG) && (DEBUG & 1)
  if (p_ad->do_not_init)
    {
      printf("++++++++++ values to pass to threads (do_not_init=1)\n");
      Display_Solution(p_ad);
      printf("+++++++++++++++++++++++++++\n");
    }
#endif
}




static void
Verify_Sol(AdData *p_ad)
{
  if (p_ad->total_cost != 0 || !check_valid)
    return;

  int i = Random_Permut_Check(p_ad->sol, p_ad->size, p_ad->actual_value, p_ad->base_value);
  if (i >= 0)
    {
      fprintf(stderr, "*** Erroneous Solution !!! not a valid permutation, error at [%d] = %d\n", i, p_ad->sol[i]);
    }
  else
    if (!Check_Solution(p_ad))
      printf("*** Erroneous Solution !!!\n");
}




#define L(msg) fprintf(stderr, msg "\n")


/*
 *  PARSE_CMD_LINE
 *
 */
static void
Parse_Cmd_Line(int argc, char *argv[], AdData *p_ad)
{
  int param_read = 0;
  int i;

  nb_threads = 1;

  count = -1;
  disp_mode = 1;
  check_valid = 0;
  read_initial = 0;

  p_ad->param = -1;
  p_ad->seed = -1;
  p_ad->debug = 0;
  p_ad->log_file = NULL;
  p_ad->prob_select_loc_min = -1;
  p_ad->freeze_loc_min = -1;
  p_ad->freeze_swap = -1;
  p_ad->reset_limit = -1;
  p_ad->reset_percent = -1;
  p_ad->restart_limit = -1;
  p_ad->restart_max = -1;
  p_ad->exhaustive = 0;
  p_ad->first_best = 0;
#if defined MPI
  count_to_communication = -1 ;
#if (defined COMM_COST)||(defined ITER_COST)
  proba_communication = 0 ;
#endif
#ifdef PRINT_COSTS
  filename_pattern_print_cost=NULL ;
#endif
#endif /* MPI */

  for(i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-')
	{
	  switch(argv[i][1])
	    {
	    case 'i':
	      read_initial = 1;
	      continue;

	    case 'D':
	      if (++i >= argc)
		{
		  L("debug level expected");
		  exit(1);
		}
	      p_ad->debug = atoi(argv[i]);
	      continue;

	    case 's':
	      if (++i >= argc)
		{
		  L("random seed expected");
		  exit(1);
		}
	      p_ad->seed = atoi(argv[i]);
	      continue;

	    case 'L':
	      if (++i >= argc)
		{
		  L("log file name expected");
		  exit(1);
		}
	      p_ad->log_file = argv[i];
	      continue;

	    case 'c':
	      check_valid = 1;
	      continue;

	    case 'e':
	      p_ad->exhaustive = 1;
	      continue;

	    case 'b':
	      if (++i >= argc)
		{
		  L("count expected");
		  exit(1);
		}
	      count = atoi(argv[i]);
	      continue;

	    case 'd':
	      if (++i >= argc)
		{
		  L("display mode expected");
		  exit(1);
		}
	      disp_mode = atoi(argv[i]);
	      continue;

	    case 'P':
	      if (++i >= argc)
		{
		  L("probability (in %%) expected");
		  exit(1);
		}
	      p_ad->prob_select_loc_min = atoi(argv[i]);
	      continue;

	    case 'f':
	      if (++i >= argc)
		{
		  L("freeze number expected");
		  exit(1);
		}
	      p_ad->freeze_loc_min = atoi(argv[i]);
	      continue;

	    case 'F':
	      if (++i >= argc)
		{
		  L("freeze number expected");
		  exit(1);
		}
	      p_ad->freeze_swap = atoi(argv[i]);
	      continue;

	    case 'l':
	      if (++i >= argc)
		{
		  L("reset limit expected");
		  exit(1);
		}
	      p_ad->reset_limit = atoi(argv[i]);
	      continue;

	    case 'p':
	      if (++i >= argc)
		{
		  L("reset percent expected");
		  exit(1);
		}
	      p_ad->reset_percent = atoi(argv[i]);
	      continue;

	    case 'a':
	      if (++i >= argc)
		{
		  L("restart limit expected");
		  exit(1);
		}
	      p_ad->restart_limit = atoi(argv[i]);
	      continue;

	    case 'r':
	      if (++i >= argc)
		{
		  L("restart number expected");
		  exit(1);
		}
	      p_ad->restart_max = atoi(argv[i]);
	      continue;

#ifdef CELL
	    case 't':
	      if (++i >= argc)
		{
		  L("number of threads expected");
		  exit(1);
		}
	      nb_threads = atoi(argv[i]);
	      continue;

	    case 'I':
	      read_initial = 2;
	      continue;
#endif
#if defined MPI
	    case 'C':
	      if (++i >= argc)
		{
		  L("#iterations before check for communication");
		  exit(1);
		}
	      count_to_communication = atoi(argv[i]);
	      continue;
#if (defined COMM_COST) || (defined ITER_COST)
	    case 'z':
	      if (++i >= argc)
		{
		  L("> #rand()*100 -> sends cost");
		  exit(1);
		}
	      proba_communication = atoi(argv[i]);
	      continue;
#endif
#ifdef PRINT_COSTS
	    case 'y':
	      if (++i >= argc)
		{
		  L("#filename pattern in which to save costs");
		  exit(1);
		}
	      filename_pattern_print_cost = argv[i];
	      continue;
#endif /* PRINT_COSTS */
#endif /* MPI */
	    case 'h':
	      fprintf(stderr, "Usage: %s [ OPTION ]", argv[0]);
	      if (param_needed > 0)
		fprintf(stderr, " PARAM");
	      else if (param_needed < 0)
		fprintf(stderr, " FILE");

	      L("");
	      L("   -i          read initial configuration");
	      L("   -D LEVEL    set debug mode (0=debug info, 1=step-by-step)");
	      L("   -L FILE     use file as log file");
	      L("   -c          check if the solution is valid");
	      L("   -s SEED     specify random seed");
	      L("   -b COUNT    bench COUNT times");
	      L("   -d WHAT     set display info (needs -b), WHAT is:");
              L("                 0=only last iter counters, 1=sum of restart+last iter counters (default)");
	      L("                 2=restart and last iter counters");
	      L("   -P PERCENT  probability to select a local min (instead of staying on a plateau)");
	      L("   -f NB       freeze variables of local min for NB swaps");
	      L("   -F NB       freeze variables swapped for NB swaps");
	      L("   -l LIMIT    reset some variables when LIMIT variable are frozen");
	      L("   -p PERCENT  reset PERCENT %% of variables");
	      L("   -a NB       abort and restart when NB iterations are reached");
	      L("   -r COUNT    restart at most COUNT times");
	      L("   -e          exhaustive seach (do all combinations)");
	      L("   -h          show this help");
#ifdef CELL
	      L("");
	      L("Cell specific options:");
	      L("   -t NB       launch NB threads");
	      L("   -I          set the same initial configuration to all threads");
#endif
#ifdef MPI
	      L("");
	      L("MPI specific options:");
	      L("   -C NB       check comm and send comm every NB iterations");
#if (defined COMM_COST) || (defined ITER_COST)
	      L("   -z NB       rand()*100 < NB -> sends cost");
#endif /* (defined COMM_COST) || (defined ITER_COST) */
#ifdef PRINT_COSTS
	      L("   -y pattern  pattern to create filename to save cost per proc");
#endif /* PRINT_COSTS */
#endif
	      exit(0);

	    default:
	      fprintf(stderr, "unrecognized option %s (-h for a help)\n", argv[i]);
	      exit(1);
	    }
	}

      if (param_needed > 0 && !param_read)
	{
	  p_ad->param = atoi(argv[i]);
	  param_read = 1;
	}
      else if (param_needed < 0 && !param_read)
	{
	  strcpy(p_ad->param_file, argv[i]);
	  param_read = 1;
	}
      else
	{
	  fprintf(stderr, "unrecognized argument %s (-h for a help)\n", argv[i]);
	  exit(1);
	}
    }

  if (param_needed > 0 && !param_read)
    {
      printf("param: ");
      No_Gcc_Warn_Unused_Result(scanf("%d", &p_ad->param));
      getchar();		/* get the last \n */
    } 
  else if (param_needed < 0 && !param_read)
    {
      printf("file: ");
      No_Gcc_Warn_Unused_Result(fgets(p_ad->param_file, sizeof(p_ad->param_file), stdin));
      int l = strlen(p_ad->param_file);
      if (--l >= 0 && p_ad->param_file[l] == '\n')
	p_ad->param_file[l] = '\0';
    }
}

void AS_set_p_ad_seed(AdData *p_ad)
{
  PRINT0("TODO: Check here is we have to modify random search!\n") ;
  p_ad->seed = Random(65536);
}
