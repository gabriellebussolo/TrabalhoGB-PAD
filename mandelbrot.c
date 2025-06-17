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
#define X_MIN -2.0
#define X_MAX 2.0
#define Y_MIN -2.0
#define Y_MAX 2.0

// Estrutura para representar um bloco
typedef struct
{
    int block_id;
    int start_x;
    int start_y;
    int end_x;
    int end_y;
} Block;

// Estrutura para argumentos das threads trabalhadoras
typedef struct
{
    int thread_id;
} WorkerArgs;

// Estrutura para armazenar o resultado de um bloco
typedef struct
{
    unsigned char *pixels; // Os dados RGB para todos os pixels deste bloco
    Block *block;          // Bloco de xy
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

// Fila de resultados
ResultsQueue results_queue;

// Buffer de trabalho
Block *workBuffer;

// Numero total de blocos
int total_blocks;

// Indica o próximo bloco a ser processado pelas workers
int next_block = 0;

pthread_mutex_t worker_mutex;  // Mutex específico para o trabalhador pegar o próximo bloco
pthread_mutex_t printer_mutex; // Mutex específico para o printer pegar o próximo resultado
pthread_cond_t has_results;    // Sinaliza que há resultados na fila

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
    queue->results[queue->tail] = result; // adiciona o resultado no fim da fila
    queue->tail++;
    queue->count++;
}

// Função para retirar um resultado da fila
BlockResult dequeue(ResultsQueue *queue)
{
    BlockResult result = queue->results[queue->head];
    queue->head++;
    queue->count--;
    printf("Thread %d (printer): Pegou bloco %d da fila de resultados...\n",
           (int)pthread_self(), result.block->block_id);
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
    while (next_block < total_blocks)
    {
        int current_block_id;
        // Obter o próximo bloco a ser processado
        pthread_mutex_lock(&worker_mutex);
        current_block_id = next_block;
        next_block++;
        printf("Thread %d (worker): pegou bloco %d do buffer de trabalho.\n",
               args->thread_id, current_block_id);
        pthread_mutex_unlock(&worker_mutex);

        // Calcula o Mandelbrot para o bloco
        Block *current_block = &workBuffer[current_block_id];
        unsigned char *block_RGBpixels = process_block(current_block);

        // Cria o resultado do bloco
        BlockResult result;
        result.pixels = block_RGBpixels;
        result.block = current_block;

        pthread_mutex_lock(&printer_mutex);
        enqueue(&results_queue, result); // Adiciona o resultado na fila de resultados
        pthread_mutex_unlock(&printer_mutex);
        pthread_cond_signal(&has_results); // Sinaliza que há novos resultados

        printf("Thread %d (worker): adicionou bloco %d no buffer de resultados.\n",
               args->thread_id, current_block_id);
    }
    return NULL;
}

// Função que a thread printer executará
void *printer_function()
{
    int processed_count = 0;

    // Aloca memória para o buffer da imagem final e inicializa com zeros (preto)
    unsigned char *image_buffer = (unsigned char *)calloc(WIDTH * HEIGHT * 3, sizeof(unsigned char));
    if (image_buffer == NULL)
    {
        perror("Erro ao alocar memória para image_buffer");
        return NULL;
    }

    while (processed_count < total_blocks)
    {
        pthread_mutex_lock(&printer_mutex);
        // Espera se a fila estiver vazia
        while (results_queue.count == 0)
        {
            pthread_cond_wait(&has_results, &printer_mutex);
        }
        BlockResult result = dequeue(&results_queue); // Pega um resultado da fila
        pthread_mutex_unlock(&printer_mutex);

        // Copia os pixels do bloco para o image_buffer
        Block *block = result.block; // Bloco de xy
        int block_width = block->end_x - block->start_x;
        int block_height = block->end_y - block->start_y;
        unsigned char *block_pixels = result.pixels; // Ponteiro para os pixels do resultado

        int pixel_index = 0;
        for (int y = 0; y < block_height; y++)
        {
            for (int x = 0; x < block_width; x++)
            {
                int destination_pos = ((block->start_y + y) * WIDTH + block->start_x + x) * 3;
                image_buffer[destination_pos] = block_pixels[pixel_index++];
                image_buffer[destination_pos + 1] = block_pixels[pixel_index++];
                image_buffer[destination_pos + 2] = block_pixels[pixel_index++];
            }
        }

        processed_count++;

        printf("Thread %d (printer): atualizando imagem com %d blocos.\n", (int)pthread_self(), processed_count);
        FILE *fp = fopen("mandelbrot.ppm", "wb");
        if (fp == NULL)
        {
            perror("Erro ao abrir o arquivo temporário para escrita");
        }
        else
        {
            // P6 = formato binário | 255 = valor máximo de cor (RGB é de 0 a 255)
            fprintf(fp, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
            fwrite(image_buffer, 1, WIDTH * HEIGHT * 3, fp);
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
    total_blocks = num_blocks_x * num_blocks_y;

    // Ajusta o tamanho do bloco se a largura/altura não for divisível
    if (WIDTH % BLOCK_SIZE != 0)
        num_blocks_x++;
    if (HEIGHT % BLOCK_SIZE != 0)
        num_blocks_y++;

    total_blocks = num_blocks_x * num_blocks_y; // Recalcula com ajuste

    // Aloca memória para os blocos
    workBuffer = (Block *)malloc(total_blocks * sizeof(Block));
    if (workBuffer == NULL)
    {
        perror("Erro ao alocar memória para workBuffer");
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
            workBuffer[block_index].block_id = block_index;
            workBuffer[block_index].start_x = j * BLOCK_SIZE;
            workBuffer[block_index].start_y = i * BLOCK_SIZE;
            workBuffer[block_index].end_x = (j + 1) * BLOCK_SIZE > WIDTH ? WIDTH : (j + 1) * BLOCK_SIZE;
            workBuffer[block_index].end_y = (i + 1) * BLOCK_SIZE > HEIGHT ? HEIGHT : (i + 1) * BLOCK_SIZE;
            block_index++;
        }
    }

    // Cria as threads
    pthread_t threads[NUM_THREADS + 1]; // +1 para a thread de print na tela
    WorkerArgs thread_args[NUM_THREADS];

    // Inicializa a thread printer
    pthread_create(&threads[NUM_THREADS], NULL, printer_function, NULL);
    printf("Thread printer criada.\n");

    // Inicializa e inicia as threads workers de processamento
    for (int i = 0; i < NUM_THREADS; i++)
    {
        thread_args[i].thread_id = i;

        pthread_create(&threads[i], NULL, worker_function, &thread_args[i]);
        printf("Thread %d (worker) criada.\n", i);
    }

    // Espera todas as threads terminarem
    for (int i = 0; i < NUM_THREADS + 1; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("Geração do fractal de Mandelbrot concluída. Verifique mandelbrot.ppm\n");

    return 0;
}