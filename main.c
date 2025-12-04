#define _GNU_SOURCE
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

// Definições de Tempo
#define NSEC_PER_SEC 1000000000L

// 1. ESTRUTURAS BÁSICAS DA FILA (Jobs)

typedef void (*job_func_t)(void *arg);

typedef struct job {
    job_func_t func; //funcao a ser executada
    void *arg;
    struct job *next;
} job_t;

static job_t *queue_head = NULL;
static job_t *queue_tail = NULL;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

// Adiciona job na fila (Chamado pelo gerador aperiódico)
void enqueue_job(job_func_t f, void *arg) {
    job_t *j = (job_t*)malloc(sizeof(job_t));
    j->func = f;
    j->arg = arg; //id evento
    j->next = NULL;

    pthread_mutex_lock(&queue_mutex);
    if (queue_tail)
        queue_tail->next = j; //atual aponta para o novo
    else
        queue_head = j; //novo é o primeiro
    
    queue_tail = j;
    pthread_cond_signal(&queue_cond); 
    pthread_mutex_unlock(&queue_mutex); //"Destranca a porta"
}

// Retira job da fila (Usado pelo servidor)
job_t *dequeue_job(void) {
    job_t *j = queue_head;
    if (!j) return NULL;
    
    queue_head = j->next; //segundo vira o primeiro
    if (!queue_head) queue_tail = NULL; //se acabou, zera o ultimo
    
    return j;
}

// ==========================================
// 2. FUNÇÕES AUXILIARES DE TEMPO

static void timespec_add_ns(struct timespec *t, long ns) {
    t->tv_nsec += ns;
    while (t->tv_nsec >= NSEC_PER_SEC) { //se passou e 1 bilhao de ns
        t->tv_nsec -= NSEC_PER_SEC; //subtrai 1 bilhao
        t->tv_sec += 1; //aficiona 1 segundo
    }
}

typedef struct {
    long period_ns; // Ts (Período do servidor)
    long budget_ns; // Cs (Orçamento de execução)
} server_params_t;


// (NAV_PLAN - Simplificada) ---> é aperiódica pois só acontece quandomuda algo no ambiente, ou piloto

void nav_plan_job(void *arg) {
    //trava a cpu propositalmente por 2.5ms para simular o esforço computacional
    int id = *((int*)arg);// recupera ID
    free(arg); // Libera a memória do ID
    
    // --- LÓGICA DA NAV_PLAN ---
    usleep(2500); 
    
    printf("[NAV_PLAN] Evento %d processado com sucesso (Carga ~2.5ms)\n", id);
}

// 4. THREAD SERVIDOR PERIÓDICO

void *server_thread(void *arg) {
    server_params_t *params = (server_params_t*)arg;
    long Ts = params->period_ns; //periodo 10 ms
    long Cs = params->budget_ns; // orçamento 3ms

    struct timespec next_release;
    clock_gettime(CLOCK_MONOTONIC, &next_release);

    printf(">>> Servidor Iniciado (Ts=%ldns, Cs=%ldns)\n", Ts, Cs);

    while (1) {
        // Define momento da próxima ativação (Período)
        timespec_add_ns(&next_release, Ts);

        // Início do período (medição do orçamento)
        struct timespec start_period;
        clock_gettime(CLOCK_MONOTONIC, &start_period);
        long consumed_ns = 0;

        // Loop de consumo do orçamento
        while (consumed_ns < Cs) {
            
            // Tenta pegar um job da fila com proteçãpoo mutex
            pthread_mutex_lock(&queue_mutex);
            if (!queue_head) {
                // Fila vazia: servidor descansa até o próximo período
                pthread_mutex_unlock(&queue_mutex);
                goto end_of_service;
            }
            job_t *j = dequeue_job();
            pthread_mutex_unlock(&queue_mutex);

            // Mede tempo ANTES de executar
            struct timespec t_before, t_after;
            clock_gettime(CLOCK_MONOTONIC, &t_before);
            
            // EXECUTA A NAV_PLAN
            if (j) {
                j->func(j->arg);
                free(j); // Libera a estrutura do job
            }

            // Mede tempo DEPOIS de executar
            clock_gettime(CLOCK_MONOTONIC, &t_after);

            // Calcula quanto gastou e desconta do orçamento
            long dt = (t_after.tv_sec - t_before.tv_sec) * NSEC_PER_SEC + 
                      (t_after.tv_nsec - t_before.tv_nsec);
            
            consumed_ns += dt;

            // Se o orçamento estourou, para forçadamente
            //exemplo: Termina o evento 397, o priximo item da lista, vai para a fila
            if (consumed_ns >= Cs) {
                printf("!!! Orçamento Esgotado (%ld > %ld) - Adia resto da fila !\n", consumed_ns, Cs);
                break;
            }
        }

end_of_service:
        // Dorme até o início exato do próximo período
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_release, NULL);
    }
    return NULL;
}


// 5. GERADOR DE CARGA (Simula Interrupções)

// Função que encapsula a criação do job (conforme PDF)
void requisicao_aperiodica(int id) {
    
    int *p = (int*)malloc(sizeof(int));
    *p = id; 
    
    // Chama a função de enfileirar
    enqueue_job(nav_plan_job, p);
}

// Thread Gerador
void *gerador_aperiodico(void *arg) {
    int id_counter = 0;
    srand(time(NULL));

    while(1) {
        // Espera aleatória
        usleep(20000 + (rand() % 80000)); 
        
        // Chama a função auxiliar corrigida
        requisicao_aperiodica(++id_counter);
    }
    return NULL;
}

// 6. MAIN

int main() {
    pthread_t th_server, th_gen;
    pthread_attr_t attr;
    struct sched_param sp;

    // --- Configuração RT (SCHED_FIFO) para o Servidor ---
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    
    // Prioridade Alta (60)
    sp.sched_priority = 60;
    pthread_attr_setschedparam(&attr, &sp);

    // Configura Servidor: Período 10ms, Orçamento 3ms
    static server_params_t params = {
        .period_ns = 10L * 1000000L, 
        .budget_ns = 3L * 1000000L
    };

    // Cria Thread do Servidor (RT)
    if (pthread_create(&th_server, &attr, server_thread, &params) != 0) {
        perror("Erro ao criar thread RT (execute com sudo)");
        return 1;
    }
    pthread_attr_destroy(&attr);

    // Cria Thread do Gerador (Normal)
    pthread_create(&th_gen, NULL, gerador_aperiodico, NULL);

    // Aguarda threads
    pthread_join(th_server, NULL);
    pthread_join(th_gen, NULL);

    return 0;
}