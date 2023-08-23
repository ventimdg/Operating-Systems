/**
 * The MapReduce coordinator.
 */

#include "coordinator.h"

#ifndef SIG_PF
#define SIG_PF void (*)(int)
#endif

/* Global coordinator state. */
coordinator* state;

extern void coordinator_1(struct svc_req*, struct SVCXPRT*);

/* Set up and run RPC server. */
int main(int argc, char** argv) {
  register SVCXPRT* transp;

  pmap_unset(COORDINATOR, COORDINATOR_V1);

  transp = svcudp_create(RPC_ANYSOCK);
  if (transp == NULL) {
    fprintf(stderr, "%s", "cannot create udp service.");
    exit(1);
  }
  if (!svc_register(transp, COORDINATOR, COORDINATOR_V1, coordinator_1, IPPROTO_UDP)) {
    fprintf(stderr, "%s", "unable to register (COORDINATOR, COORDINATOR_V1, udp).");
    exit(1);
  }

  transp = svctcp_create(RPC_ANYSOCK, 0, 0);
  if (transp == NULL) {
    fprintf(stderr, "%s", "cannot create tcp service.");
    exit(1);
  }
  if (!svc_register(transp, COORDINATOR, COORDINATOR_V1, coordinator_1, IPPROTO_TCP)) {
    fprintf(stderr, "%s", "unable to register (COORDINATOR, COORDINATOR_V1, tcp).");
    exit(1);
  }

  coordinator_init(&state);

  svc_run();
  fprintf(stderr, "%s", "svc_run returned");
  exit(1);
  /* NOTREACHED */
}

/* EXAMPLE RPC implementation. */
int* example_1_svc(int* argp, struct svc_req* rqstp) {
  static int result;

  result = *argp + 1;

  return &result;
}

/* SUBMIT_JOB RPC implementation. */
int* submit_job_1_svc(submit_job_request* argp, struct svc_req* rqstp) {
  static int result;

  // printf("Received submit job request\n");
  /* TODO */


  app exists = get_app(argp->app);
  int job_id;


  if (exists.name == NULL){

    job_id = -1;

  } else {

    job_id = state->job_id;

    state->job_id++;

    job_request* cur_job = (job_request*) malloc(sizeof(job_request));

    cur_job->job_id = job_id;

    cur_job->input_files = (char**) malloc(sizeof(char*) * argp->files.files_len);

    for (int i = 0; i < argp->files.files_len; i++){
      cur_job->input_files[i] = strdup(argp->files.files_val[i]);
    }

    cur_job->num_input_files = argp->files.files_len;

    cur_job->output_dir = strdup(argp->output_dir);

    cur_job->app_name = strdup(argp->app);

    cur_job->cur_app = exists;

    cur_job->n_reduce = argp->n_reduce;

    cur_job->args = (char*) malloc(sizeof(char) * argp->args.args_len + 1);
    cur_job->args[argp->args.args_len] = '\0';
    memcpy(cur_job->args, argp->args.args_val, argp->args.args_len);

    cur_job->next_input_job = 0;

    cur_job->next_reduce_job = 0;

    cur_job->done = false;

    cur_job->failed = false;

    cur_job->num_input_completed = 0;

    cur_job->num_reduce_completed = 0;

    state->queue_order = g_list_append(state->queue_order, GINT_TO_POINTER(job_id));

    g_hash_table_insert(state->job_data, GINT_TO_POINTER(job_id), cur_job);
    
  }

  result = job_id;


  /* Do not modify the following code. */
  /* BEGIN */
  struct stat st;
  if (stat(argp->output_dir, &st) == -1) {
    mkdirp(argp->output_dir);
  }

  return &result;
  /* END */
}

/* POLL_JOB RPC implementation. */
poll_job_reply* poll_job_1_svc(int* argp, struct svc_req* rqstp) {
  static poll_job_reply result;

  // printf("Received poll job request\n");

  job_request* lookup_val = g_hash_table_lookup(state->job_data, GINT_TO_POINTER(*argp));

  
  if (lookup_val == NULL){
    result.done = true;
    result.failed = false;
    result.invalid_job_id = true;
  } else {
    result.done = lookup_val->done;
    result.failed = lookup_val->failed;
    result.invalid_job_id = false;
  }

  /* TODO */

  return &result;
}

