# TrabalhoGB PAD - Fractal de Mandelbrot

Este projeto implementa uma versão paralela do Fractal de Mandelbrot utilizando threads em C. O programa gera uma imagem PPM colorida do fractal, distribuindo o trabalho entre múltiplas threads para melhor performance.

## Características

- Implementação paralela usando pthreads
- Geração de imagem PPM colorida
- Processamento em blocos para melhor distribuição de trabalho
- Sistema de fila para gerenciamento de resultados

## Requisitos

- Compilador C (gcc recomendado)

## Compilação

Para compilar o projeto, execute:

```bash
gcc -pthread mandelbrot.c -o mandelbrot
```

## Execução

Para executar o programa:

```bash
./mandelbrot
```

O programa irá gerar um arquivo `mandelbrot.ppm` contendo a imagem do fractal.
É possível salvar um output com o log da execução: 
```bash
./mandelbrot > output.log
```

## Parâmetros Configuráveis

Os seguintes parâmetros podem ser ajustados no código fonte:

- `WIDTH` e `HEIGHT`: Dimensões da imagem (padrão: 800x800)
- `MAX_ITERATIONS`: Número máximo de iterações (padrão: 1000)
- `BLOCK_SIZE`: Tamanho dos blocos de processamento (padrão: 10)
- `NUM_THREADS`: Número de threads trabalhadoras (padrão: 4)

## Estrutura do Projeto

- `mandelbrot.c`: Código fonte principal
- `mandelbrot.ppm`: Imagem gerada do fractal
- `output.log`: Log de execução

## Arquitetura

O programa utiliza uma arquitetura produtor-consumidor com:
- Threads trabalhadoras que calculam partes do fractal
- Thread de impressão que monta a imagem final
- Fila de resultados para comunicação entre threads
- Sistema de blocos para distribuição eficiente do trabalho

## Autores

Cinthia Becher e Gabrielle Bussolo

