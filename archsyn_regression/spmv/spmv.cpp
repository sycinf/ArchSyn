extern "C" float spmv(volatile float* y, volatile int* ptr, volatile float* valArray, volatile int* indArray, volatile float* xvec, int dim);
float spmv(volatile float* y,volatile int* ptr,volatile float* valArray,volatile int* indArray,volatile float* xvec, int dim)
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
		kbegin = kend;
		rtVal = curY;
	}
	return rtVal;
}
