#include "daemon-core.h"
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include "args.h"
#include <stdlib.h>
#include <stdio.h>
#define SOCKET_FD "/tmp/fancy-notify.socket"
#define SIMPLE "simple notification"
#define KILL "kill"
struct simple_notifications{
	char **messages;
	int count;
};
volatile int stop = 0; //turned to 1 when ctr-c is sent to stop daemon gracefully
volatile int stop_count = 0; //number of stop signals received
pthread_mutex_t lock;

int force = 0; //from -f flag
struct simple_notifications simple_notifications;

void handle_signal(int sig);
int start_daemon();
void kill_daemon();
int simple_notification(const char* text);
void *main_thread();

int main(int argc, char* argv[]){
	/* mutex init */
	if (pthread_mutex_init(&lock, NULL)!=0){
		fprintf(stderr,"could not initialise vital mutex\n");
	}
	/* argument processing */
	struct args arguments;
	parse_args(argc,argv,&arguments);
	if (argc < 2){
		fprintf(stderr,"No arguments provided. Refer to man-page for more info\n");
	}
	for (int i = 0;i<arguments.number_single;i++){
		switch(arguments.single[i]){
			case 'd': //daemon mode
				printf("starting daemon\n");
				if (start_daemon() != 0){
					fprintf(stderr,"Could not start daemon.\n");
					return 1;
				}
				break;
			case 'f':  //force
				force = 1;
				break;
			case 's': //simple type
				if (arguments.number_other != 1){
					fprintf(stderr,"not enough args provided\n");
					exit(EXIT_FAILURE);
				}
				simple_notification((const char *)arguments.other[0]);
				break;
			case 'k': //kill daemon
				kill_daemon();
				break;
		}
	}
	/* cleanup */
	return 0;
}
int start_daemon(){
	/* setup signal handler for SIGINT */
	signal(SIGINT, handle_signal);

	/* set up notification buffer */
	simple_notifications.count = 0;
	simple_notifications.messages = malloc(sizeof(char*));
	
	/* set up main thread for processing notifications */
	pthread_t thread_id;
	pthread_create(&thread_id, NULL, main_thread, NULL);
	
	/* set up sockets */
	if (access(SOCKET_FD, F_OK) == 0){
		if (force){
			printf("overwriting previous socket\n");
			remove(SOCKET_FD);
		}else{
			fprintf(stderr,"Socket already exists, daemon may be running elsewhere. use -f to continue\n");
		}
	}
	int server = make_named_socket(SOCKET_FD);
	listen(server,1);
	system("chmod 777 "SOCKET_FD); //allow r+w for all users
	int data_socket;
	char *buffer;

	/* mainloop */
	while (!stop){
		data_socket = accept(server,NULL,NULL);
		buffer = receive_string(data_socket);
		if (strcmp(buffer,SIMPLE) == 0){
			buffer = receive_string(data_socket);
			printf("aquiring simple notification lock\n");

			if (pthread_mutex_lock(&lock)!=0){
				fprintf(stderr,"critical mutex error in main_thread\n");
				exit(EXIT_FAILURE);
			}


			printf("aquired\n");
			simple_notifications.count++;
			simple_notifications.messages = realloc(simple_notifications.messages,sizeof(char*)*(simple_notifications.count));
			printf("allocating %d memory...\n",(int)(strlen(buffer)+1)*(int)sizeof(char));
			simple_notifications.messages[simple_notifications.count-1] = malloc((strlen(buffer)+1)*sizeof(char));
			sprintf(simple_notifications.messages[simple_notifications.count-1],"%s",buffer);
			printf("releasing...\n");
			if (pthread_mutex_unlock(&lock)!=0){
				fprintf(stderr,"critical mutex error in main\n");
				exit(EXIT_FAILURE);
			}
		}else if (strcmp(buffer,KILL) == 0){
			printf("stopping daemon\n");
			stop = 1;
		}
		//clean up ready for next connection
		free(buffer);
		close(data_socket);
	}

	/* cleanup and success */
	close(server);
	remove(SOCKET_FD);
	for (int i = 0;i<simple_notifications.count;i++){
		free(simple_notifications.messages[i]);
	}
	free(simple_notifications.messages);
	pthread_mutex_destroy(&lock);
	return 0;
}
void *main_thread(){
	char *simple_notification_message;
	char *buffer;
	while (!stop){
		if (pthread_mutex_lock(&lock)!=0){//aquire lock
			fprintf(stderr,"critical mutex error in main_thread\n");
			exit(EXIT_FAILURE);
		}
		/* simple notifications */
		if (simple_notifications.count > 0){
			//printf("main_thread: processing simple notification...\n");
			simple_notification_message=simple_notifications.messages[simple_notifications.count-1]; //copy over message so we can release the lock and use a local copy
			simple_notifications.count--;
			printf("creating notification %s\n",simple_notification_message);
			//printf("main_thread: done\n");
			//release lock as early as possible
			if (pthread_mutex_unlock(&lock)!=0){//release lock
				fprintf(stderr,"critical mutex error in main_thread\n");
				exit(EXIT_FAILURE);

			}
			buffer = malloc(strlen(simple_notification_message)+60);
			sprintf(buffer,"/usr/local/bin/simple-notification.py \"%s\"",simple_notification_message);
			system(buffer);
		}
		pthread_mutex_unlock(&lock);//failsafe
		//sleep(2); //simulated delay
	}
	return NULL;
}
int simple_notification(const char* text){
	int data_socket = connect_named_socket(SOCKET_FD);
	send_string(data_socket,SIMPLE);
	send_string(data_socket,text);

	/* cleanup */
	close(data_socket);
	return 0;
}
void handle_signal(int sig){
	signal(sig, SIG_IGN);
	switch (sig){
		case SIGINT:
			if (stop_count<1){
				fprintf(stderr,"SIGINT received\n");
				stop = 1;
				stop_count++;
			}else{
				fprintf(stderr,"Forced stop\n");
				exit(1);
			}
	}
	signal(sig, handle_signal);
}
void kill_daemon(){
	printf("attempting to kill daemon\n");
	int data_socket = connect_named_socket(SOCKET_FD);
	send_string(data_socket,KILL);
	close(data_socket);
}
