#include <stdio.h>
#include <stdlib.h>
#include <complex.h>

#define WIDTH 800
#define HEIGHT 600
#define MAX_ITERATIONS 1000

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

int main()
{
    FILE *fp = fopen("mandelbrot.ppm", "wb");
    if (!fp)
    {
        printf("Erro ao criar arquivo de saída\n");
        return 1;
    }

    // Escreve o cabeçalho do arquivo PPM
    fprintf(fp, "P6\n%d %d\n255\n", WIDTH, HEIGHT);

    // Define os limites do plano complexo
    double x_min = -2.0;
    double x_max = 1.0;
    double y_min = -1.0;
    double y_max = 1.0;

    // Gera a imagem pixel por pixel
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            // Mapeia as coordenadas do pixel para o plano complexo
            double complex c = (x_min + (x_max - x_min) * x / WIDTH) +
                               (y_min + (y_max - y_min) * y / HEIGHT) * I;

            int iterations = calculate_mandelbrot(c);
            int r, g, b;
            get_color(iterations, &r, &g, &b);

            // Escreve os valores RGB no arquivo
            fputc(r, fp);
            fputc(g, fp);
            fputc(b, fp);
        }
    }

    fclose(fp);
    printf("Imagem do Fractal de Mandelbrot gerada com sucesso!\n");
    return 0;
}