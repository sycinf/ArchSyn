extern "C" double spmv(volatile double* y, volatile int* ptr, volatile double* valArray, volatile int* indArray, volatile double* xvec, int dim);
double spmv(volatile double* y,volatile int* ptr,volatile double* valArray,volatile int* indArray,volatile double* xvec, int dim)
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