/* GET_TASK RPC implementation. */
get_task_reply* get_task_1_svc(void* argp, struct svc_req* rqstp) {
  static get_task_reply result;

  // printf("Received get task request\n");
  result.file = "";
  result.output_dir = "";
  result.app = "";
  result.wait = true;
  result.args.args_len = 0;

  /* TODO */

  for (GList* elem = state->assigned_tasks; elem; elem = elem->next) {
     assigned_job* new_job = (assigned_job*) elem->data;
     if (time(NULL) - new_job->assigned > TASK_TIMEOUT_SECS){

      result.job_id = new_job->job_id;

      result.task = new_job->task;

      result.file = strdup(new_job->file );

      result.output_dir = strdup(new_job->output_dir);
      
      result.app = strdup(new_job->app);

      result.n_reduce =  new_job->n_reduce;

      result.n_map = new_job->n_map;

      result.reduce = new_job->reduce;

      result.wait = new_job->wait;

      result.args.args_val = strdup(new_job->args.args_val);

      result.args.args_len = new_job->args.args_len;

      new_job->assigned = time(NULL);

      return &result;

     }
  }

  for (GList* elem = state->queue_order; elem; elem = elem->next) {
    int value = GPOINTER_TO_INT(elem->data);
    job_request* cur_job = g_hash_table_lookup(state->job_data, GINT_TO_POINTER(value));
    assigned_job* new_job = (assigned_job*) malloc(sizeof(assigned_job));

    if (cur_job!= NULL){
      if (!cur_job->done && !cur_job->failed){
        if (cur_job->next_input_job < cur_job->num_input_files){
          result.job_id = cur_job->job_id;
          new_job->job_id = cur_job->job_id;

          result.task = cur_job->next_input_job;
          new_job->task = cur_job->next_input_job;
          cur_job->next_input_job++;

          result.file = strdup(cur_job->input_files[result.task]);
          new_job->file = strdup(cur_job->input_files[result.task]);

          result.output_dir = strdup(cur_job->output_dir);
          new_job->output_dir = strdup(cur_job->output_dir);

          result.app = strdup(cur_job->app_name);
          new_job->app = strdup(cur_job->app_name);

          result.n_reduce =  cur_job->n_reduce;
          new_job->n_reduce =  cur_job->n_reduce;

          result.n_map = cur_job->num_input_files;
          new_job->n_map = cur_job->num_input_files;

          result.reduce = false;
          new_job->reduce = false;

          result.wait = false;
          new_job->wait = false;

          result.args.args_val = strdup(cur_job->args);
          new_job->args.args_val = strdup(cur_job->args);

          result.args.args_len = strlen(cur_job->args);
          new_job->args.args_len = strlen(cur_job->args);

          new_job->assigned = time(NULL);

          state->assigned_tasks = g_list_append(state->assigned_tasks, new_job);

          return &result;

        } else if (cur_job->num_input_completed == cur_job->num_input_files && cur_job->next_reduce_job < cur_job->n_reduce){
          
          char* str = "Reduce Task";

          result.job_id = cur_job->job_id;
          new_job->job_id = cur_job->job_id;

          result.task = cur_job->next_reduce_job;
          new_job->task = cur_job->next_reduce_job;
          cur_job->next_reduce_job++;

          result.file = strdup(str);
          new_job->file = strdup(str);

          result.output_dir = strdup(cur_job->output_dir);
          new_job->output_dir = strdup(cur_job->output_dir);

          result.app = strdup(cur_job->app_name);
          new_job->app = strdup(cur_job->app_name);

          result.n_reduce =  cur_job->n_reduce;
          new_job->n_reduce =  cur_job->n_reduce;

          result.n_map = cur_job->num_input_files;
          new_job->n_map = cur_job->num_input_files;

          result.reduce = true;
          new_job->reduce = true;

          result.wait = false;
          new_job->wait = false;

          result.args.args_val = strdup(cur_job->args);
          new_job->args.args_val = strdup(cur_job->args);

          result.args.args_len = strlen(cur_job->args);
          new_job->args.args_len = strlen(cur_job->args);

          new_job->assigned = time(NULL);

          state->assigned_tasks = g_list_append(state->assigned_tasks, new_job);

          return &result;
        }
      }
    }
  }
  return &result;
}

/* FINISH_TASK RPC implementation. */
void* finish_task_1_svc(finish_task_request* argp, struct svc_req* rqstp) {
  static char* result;

  // printf("Received finish task request\n");

  job_request* cur_job = g_hash_table_lookup(state->job_data, GINT_TO_POINTER(argp->job_id));

  for (GList* elem = state->assigned_tasks; elem; elem = elem->next) {
    assigned_job* new_job = (assigned_job*) elem->data;
    if (argp->job_id == new_job->job_id && argp->task == new_job->task && argp->reduce == new_job->reduce){
      state->assigned_tasks = g_list_delete_link(state->assigned_tasks, elem);
      free(new_job);
      break;
    }
  }

  if (!argp->success){
    cur_job->failed = true;
    cur_job->done = true;
  } else {
    if (argp->reduce){
      cur_job->num_reduce_completed++;
      if (cur_job->num_reduce_completed == cur_job->n_reduce){
        cur_job->done = true;
      }
    } else {
      cur_job->num_input_completed++;
    }
  }

  // printf("Success %d, Name: %d, Reduce Finish: %d, Completed Reduce: %d, Completed Input: %d\n", argp->success, cur_job->job_id, argp->reduce, cur_job->num_reduce_completed, cur_job->num_input_completed);


  /* TODO */

  return (void*)&result;
}

/* Initialize coordinator state. */
void coordinator_init(coordinator** coord_ptr) {
  *coord_ptr = malloc(sizeof(coordinator));

  coordinator* coord = *coord_ptr;
  coord->job_id = 0;
  coord->queue_order = NULL;
  coord->assigned_tasks = NULL;
  coord->job_data = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

  /* TODO */
}
