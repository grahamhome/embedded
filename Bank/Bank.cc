/*
 * Bank Simulation program created by Graham Home and Avinash Srinivasan
 * Completed on 10/13/15 for Real-Time and Embedded Systems Project 4
 *
 * Uses a shared "Bank" variable, a customer-generation thread, a timekeeper thread
 * and three teller threads (number of teller threads is user-configurable) to simulate
 * the daily operations of a bank. Bank variable holds a queue of customers (which are
 * actually Task Control Blocks) which the tellers draw from and a set of metrics variables
 * which the tellers update as they serve customers. At the end of each day, the bank prints
 * its daily metrics before resetting them for the next day.
 */
#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include <sys/neutrino.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define NUM_TELLERS 3 //Number of tellers

#define MIN_CUST_GEN_TIME 60 //Minimum customer generation time (seconds, bank time)
#define MAX_CUST_GEN_TIME 4*60 //Maximum customer generation time (seconds, bank time)

#define MIN_CUST_SERV_TIME 30 //Minimum customer service time (seconds, bank time)
#define MAX_CUST_SERV_TIME 6*60 //Maximum customer service time (seconds, bank time)

#define DAY_START 9*60*60 //Day start time in bank seconds after midnight
#define DAY_END 16*60*60 //Day end time in bank seconds after midnight

#define SEC_IN_DAY 24*60*60 //Number of bank seconds in a day

#define BILLION 1000000000L;

int MAX_CUST = (MIN_CUST_GEN_TIME * (DAY_END-DAY_START)); //Maximum # of customers which can be generated in a day

/*	Represents a bank customer.	*/
struct Customer{
	double startTime; //The time the customer was created & joined the queue
	Customer *next; //The next customer in line
};

/*	Represents a bank. Has variables to store bank metrics and
	methods allowing those metrics to be updated by tellers.
	Also allows tellers to add and remove customers from a FIFO queue.	*/
class Bank {
	double queueAvgTime; //Average time customers spend in queue
	double queueMaxTime; //Maximum time spent by a customer in the queue
	double serviceAvgTime; //Average time customers spend with tellers
	double serviceMaxTime; //Maximum time a teller has spent with a customer
	double waitAvgTime; //Average time tellers spend waiting for customers
	double waitMaxTime; //Maximum time a teller has spent waiting for a customer
	int max; //Maximum number of customers waiting at once
	int current; //Number of customers currently in the queue
	bool day; //True if daytime, False if night.
	Customer *first; //First customer in line
	int count; //Number of customers currently in queue
	bool open; //True if there are still customers in the bank, False otherwise.
	int nCust; //Number of customers served
	pthread_mutexattr_t mutexAttr; //Mutex attribute variable
	public:
		pthread_mutex_t queueMutex; //Mutex for add and remove methods
		pthread_mutex_t queueMetricMutex; //Mutex for method updateQueueTime
		pthread_mutex_t serviceMetricMutex; //Mutex for method updateServiceTime
		pthread_mutex_t waitMetricMutex; //Mutex for method updateWaitTime
		pthread_mutex_t dayMutex; //Mutex for day variable
		pthread_mutex_t openMutex; //Mutex for open variable
		pthread_mutex_t nCustMutex; //Mutex for nCust variable

