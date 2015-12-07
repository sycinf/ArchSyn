#include<stdio.h>
#include<stdlib.h>
#define DIM 50
#define SPARSITY 2
double spmv( double* y,  int* ptr,  double* valArray,  int* indArray, double* xvec, int dim);
double spmv_sw(double* y, int* ptr, double* valArray, int* indArray, double* xvec, int dim)
{
	int kbegin = 0;
	double rtVal = 0.0;
	for(int s=0; s<dim; s++)
	{
		int kend = ptr[s];
		double curY = 0.0;
		for(int k=kbegin; k<kend; k++)
		{
			curY=curY+valArray[k]*xvec[indArray[k]];
		}
		y[s] = curY;
		int kbegin = kend;
		rtVal = curY;
	}
	return rtVal;
}

int main()
{
	int* ptr = (int*)malloc(DIM*sizeof(int));
	int totalNum = 0;
	int i;
	for(i=0; i<DIM; i++)
	{
		int numEle = rand()%(DIM/SPARSITY);
		totalNum+=numEle;
		ptr[i] = totalNum;
	}

	double* xvec = (double*)malloc(DIM*sizeof(double));
	for(i=0; i<DIM; i++)
	{
		xvec[i] = (double)(i);
	}
	int* indArray;
	double* valArray;

	valArray=(double*) malloc(totalNum*sizeof(double));
	int j;
	for(j=0; j<totalNum; j++)
	{
		valArray[j] = (double)(rand()%DIM);
	}

	indArray=(int*)malloc(totalNum*sizeof(int));
	int k;
	for(k=0; k<totalNum; k++)
		indArray[k]=rand()%DIM;

	double* y = (double*)malloc(DIM*sizeof(double));
	for(k=0; k<DIM; k++)
	{
		y[k] = 0.0;
	}
	double* y2 = (double*)malloc(DIM*sizeof(double));
	for(k=0; k<DIM; k++)
	{
		y2[k] = 0.0;
	}
	spmv(y,ptr,valArray,indArray,xvec,DIM);
	spmv_sw(y2,ptr,valArray,indArray,xvec,DIM);
	

	for(k=0; k<DIM; k++)
		printf("%f ",y[k]);
	printf("\n");
	for(k=0; k<DIM; k++)
		printf("%f ",y2[k]);
	
}
