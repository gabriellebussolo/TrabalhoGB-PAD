#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <pthread.h>
#include <string.h>

#define WIDTH 800
#define HEIGHT 800
#define MAX_ITERATIONS 1000
#define BLOCK_SIZE 10
#define NUM_THREADS 4

// Limites da região do fractal
#define X_MIN -2.5
#define X_MAX 1.5
#define Y_MIN -2.0
#define Y_MAX 2.0

// Estrutura para representar um bloco
typedef struct
{
    int start_x;
    int start_y;
    int end_x;
    int end_y;
} Block;

// Estrutura para argumentos das threads trabalhadoras
typedef struct
{
    Block *blocks; // Blocos de xy
    int num_blocks;
    int thread_id;
} WorkerArgs;

// Estrutura para argumentos da thread de print na tela
typedef struct
{
    unsigned char *image_buffer;
    int num_blocks;
    Block *blocks; // Blocos de RGB
} PrinterArgs;

// Estrutura para armazenar o resultado de um bloco
typedef struct
{
    int block_id;
    unsigned char *pixels; // Os dados RGB para todos os pixels deste bloco
} BlockResult;

// Estrutura de uma fila para o buffer de resultados
typedef struct
{
    BlockResult *results;
    int capacity;
    int head;
    int tail;
    int count;
} ResultsQueue;

// Variável global para a fila de resultados
ResultsQueue results_queue;

// Variável global para indicar o próximo bloco a ser processado pelas workers
int next_block = 0;
pthread_mutex_t worker_mutex; // Mutex específico para o trabalhador pegar o próximo bloco
pthread_mutex_t printer_mutex; // Mutex específico para o printer pegar o próximo resultado
pthread_cond_t has_results; // Sinaliza que há resultados na fila