	//Constructor
public:
	Bank(){
		nCust = 0;
		queueAvgTime = 0;
		queueMaxTime = 0;
		serviceAvgTime = 0;
		serviceMaxTime = 0;
		waitAvgTime = 0;
		waitMaxTime = 0;
		max = 0;
		current = 0;
		first = NULL;
		day = true;
		open = true;
		pthread_mutexattr_init(&mutexAttr); //Initialize mutex attribute variable
		pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_ERRORCHECK); //Set our mutex attribute to error-checking type
		pthread_mutex_init(&queueMutex, &mutexAttr); //Initialize all mutexes to be error-checking
		pthread_mutex_init(&queueMetricMutex, &mutexAttr);
		pthread_mutex_init(&serviceMetricMutex, &mutexAttr);
		pthread_mutex_init(&waitMetricMutex, &mutexAttr);
		pthread_mutex_init(&nCustMutex, &mutexAttr);
		pthread_mutex_init(&dayMutex, &mutexAttr);
		pthread_mutex_init(&openMutex, &mutexAttr);
		pthread_mutex_init(&nCustMutex, &mutexAttr);
	}

	/*Method to reset the bank's metrics*/
	void reset() {
		int result = pthread_mutex_trylock(&nCustMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&nCustMutex);
		}
		nCust = 0;
		result = pthread_mutex_unlock(&nCustMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&nCustMutex);
		}
		result = pthread_mutex_trylock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&queueMetricMutex);
		}
		queueAvgTime = 0;
		queueMaxTime = 0;
		max = 0;
		result = pthread_mutex_unlock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&queueMetricMutex);
		}
		result = pthread_mutex_trylock(&serviceMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&serviceMetricMutex);
		}
		serviceAvgTime = 0;
		serviceMaxTime = 0;
		result = pthread_mutex_unlock(&serviceMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&serviceMetricMutex);
		}
		result = pthread_mutex_trylock(&waitMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&waitMetricMutex);
		}
		waitAvgTime = 0;
		waitMaxTime = 0;
		result = pthread_mutex_unlock(&waitMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&waitMetricMutex);
		}
	}

	/*Method to convert a time in seconds into bank time in microseconds*/
	double bankTime(double time) {
		return (time/600)*1000000;
	}

	/*Method to get the current time in seconds*/
	double getTimeSec(){
		struct timespec start;
		clock_gettime(CLOCK_REALTIME, &start);
		double seconds = start.tv_sec + (double)start.tv_nsec/(double)BILLION;

		return seconds;
	}

	/* Prints bank metrics when all customers have been served */
	void printMetrics() {

		//Print metrics

		std::cout.precision(5); //Set cout precision

		//Print number of customers
		std::cout << getNCust() << " customers served today." << std::endl;

		//Print max queue length
		std::cout << "Maximum queue size was " << getMaxQueueSize() << "." << std::endl;

		//Print avg customer wait time
		std::cout << "Average customer wait time was " << getAvgQueueTime()/60 << " bank minutes." << std::endl;
		//Print max customer wait time
		std::cout << "Maximum customer wait time was " << getMaxQueueTime()/60 << " bank minutes." << std::endl;

		//Print avg customer service time
		std::cout << "Average customer service time was " << getAvgServiceTime()/60 << " bank minutes." << std::endl;
		//Print max customer service time
		std::cout << "Maximum customer service time was " << getMaxServiceTime()/60 << " bank minutes." << std::endl;

		//Print avg teller wait time
		std::cout << "Average teller wait time was " << getAvgWaitTime()/60 << " bank minutes." << std::endl;
		//Print max teller wait time
		std::cout << "Maximum teller wait time was " << getMaxWaitTime()/60 << " bank minutes." << std::endl;
		std::cout << std::endl;
		reset(); //Reset the metrics for the next day

	}

	/*Returns nCust */
	int getNCust() {
		int result = pthread_mutex_trylock(&nCustMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&nCustMutex);
		}
		int retval = nCust;
		result = pthread_mutex_unlock(&nCustMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&nCustMutex);
		}
		return retval;
	}

	/*Increments nCust*/
	void incNCust(){
		int result = pthread_mutex_trylock(&nCustMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&nCustMutex);
		}
		nCust++;
		result = pthread_mutex_unlock(&nCustMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&nCustMutex);
		}
	}

	/* Returns bank open status */
	int getOpen() {
		int result = pthread_mutex_trylock(&openMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&openMutex);
		}
		bool retval = open;
		result = pthread_mutex_unlock(&openMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&openMutex);
		}

		return retval;
	}

	/* Set bank open status */
	void setOpen(bool value) {
		int result = pthread_mutex_trylock(&openMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&openMutex);
		}
		if (open && !value) { printMetrics(); } //Print the metrics if set to closed state from open state
		open = value;
		result = pthread_mutex_unlock(&openMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&openMutex);
		}
	}

	/* Method to set the day variable to the value passed as a parameter */
	void setDay(bool val) {
		int result = pthread_mutex_trylock(&dayMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&dayMutex);
		}
		day = val;
		result = pthread_mutex_unlock(&dayMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&dayMutex);
		}
	}

	/* Method to get the day variable */
	bool getDay() {
		int result = pthread_mutex_trylock(&dayMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&dayMutex);
		}
		bool retval = day;
		result = pthread_mutex_unlock(&dayMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&dayMutex);
		}
		return retval;
	}

	/*Method to get the average customer queue time */
	double getAvgQueueTime() {
		int result = pthread_mutex_trylock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&queueMetricMutex);
		}
		double retval = queueAvgTime;
		result = pthread_mutex_unlock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&queueMetricMutex);
		}
		return retval;
	}

	/*Method to get the max customer queue time */
	double getMaxQueueTime() {
		int result = pthread_mutex_trylock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&queueMetricMutex);
		}
		double retval = queueMaxTime;
		result = pthread_mutex_unlock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&queueMetricMutex);
		}
		return retval;
	}

	/*Method to get the average customer service time */
	double getAvgServiceTime() {
		int result = pthread_mutex_trylock(&serviceMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&serviceMetricMutex);
		}
		double retval = serviceAvgTime;
		result = pthread_mutex_unlock(&serviceMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&serviceMetricMutex);
		}
		return retval;
	}

	/*Method to get the max customer service time */
	double getMaxServiceTime() {
		int result = pthread_mutex_trylock(&serviceMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&serviceMetricMutex);
		}
		double retval = serviceMaxTime;
		result = pthread_mutex_unlock(&serviceMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&serviceMetricMutex);
		}
		return retval;
	}

	/*Method to get the average teller waiting time */
	double getAvgWaitTime() {
		int result = pthread_mutex_trylock(&waitMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&waitMetricMutex);
		}
		double retval = waitAvgTime;
		result = pthread_mutex_unlock(&waitMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&waitMetricMutex);
		}
		return retval;
	}

	/*Method to get the max teller waiting time */
	double getMaxWaitTime() {
		int result = pthread_mutex_trylock(&waitMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&waitMetricMutex);
		}
		double retval = waitMaxTime;
		result = pthread_mutex_unlock(&waitMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&waitMetricMutex);
		}
		return retval;
	}

	/*Method to get the max queue length */
	int getMaxQueueSize() {
		int result = pthread_mutex_trylock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&queueMetricMutex);
		}
		int retval = max;
		result = pthread_mutex_unlock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&queueMetricMutex);
		}
		return retval;
	}

	/*	Method to add a customer to the queue, updating the max queue size if needed. */
	void add(Customer *c) {
		int result = pthread_mutex_trylock(&queueMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&queueMutex);
		}
		result = pthread_mutex_trylock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&queueMetricMutex);
		}
		if (first == NULL) {
			first = c;
		} else {
			Customer last = (*first);
			while (last.next != NULL) {
				last = (*last.next);
			}
			last.next = c;
		}
		current++; //Update number of customers in queue
		if (max < current) { max = current; } //Update max queue length if needed

		result = pthread_mutex_unlock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&queueMetricMutex);
		}
		result = pthread_mutex_unlock(&queueMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&queueMutex);
		}
	}

	/*	Method to remove a customer from the queue. Returns NULL if queue is empty.	*/
	Customer *remove() {
		Customer *out;
		int result = pthread_mutex_trylock(&queueMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&queueMutex);
		}
		result = pthread_mutex_trylock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&queueMetricMutex);
		}
		if (first == NULL) {
			out = NULL;
		} else {
			out = first;
			first = (*out).next;
			current--;
		}
		result = pthread_mutex_unlock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&queueMetricMutex);
		}
		result = pthread_mutex_unlock(&queueMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&queueMutex);
		}
		return out;
	}

	/*	Method to update the customer waiting time metrics.	*/
	void updateQueueTime(double time) {
		int result = pthread_mutex_trylock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&queueMetricMutex);
		}

		queueAvgTime = (queueAvgTime!=0) ? ((queueAvgTime+time)/2) : (queueAvgTime+time);
		if (queueMaxTime < time) { queueMaxTime = time; }

		result = pthread_mutex_unlock(&queueMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&queueMetricMutex);
		}
	}

	/*	Method to update the service time metrics.	*/
	void updateServiceTime(double time) {
		int result = pthread_mutex_trylock(&serviceMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&serviceMetricMutex);
		}

		serviceAvgTime = (serviceAvgTime!=0) ? ((serviceAvgTime+time)/2): (serviceAvgTime+time);
		if (serviceMaxTime < time) { serviceMaxTime = time; }

		result = pthread_mutex_unlock(&serviceMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&serviceMetricMutex);
		}
	}

	/*	Method to update the teller waiting time metrics. */
	void updateWaitTime(double time) {
		int result = pthread_mutex_trylock(&waitMetricMutex);
		while (result != 0) {
			result = pthread_mutex_trylock(&waitMetricMutex);
		}

		waitAvgTime = (waitAvgTime!=0) ? ((waitAvgTime+time)/2) : (waitAvgTime+time);
		if (waitMaxTime < time) { waitMaxTime = time; }

		result = pthread_mutex_unlock(&waitMetricMutex);
		while (result != 0) {
			result = pthread_mutex_unlock(&waitMetricMutex);
		}
	}
};

