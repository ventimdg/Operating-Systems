/**
 * The MapReduce coordinator.
 */

#ifndef H1_H__
#define H1_H__
#include "../rpc/rpc.h"
#include "../lib/lib.h"
#include "../app/app.h"
#include "job.h"
#include <glib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>


typedef struct {
  /* TODO */
  int job_id;
  GList* queue_order;
  GHashTable* job_data; 
  GList* assigned_tasks;
} coordinator;

typedef struct {
  int job_id;
  char** input_files;
  int num_input_files;
  char* output_dir;
  char *app_name;
  app cur_app;
  int n_reduce;
  char* args;
  int next_input_job;
  int num_input_completed;
  int next_reduce_job;
  int num_reduce_completed;
  bool done;
  bool failed;
} job_request;

typedef struct {
  time_t assigned; 
	int job_id;
	int task;
	path file;
	path output_dir;
	char *app;
	int n_reduce;
	int n_map;
	bool_t reduce;
	bool_t wait;
	struct {
		u_int args_len;
		char *args_val;
	}args;
} assigned_job;

void coordinator_init(coordinator** coord_ptr);
#endif
