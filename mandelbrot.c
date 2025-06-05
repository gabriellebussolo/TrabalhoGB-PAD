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

// Limites do plano complexo (região do fractal)
#define X_MIN -2.0
#define X_MAX 2.0
#define Y_MIN -2.0
#define Y_MAX 2.0

// Estrutura para representar um bloco
typedef struct
{
    int start_x;
    int start_y;
    int end_x;
    int end_y;
    unsigned char *buffer; // Buffer para armazenar os pixels RGB
} Block;

// Estrutura para argumentos da thread
typedef struct
{
    Block *blocks;
    int num_blocks;
    int thread_id;
} ThreadArgs;

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

// Função para processar um bloco específico
void process_block(Block *block)
{
    for (int y = block->start_y; y < block->end_y; y++)
    {
        for (int x = block->start_x; x < block->end_x; x++)
        {
            // Mapeia as coordenadas do pixel para o plano complexo
            double complex c = (X_MIN + (X_MAX - X_MIN) * x / WIDTH) +
                               (Y_MIN + (Y_MAX - Y_MIN) * y / HEIGHT) * I;

            int iterations = calculate_mandelbrot(c);
            int r, g, b;
            get_color(iterations, &r, &g, &b);

            // Calcula a posição no buffer
            int buffer_pos = ((y - block->start_y) * (block->end_x - block->start_x) +
                              (x - block->start_x)) *
                             3;

            // Armazena os valores RGB no buffer
            block->buffer[buffer_pos] = r;
            block->buffer[buffer_pos + 1] = g;
            block->buffer[buffer_pos + 2] = b;
        }
    }
}

// Função que cada thread executará
void *thread_function(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    int blocks_per_thread = args->num_blocks / NUM_THREADS;
    int start_block = args->thread_id * blocks_per_thread;
    int end_block = (args->thread_id == NUM_THREADS - 1) ? args->num_blocks : start_block + blocks_per_thread;

    for (int i = start_block; i < end_block; i++)
    {
        process_block(&args->blocks[i]);
    }

    return NULL;
}

int main()
{
    // Calcula o número de blocos em cada dimensão
    int num_blocks_x = (WIDTH + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int num_blocks_y = (HEIGHT + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int total_blocks = num_blocks_x * num_blocks_y;

    // Cria array de blocos
    Block *blocks = (Block *)malloc(total_blocks * sizeof(Block));

    // Aloca um buffer grande para a imagem inteira
    unsigned char *image_buffer = (unsigned char *)malloc(WIDTH * HEIGHT * 3);
    if (!image_buffer)
    {
        printf("Erro ao alocar memória para o buffer da imagem\n");
        return 1;
    }

    // Inicializa os blocos e aloca os buffers
    int block_index = 0;
    for (int by = 0; by < num_blocks_y; by++)
    {
        for (int bx = 0; bx < num_blocks_x; bx++)
        {
            blocks[block_index].start_x = bx * BLOCK_SIZE;
            blocks[block_index].start_y = by * BLOCK_SIZE;
            blocks[block_index].end_x = (bx + 1) * BLOCK_SIZE;
            blocks[block_index].end_y = (by + 1) * BLOCK_SIZE;

            // Ajusta os limites do último bloco em cada dimensão
            if (blocks[block_index].end_x > WIDTH)
                blocks[block_index].end_x = WIDTH;
            if (blocks[block_index].end_y > HEIGHT)
                blocks[block_index].end_y = HEIGHT;

            // Aloca o buffer para este bloco (3 bytes por pixel: RGB)
            int block_width = blocks[block_index].end_x - blocks[block_index].start_x;
            int block_height = blocks[block_index].end_y - blocks[block_index].start_y;
            blocks[block_index].buffer = (unsigned char *)malloc(block_width * block_height * 3);

            block_index++;
        }
    }

    // Cria as threads
    pthread_t threads[NUM_THREADS];
    ThreadArgs thread_args[NUM_THREADS];

    // Inicializa e inicia as threads
    for (int i = 0; i < NUM_THREADS; i++)
    {
        thread_args[i].blocks = blocks;
        thread_args[i].num_blocks = total_blocks;
        thread_args[i].thread_id = i;
        pthread_create(&threads[i], NULL, thread_function, &thread_args[i]);
    }

    // Espera todas as threads terminarem
    for (int i = 0; i < NUM_THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // Copia os blocos para o buffer da imagem
    for (int i = 0; i < total_blocks; i++)
    {
        int block_width = blocks[i].end_x - blocks[i].start_x;
        int block_height = blocks[i].end_y - blocks[i].start_y;

        for (int y = 0; y < block_height; y++)
        {
            // Calcula as posições nos buffers
            int src_pos = y * block_width * 3;
            int dst_pos = ((blocks[i].start_y + y) * WIDTH + blocks[i].start_x) * 3;

            // Copia uma linha do bloco para o buffer da imagem
            memcpy(&image_buffer[dst_pos], &blocks[i].buffer[src_pos], block_width * 3);
        }

        // Libera o buffer do bloco
        free(blocks[i].buffer);
    }

    // Abre o arquivo para escrita
    FILE *fp = fopen("mandelbrot.ppm", "wb");
    if (!fp)
    {
        printf("Erro ao criar arquivo de saída\n");
        free(blocks);
        free(image_buffer);
        return 1;
    }

    // Escreve o cabeçalho do arquivo PPM
    fprintf(fp, "P6\n%d %d\n255\n", WIDTH, HEIGHT);

    // Escreve todo o buffer da imagem no arquivo
    fwrite(image_buffer, 1, WIDTH * HEIGHT * 3, fp);

    // Limpa
    free(blocks);
    free(image_buffer);
    fclose(fp);
    printf("Imagem do Fractal de Mandelbrot gerada com sucesso!\n");
    return 0;
}