//Method run by teller thread
void *teller(void *bank) {

	double idleTime = (*(Bank*)bank).getTimeSec();
	bool idle = true;
	while(1) {

		bool day = (*(Bank*)bank).getDay();
		bool open = (*(Bank*)bank).getOpen();

		if (day || open) { //If it is day OR it is night and customers remain

			//Get first customer in line
			usleep(1000); //We have to do this so customer generation thread has time to access the queue
			day = (*(Bank*)bank).getDay();
			open = (*(Bank*)bank).getOpen();
			Customer *c = (*(Bank*)bank).remove();
			if (c == NULL) {
				if (!day) {
					(*(Bank*)bank).setOpen(false); //It is night and there are no customers - close the bank.
				}
				else if (!idle) {
					//Teller is now idle
					idle = true;
					//Records time at which it went idle
					idleTime = (*(Bank*)bank).getTimeSec();
				}
			} else {

				if (idle) { //If the teller has been waiting for a customer

					//Teller is no longer idle
					idle = false;

					//Get current time
					double now = (*(Bank*)bank).getTimeSec();

					//Calculate idle time in bank time
					double idleFor = (now-idleTime)*600;

					//Update teller idle time metric
					(*(Bank*)bank).updateWaitTime(idleFor);
				}

				//Update queue time metrics (Get current time and customer start time)
				//Get current bank time in seconds
				double now = (*(Bank*)bank).getTimeSec();

				//Get time customer has been waiting in bank time
				double custWaitTime = (now - (*c).startTime)*600;

				//Update customer wait time metric
				(*(Bank*)bank).updateQueueTime(custWaitTime);

				//Generate service time
				double waitTime = (double)(rand() % (MAX_CUST_SERV_TIME-(MIN_CUST_SERV_TIME+1))) + MIN_CUST_SERV_TIME+1;

				double wait = (*(Bank*)bank).bankTime(waitTime); //Convert to bank time in microseconds

				//Update service metric
				(*(Bank*)bank).updateServiceTime(waitTime);

				//Serve customer
				usleep((int)wait);

				//Update nCust
				(*(Bank*)bank).incNCust();

				//Delete customer to prevent memory leak - this line currently causes problems if included
				//delete c;
			}
		} else { //It's night with no customers left

			if (idle) { //If the teller has been waiting for a customer

				//Teller is no longer idle
				idle = false;

				//Get current bank time in seconds
				double now = (*(Bank*)bank).getTimeSec();

				//Calculate idle time in bank time
				double idleFor = (now-idleTime)*600;

				//Update teller idle time metric
				(*(Bank*)bank).updateWaitTime(idleFor);

				//Reset wait time
				idleTime = 0;
			}
		}
	}
}

