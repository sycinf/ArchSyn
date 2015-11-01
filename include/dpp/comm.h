#ifndef _COMM_H
#define _COMM_H
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define FIFO_SIZE 2
#define u64 long
template<typename T>
struct single_fifo
{
	T storage[FIFO_SIZE];
	int	   head;
	int	   tail;
	int    space;
	int    id;
	pthread_mutex_t* my_mutex;
	pthread_cond_t* my_cond ;

	void init(int fifoId,pthread_mutex_t* _my_mutex,pthread_cond_t* _my_cond)
	{
		head = 0;
		tail = 0;
		id = fifoId;
		space = FIFO_SIZE;
		my_mutex = _my_mutex;
		my_cond = _my_cond;
	}
	void write(T i)
	{
		pthread_mutex_lock(my_mutex);
		while(space == 0)
		{
			pthread_cond_wait(my_cond, my_mutex);
		}
		storage[head] = i;
		space--;
		head = (head+1)%FIFO_SIZE;
		// wake people up when we've just wrote something
		// into an empty fifo
		if(space == FIFO_SIZE-1)
			pthread_cond_signal(my_cond);
		pthread_mutex_unlock(my_mutex);
								
	}

	T read()
	{
		pthread_mutex_lock(my_mutex);
		while(space == FIFO_SIZE)
		{
			pthread_cond_wait(my_cond, my_mutex);
		}
		T rt_val = storage[tail];
		space++;
		tail = (tail+1)%FIFO_SIZE;
		// wake ppl up when we free up some space
		if(space==1)
			pthread_cond_signal(my_cond);
		pthread_mutex_unlock(my_mutex);
		return rt_val;		
	}		
};


template<typename T>
struct fifo_channel
{
	struct single_fifo<T>* all_fifos;
	int num_single_fifos;
	pthread_mutex_t* all_fifo_mutexes;
	pthread_cond_t* all_fifo_conds;
	void init(int fanout)
	{
		num_single_fifos = fanout;
		all_fifos = (struct single_fifo<T>*)new struct single_fifo<T> [num_single_fifos];
		// init each of the single fifo
		// each fifo should have its own cond variable
		all_fifo_mutexes = new pthread_mutex_t[num_single_fifos];
		all_fifo_conds = new pthread_cond_t[num_single_fifos];
		pthread_attr_t attr;
		int init_ind;
		for(init_ind = 0; init_ind < num_single_fifos; init_ind++)
		{
			pthread_mutex_init(&(all_fifo_mutexes[init_ind]), NULL);
   			pthread_cond_init (&(all_fifo_conds[init_ind]), NULL);
			all_fifos[init_ind].init(init_ind, &(all_fifo_mutexes[init_ind]), &(all_fifo_conds[init_ind]));
		}
	}
	void write(T val)
	{
		
		int fifo_ind;
		for(fifo_ind=0; fifo_ind<num_single_fifos; fifo_ind++) 
			all_fifos[fifo_ind].write(val);		
	}
	
	T read(int fifo_ind)
	{
		return all_fifos[fifo_ind].read();
	}
};
// we need a struct to give it to the functions
template<typename T>
struct channel_info
{
	struct fifo_channel<T>* channel_ptr;
	int assigned_slot;
	void init(struct fifo_channel<T>* _channel_ptr, int _assigned_slot)
	{
		channel_ptr = _channel_ptr;
		assigned_slot = _assigned_slot;
	}
};

//the push pop primitive
template<typename T>
void push(channel_info<T>* channel, T val)
{
	struct fifo_channel<T>* fifo= channel->channel_ptr;
	fifo->write(val);
}

template<typename T>
void pop(channel_info<T>* channel, T& val)
{
	struct fifo_channel<T>* fifo = channel->channel_ptr;
	int fanout_ind = channel->assigned_slot;
	val = fifo->read(fanout_ind);
}
#endif