// Função para inicializar a fila de resultados
void init_queue(ResultsQueue *queue, int capacity)
{
    queue->capacity = capacity;
    queue->results = (BlockResult *)malloc(capacity * sizeof(BlockResult));
    if (queue->results == NULL)
    {
        perror("Erro ao alocar memória para a fila de resultados");
        exit(EXIT_FAILURE);
    }
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

// Função para adicionar um resultado à fila
void enqueue(ResultsQueue *queue, BlockResult result)
{
    pthread_mutex_lock(&printer_mutex);
    queue->results[queue->tail] = result; // adiciona o resultado no fim da fila
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    pthread_mutex_unlock(&printer_mutex);
    pthread_cond_signal(&has_results); // Sinaliza que há novos resultados
}

// Função para retirar um resultado da fila
BlockResult dequeue(ResultsQueue *queue)
{
    pthread_mutex_lock(&printer_mutex);
    // Espera se a fila estiver vazia
    while (queue->count == 0)
    {
        pthread_cond_wait(&has_results, &printer_mutex);
    }
    BlockResult result = queue->results[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    printf("Thread %d (printer): Pegou bloco %d da fila de resultados...\n", (int)pthread_self(), result.block_id);
    pthread_mutex_unlock(&printer_mutex);
    return result;
}

// Função para calcular o número de iterações para um ponto específico
int calculate_mandelbrot(double complex c)
{
    double complex z = 0;
    int iterations = 0;

    while (cabs(z) <= 2.0 && iterations < MAX_ITERATIONS)
    {
        z = z * z + c;
        iterations++;
    }

    return iterations;
}

// Função para gerar cores baseadas no número de iterações
void get_color(int iterations, int *r, int *g, int *b)
{
    if (iterations == MAX_ITERATIONS)
    {
        *r = *g = *b = 0; // Preto para pontos dentro do conjunto
    }
    else
    {
        // Gradiente de cores para pontos fora do conjunto
        *r = (iterations * 7) % 256;
        *g = (iterations * 5) % 256;
        *b = (iterations * 11) % 256;
    }
}

// Função para calcular Mandelbrot para um bloco específico (retorna os pixels)
unsigned char *process_block(Block *block)
{
    int block_width = block->end_x - block->start_x;
    int block_height = block->end_y - block->start_y;
    unsigned char *pixels = (unsigned char *)malloc(block_width * block_height * 3);
    if (pixels == NULL)
    {
        perror("Erro ao alocar memória para pixels do bloco");
        exit(EXIT_FAILURE);
    }

    int pixel_index = 0;
    for (int y = block->start_y; y < block->end_y; y++)
    {
        for (int x = block->start_x; x < block->end_x; x++)
        {
            // Mapeia as coordenadas do pixel para o plano complexo
            double complex c = (X_MIN + (X_MAX - X_MIN) * x / WIDTH) + (Y_MIN + (Y_MAX - Y_MIN) * y / HEIGHT) * I;

            int iterations = calculate_mandelbrot(c);

            int r, g, b;
            get_color(iterations, &r, &g, &b);

            pixels[pixel_index++] = (unsigned char)r;
            pixels[pixel_index++] = (unsigned char)g;
            pixels[pixel_index++] = (unsigned char)b;
        }
    }
    return pixels;
}

// Função que cada thread trabalhadora executará
void *worker_function(void *arg)
{
    WorkerArgs *args = (WorkerArgs *)arg;
    while (1)
    {
        int block_id;

        // Região crítica 1: obter o próximo bloco a ser processado
        pthread_mutex_lock(&worker_mutex); // Usa o mutex específico para workers
        if (next_block >= args->num_blocks)
        {
            pthread_mutex_unlock(&worker_mutex);
            break; // Todos os blocos foram atribuídos
        }
        block_id = next_block;
        next_block++;
        printf("Thread %d (worker): pegou bloco %d do buffer de trabalho.\n", args->thread_id, block_id);
        pthread_mutex_unlock(&worker_mutex);

        // Calcula o Mandelbrot para o bloco
        Block *current_block = &args->blocks[block_id];
        unsigned char *block_pixels = process_block(current_block);

        // Cria o resultado do bloco
        BlockResult result;
        result.block_id = block_id;
        result.pixels = block_pixels;
        enqueue(&results_queue, result); // Adiciona o resultado na fila global
        printf("Thread %d (worker): adicionou bloco %d no buffer de resultados.\n", args->thread_id, block_id);
    }
    return NULL;
}

// Função que a thread printer executará
void *printer_function(void *arg)
{
    PrinterArgs *args = (PrinterArgs *)arg;
    int processed_count = 0;

    while (processed_count < args->num_blocks)
    {
        BlockResult result = dequeue(&results_queue); // Pega um resultado da fila

        // Copia os pixels do bloco para o image_buffer
        Block *block = &args->blocks[result.block_id];
        int block_width = block->end_x - block->start_x;
        int block_height = block->end_y - block->start_y;
        unsigned char *block_pixels = result.pixels; // Ponteiro para os pixels do resultado

        int pixel_index = 0;
        for (int y = 0; y < block_height; y++)
        {
            for (int x = 0; x < block_width; x++)
            {
                int dst_pos = ((block->start_y + y) * WIDTH + block->start_x + x) * 3;
                args->image_buffer[dst_pos] = block_pixels[pixel_index++];
                args->image_buffer[dst_pos + 1] = block_pixels[pixel_index++];
                args->image_buffer[dst_pos + 2] = block_pixels[pixel_index++];
            }
        }

        free(block_pixels); // Libera a memória alocada para os pixels deste bloco
        processed_count++;

        // Salva a imagem periodicamente para mostrar o progresso
        printf("Thread %d (printer): atualizando imagem com %d blocos.\n", (int)pthread_self(), processed_count);
        FILE *fp = fopen("mandelbrot.ppm", "wb");
        if (fp == NULL)
        {
            perror("Erro ao abrir o arquivo temporário para escrita");
        }
        else
        {
            fprintf(fp, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
            fwrite(args->image_buffer, 1, WIDTH * HEIGHT * 3, fp);
            fclose(fp);
        }
        printf("Thread %d (printer): imagem atualizada.\n", (int)pthread_self());
    }
    printf("Thread printer finalizada.\n");
    return NULL;
}

int main()
{
    // Calcula o número total de blocos
    int num_blocks_x = WIDTH / BLOCK_SIZE;
    int num_blocks_y = HEIGHT / BLOCK_SIZE;
    int total_blocks = num_blocks_x * num_blocks_y;

    // Ajusta o tamanho do bloco se a largura/altura não for divisível
    if (WIDTH % BLOCK_SIZE != 0)
        num_blocks_x++;
    if (HEIGHT % BLOCK_SIZE != 0)
        num_blocks_y++;

    total_blocks = num_blocks_x * num_blocks_y; // Recalcula com ajuste

    // Aloca memória para os blocos
    Block *blocks = (Block *)malloc(total_blocks * sizeof(Block));
    if (blocks == NULL)
    {
        perror("Erro ao alocar memória para blocos");
        return 1;
    }

    // Aloca memória para o buffer da imagem final e inicializa com zeros (preto)
    unsigned char *image_buffer = (unsigned char *)calloc(WIDTH * HEIGHT * 3, sizeof(unsigned char));
    if (image_buffer == NULL)
    {
        perror("Erro ao alocar memória para image_buffer");
        free(blocks);
        return 1;
    }

    // Inicializa o mutex dos workers
    pthread_mutex_init(&worker_mutex, NULL);

    // Inicializa o mutex e a condição do printer
    pthread_mutex_init(&printer_mutex, NULL);
    pthread_cond_init(&has_results, NULL);

    // Inicializa a fila de resultados
    init_queue(&results_queue, total_blocks);

    // Inicializa os blocos
    int block_index = 0;
    for (int i = 0; i < num_blocks_y; i++)
    {
        for (int j = 0; j < num_blocks_x; j++)
        {
            blocks[block_index].start_x = j * BLOCK_SIZE;
            blocks[block_index].start_y = i * BLOCK_SIZE;
            blocks[block_index].end_x = (j + 1) * BLOCK_SIZE > WIDTH ? WIDTH : (j + 1) * BLOCK_SIZE;
            blocks[block_index].end_y = (i + 1) * BLOCK_SIZE > HEIGHT ? HEIGHT : (i + 1) * BLOCK_SIZE;
            block_index++;
        }
    }

    // Cria as threads
    pthread_t threads[NUM_THREADS + 1]; // +1 para a thread de print na tela
    WorkerArgs thread_args[NUM_THREADS];
    PrinterArgs printer_args;

    // Inicializa e inicia as threads de processamento
    for (int i = 0; i < NUM_THREADS; i++)
    {
        thread_args[i].blocks = blocks; // Ainda precisamos passar os blocos para as workers para elas saberem as dimensões
        thread_args[i].num_blocks = total_blocks;
        thread_args[i].thread_id = i;

        pthread_create(&threads[i], NULL, worker_function, &thread_args[i]);
        printf("Thread %d (worker) criada.\n", i);
    }

    // Inicializa e inicia a thread escritora
    printer_args.image_buffer = image_buffer;
    printer_args.num_blocks = total_blocks;
    printer_args.blocks = blocks; // A printer também precisa dos blocos para saber as dimensões e posições

    pthread_create(&threads[NUM_THREADS], NULL, printer_function, &printer_args);
    printf("Thread printer criada.\n");
    // Espera todas as threads terminarem
    for (int i = 0; i < NUM_THREADS + 1; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("Geração do fractal de Mandelbrot concluída. Verifique mandelbrot.ppm\n");

    return 0;
}