#include<stdio.h>
#include<stdlib.h>
#define DIM 50
#define SPARSITY 2
float spmv( float* y,  int* ptr,  float* valArray,  int* indArray, float* xvec, int dim);
float spmv_sw(float* y, int* ptr, float* valArray, int* indArray, float* xvec, int dim)
{
	int kbegin = 0;
	float rtVal = 0.0;
	for(int s=0; s<dim; s++)
	{
		int kend = ptr[s];
		float curY = 0.0;
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

	float* xvec = (float*)malloc(DIM*sizeof(float));
	for(i=0; i<DIM; i++)
	{
		xvec[i] = (float)(i);
	}
	int* indArray;
	float* valArray;

	valArray=(float*) malloc(totalNum*sizeof(float));
	int j;
	for(j=0; j<totalNum; j++)
	{
		valArray[j] = (float)(rand()%DIM);
	}

	indArray=(int*)malloc(totalNum*sizeof(int));
	int k;
	for(k=0; k<totalNum; k++)
		indArray[k]=rand()%DIM;

	float* y = (float*)malloc(DIM*sizeof(float));
	for(k=0; k<DIM; k++)
	{
		y[k] = 0.0;
	}
	float* y2 = (float*)malloc(DIM*sizeof(float));
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
