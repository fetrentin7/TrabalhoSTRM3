#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/////////////////////// Fila de requisições aperiódicas ///////////////////////////

typedef void (*job_func_t)(void *arg);

typedef struct job {
    job_func_t func;
    void *arg;
    struct job *next;
} job_t;

static job_t *queue_head = NULL;
static job_t *queue_tail = NULL;

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond  = PTHREAD_COND_INITIALIZER;

/////////////////////// Enfileira um job (chamado pelas tarefas aperiódicas) ///////////////////////

void enqueue_job(job_func_t f, void *arg) {
    job_t *j = malloc(sizeof(job_t));
    j->func = f;
    j->arg  = arg;
    j->next = NULL;

    pthread_mutex_lock(&queue_mutex);

    if (queue_tail)
        queue_tail->next = j;
    else
        queue_head = j;

    queue_tail = j;

    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

/////////////////////// Retira um job da fila (usado pelo servidor) ///////////////////////

job_t *dequeue_job(void) {
    job_t *j = queue_head;

    if (!j)
        return NULL;

    queue_head = j->next;

    if (!queue_head)
        queue_tail = NULL;

    return j;
}
void *event_generator(){

}

typedef struct{
    long period_ns;
    long budget_ns;
} server_params_t;
//long tip
void *server_thread( void *arg){
    server_params_t *params = (server_params_t *)arg;
    long Ts = params->period_ns;
    long Cs = params->budget_ns;

    
    struct timespec next_release;
    clock_gettime(CLOCK_MONOTONIC, &next_release);

    while (1) {
        timespec_add_ns(&next_release, Ts);   // próxima liberação
        struct timespec start_period;
        clock_gettime(CLOCK_MONOTONIC, &start_period);

        long elapsed = 0;

        while (elapsed < Cs) {
            pthread_mutex_lock(&queue_mutex);
            job_t *j = dequeue_job();
            pthread_mutex_unlock(&queue_mutex);

            if (!j) {
                // nada para fazer neste período
                break;
            }

            struct timespec t_before, t_after;
            clock_gettime(CLOCK_MONOTONIC, &t_before);
            j->func(j->arg);    // executa job
            clock_gettime(CLOCK_MONOTONIC, &t_after);

            elapsed = timespec_diff_ns(&t_after, &start_period);
            free(j);
        }

        // Dorme até o próximo período (tempo absoluto)
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                        &next_release, NULL);
    }
    return NULL;
}


void *nav_job(){


}
int main(int argc, char const *argv[])
{
    
    return 0;
}
