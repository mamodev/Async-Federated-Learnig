#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void simple_wavg(int rows, int cols, double dest[cols], double numbers[rows][cols], double w[rows])
{
  double total_w = 0;
  for (int i = 0; i < rows; i++)
  {
    total_w += w[i];
  }

  for (int i = 0; i < cols; i++)
  {
    dest[i] = 0;
    for (int j = 0; j < rows; j++)
    {
      dest[i] += numbers[j][i] * w[j] / total_w;
    }
  }

  return;
}

double rolling_wavg(double total_weights, size_t v_size, double *v, double w, double *dest_v)
{
  double new_total_w = total_weights + w;
  for (size_t i = 0; i < v_size; i++)
  {
    dest_v[i] = dest_v[i] * total_weights + v[i] * w;
    dest_v[i] /= new_total_w;
  }

  return new_total_w;
}

int main(void)
{
  int nc = 10;
  int nw = 20;

  double numbers[nc][nw];
  double weights[nc];

  for (int i = 0; i < nc; i++)
  {
    weights[i] = ((double)rand() / RAND_MAX) * 1000;
  }

  for (int i = 0; i < nc; i++)
  {
    for (int j = 0; j < nw; j++)
    {
      numbers[i][j] = (double)rand() / RAND_MAX;
    }
  }

  for (int i = 0; i < nc; i++)
  {
    // printf("Client %d: ", i);
    printf("W[%1.2f]", i, weights[i]);
    for (int j = 0; j < nw; j++)
    {
      printf("%1.2f ", numbers[i][j]);
    }
    printf("\n");
  }

  double wavg[nw];
  simple_wavg(nc, nw, wavg, numbers, weights);

  printf("Average:  ");
  for (int i = 0; i < nw; i++)
  {
    printf("%1.3f ", wavg[i]);
  }
  printf("\n");
  fflush(stdout);

  memset(wavg, 0, nw * sizeof(double));
  double total_w = 1;
  for (int i = 0; i < nc; i++)
  {
    total_w = rolling_wavg(total_w, nw, numbers[i], weights[i], wavg);
  }

  printf("Average:  ");
  for (int i = 0; i < nw; i++)
  {
    printf("%1.3f ", wavg[i]);
  }
  printf("\n");
  fflush(stdout);

  return 0;
}