//Method run by customer generation thread
void *genCust(void *bank) {

	while(1) {

		bool day = (*(Bank*)bank).getDay();

		if (day) {

			Customer c = Customer();
			c.next = NULL;

			//Get customer start time
			double startTime = (*(Bank*)bank).getTimeSec();

			//Store as customer start time
			c.startTime = startTime;

			//Add customer to bank queue
			(*(Bank*)bank).add(&c);

			float waitTime = (double)(rand() % MAX_CUST_GEN_TIME-(MIN_CUST_GEN_TIME+1)) + (MIN_CUST_GEN_TIME+1); //Generate a random number for the thread to sleep before making another customer.
			waitTime = (*(Bank*)bank).bankTime(waitTime); //Convert to bank time in microseconds
			usleep((int)waitTime);
		}
	}
}

//Method run by timekeeping thread
void *timekeeper(void *bank) {
	while(1) {

		//Start the day
		(*(Bank*)bank).setDay(true);
		(*(Bank*)bank).setOpen(true);

		//Sleep until night
		usleep((*(Bank*)bank).bankTime(DAY_END-DAY_START));

		//End the day
		(*(Bank*)bank).setDay(false);
		//Don't set 'open' to false - there might still be customers in the bank. Only tellers can close the bank.

		//Sleep until night
		usleep((*(Bank*)bank).bankTime((SEC_IN_DAY)-(DAY_END-DAY_START)));
	}
}


/*Main method to initialize variables & timer, generate customers and report metrics.*/
int main() {

	Bank b = Bank(); //Create bank

	//Create timekeeper thread
	pthread_create(NULL, NULL, timekeeper, (void *) &b);

	//Create customer generation thread
	pthread_create(NULL, NULL, genCust, (void *) &b);

	//Create teller threads
	for (int i=0; i<NUM_TELLERS; i++) {
		pthread_create(NULL, NULL, teller, (void *) &b);
	}

	while(1);
}
