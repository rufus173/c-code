#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include "args.h"
#include "daemon-toolkit.h"
#define SOCKET_FD "/tmp/mail-manager.socket"
#define MAIL_LOCATION "/var/spool/mail/system"
#define ERROR "------ ERROR ------\n"

struct mail{
	char **header;
	int count;
	char **body;
};

struct mail mail;
static volatile int stop = 0;
pthread_mutex_t lock;
void *daemon_view_mail();
void daemon_receive_mail(int socket);
int client_send_mail(struct args args);
void client_view_mail();
int start_daemon();
int dump_mail();
int client_delete_mail(int mail_index);
void daemon_delete_mail(int data_socket);
int load_mail();
void kill_daemon();

int main(int argc, char *argv[]){
	int result = 0;
	char *err;
	struct args args;
	parse_args(argc,argv,&args);
	for (int i = 0;i<args.number_single;i++){
		if (args.single[i] == 'd'){
			result = start_daemon();
			break;
		}
		if (args.single[i] == 'k'){
			kill_daemon();
			break;
		}
		if (args.single[i] == 'r'){// deleting mail at specified index of the array.
			if (args.number_other < 1){
				fprintf(stderr,"Please specify the index of the mail to delete\n");
				return 1;
			}
			int index = strtol(args.other[0],&err,10);
			client_delete_mail(index);
			break;
		}
	}
	if (argc < 2){//no args passed
		client_view_mail();
	}else if(args.number_single == 0){
		result = client_send_mail(args);
	}

	free_args(&args);
	return result;
}
int load_mail(){
	printf("attempting to load mail...\n");
	int i;
	char buffer;
	char mail_file_path[280];

	/* get a list of the mail files */
	FILE *mail_file;
	DIR *directory;
	struct dirent *dir;
	directory = opendir(MAIL_LOCATION);
	if (directory){
		while ((dir = readdir(directory)) != NULL){
			if (dir->d_type == DT_REG){//make sure it is a file
				//printf("loading mail file %s\n", dir->d_name);
				/* loading mail into memory */
				if (pthread_mutex_lock(&lock) != 0){
					fprintf(stderr,ERROR"mutex locking error in load_mail()\n"ERROR);
					return 1;
				}
				sprintf(mail_file_path,"%s/%s",MAIL_LOCATION,dir->d_name);
				mail_file = fopen(mail_file_path,"r");
				if (mail_file == NULL){
					fprintf(stderr,ERROR"could not open file\n"ERROR);
					return 1;
				}
				//printf("reallocating header and body...\n");
				mail.header = realloc(mail.header,sizeof(char*)*(mail.count+1));
				mail.body = realloc(mail.body,sizeof(char*)*(mail.count+1));
				//printf("allocating header and body %d..\n",mail.count);
				mail.header[mail.count] = malloc(sizeof(char));
				mail.body[mail.count] = malloc(sizeof(char));
				for (i = 0;;i++){
					//printf("reallocating mail header %d...\n",mail.count);
					mail.header[mail.count] = realloc(mail.header[mail.count],sizeof(char)*(i+1));
					//printf("getting character...\n");
					buffer = fgetc(mail_file);
					//printf("got char of %c\n",buffer);
					if (feof(mail_file)){
						fprintf(stderr,ERROR"expected newline but got end of file. possible mail corruption\n"ERROR);
						return 1;
					}
					if (buffer != '\n'){
						mail.header[mail.count][i] = buffer;
					}else{
						mail.header[mail.count][i] = '\0';
						break;
					}
				}
				//printf("allocating body %d...\n",mail.count);
				mail.body[mail.count] = malloc(sizeof(char));
				for (i=0;;i++){
					buffer = fgetc(mail_file);
					//printf("got %c\n",buffer);
					if (feof(mail_file)){
						//printf("reached end of file\n");
						mail.body[mail.count][i] = '\0';
						break;
					}
					//printf("reallocating mail body...\n");
					mail.body[mail.count] = realloc(mail.body[mail.count],sizeof(char)*(i+2));
					mail.body[mail.count][i] = buffer;
				}
				//printf("closing file...\n");
				fclose(mail_file);
				//printf("closed\n");
				mail.count++;
				//printf("mail count now %d\n",mail.count);
				//printf("	[header]\n	%s\n	[body]\n	%s	[end]\n",mail.header[mail.count-1],mail.body[mail.count-1]);
				printf("loaded mail %d\n",mail.count-1);
				if (pthread_mutex_unlock(&lock) != 0){
					fprintf(stderr,ERROR"mutex unlocking error in load_mail()\n"ERROR);
					return 1;
				}
			}
		}
		closedir(directory);
	}
	printf("mail loaded\n");
	return 0;
}

int dump_mail(){
	printf("dumping mail...\n");
	/* check mail directory exists, and if not , create it */
	struct stat sb;
	if (! (stat(MAIL_LOCATION, &sb) == 0 && S_ISDIR(sb.st_mode))){
		if (mkdir(MAIL_LOCATION, 1777) != 0){
			fprintf(stderr,ERROR"could not create folder for mail storage\n"ERROR);
			return 1;
		}
	}
	/* dump each mail into its respective file */
	char filepath[180];
	FILE* mail_file;
	if (pthread_mutex_lock(&lock) != 0){
		fprintf(stderr,ERROR"could not lock mutex\n"ERROR);
		exit(EXIT_FAILURE);
	}
	for (int i=0;i<mail.count;i++){
		/* name mails after their index */
		sprintf(filepath,"%s/%d",MAIL_LOCATION,i);
		mail_file = fopen((const char *)filepath,"w");
		if (mail_file == NULL){
			fprintf(stderr,ERROR"could not open file %s. May be due to lack of permition\n"ERROR,filepath);
			pthread_mutex_unlock(&lock);
			return 1;
		}
		/* header and body are seperated by newline character */
		fprintf(mail_file,"%s\n%s",mail.header[i],mail.body[i]);
		//cleanup after each file
		fclose(mail_file);
	}
	if (pthread_mutex_unlock(&lock) != 0){
		fprintf(stderr,"could not unlock mutex\n");
		exit(EXIT_FAILURE);
	}
	printf("mail dumped\n");
	return 0;
}
int start_daemon(){
	int close_socket = 1;
	mail.count = 0;
	mail.header = malloc(sizeof(char*));
	mail.body = malloc(sizeof(char*));
	pthread_mutex_init(&lock,NULL);
	pthread_t thread_id;
	
	/* load any mail stored in the mail files */
	if (load_mail() != 0){
		fprintf(stderr,ERROR"could not load mail\n"ERROR);
		return 1;
	}

	int data_socket;
	int result = 0;
	char *buffer;
	if (access(SOCKET_FD,F_OK) == 0){
		fprintf(stderr,"Warning: found existing socket. attempting to delete\n");
		if (remove(SOCKET_FD) == 0){
			fprintf(stderr,"Warning: old socket deleted\n");
		}else{
			fprintf(stderr,ERROR"could not delete old socket\n"ERROR);
		}
	}else{
		printf("Socket not already found, continuing\n");
	}
	int server = make_named_socket(SOCKET_FD);
	if (server < 0){
		fprintf(stderr,ERROR"could not create the server socket\n"ERROR);
	}
	listen(server,10);
	printf("Awaiting connections");
	while (!stop){
		close_socket = 1;
		data_socket = accept(server,NULL,NULL);
		receive_string(data_socket,&buffer);
		if (strcmp(buffer,"view") == 0){
			free(buffer);
			pthread_create(&thread_id,NULL,daemon_view_mail,(void *)(long)data_socket); //break of a seperate thread to deal with that
			close_socket = 0;
		}else if (strcmp(buffer,"send") == 0){
			daemon_receive_mail(data_socket);
			free(buffer);
		}else if (strcmp(buffer,"kill") == 0){
			stop = 1;
		}else if (strcmp(buffer,"delete") == 0){
			daemon_delete_mail(data_socket);
		}
		/* clean up connection */
		if (close_socket){
			close(data_socket);
		}
	}

	/* dump any unread mails into a file */
	result = dump_mail();

	/* cleanup */
	pthread_mutex_lock(&lock);//extra safety if the threads havent finnished yet
	printf("cleaning up %d mails...\n",mail.count);
	for (int i=0;i<mail.count;i++){
		//printf("freeing mail header %d\n",i);
		free(mail.header[i]);
		//printf("freeing mail body %d\n",i);
		free(mail.body[i]);
	}
	//printf("freeing structs...\n");
	free(mail.header);
	free(mail.body);
	free(buffer); //allocated by receive_string()
	printf("closing socket...\n");
	pthread_mutex_unlock(&lock);
	close_named_socket(server,SOCKET_FD);
	printf("cleanup done\n");
	pthread_mutex_destroy(&lock);
	return result;
}
void *daemon_view_mail(void *socket){
	int data_socket = (long)socket;
	printf("initiating viewing mail with the client on socket %d\n",data_socket);
	char *buffer;
	int selected;
	if (pthread_mutex_lock(&lock)<0){
		fprintf(stderr,"start_daemon: mutex failed to lock\n");
		exit(EXIT_FAILURE);
	}
	printf("sending mail count of %d...\n",mail.count);
	send_int(data_socket,mail.count);// start communication by specifying the number of mails
	printf("done\n");
	if (pthread_mutex_unlock(&lock)<0){
		fprintf(stderr,"start_daemon: mutex failed to unlock\n");
		exit(EXIT_FAILURE);
	}
	while (!stop){
		receive_string(data_socket,&buffer);//check if more mails are wanted or finnished
		if (strcmp(buffer,"done") == 0){
			printf("communication complete\n");
			break;
		}
		if (mail.count<1){//safety to prevent seg fault
			fprintf(stderr,ERROR"Client wants more mail, but none to give!"ERROR);
			break;
		}
		selected = receive_int(data_socket);
		/* guaranteed atomicity of all operations */
		if (pthread_mutex_lock(&lock)<0){
			fprintf(stderr,"start_daemon: mutex failed to lock\n");
			exit(EXIT_FAILURE);
		}
		send_string(data_socket,(const char *)mail.header[selected]);
		send_string(data_socket,(const char *)mail.body[selected]);
		if (pthread_mutex_unlock(&lock)<0){
			fprintf(stderr,"start_daemon: mutex failed to unlock\n");
			exit(EXIT_FAILURE);
		}
		/* allow other threads to use hardware after this point */
	}
	free(buffer);
	close(data_socket);
	printf("Client mail communication complete. Exiting thread\n");
	return NULL;
}
void client_view_mail(){
	char *header;
	char *body;
	int mail_count;
	int data_socket = connect_named_socket(SOCKET_FD);
	send_string(data_socket,"view");//tell daemon we want to view mails
	mail_count = receive_int(data_socket);//get number of mails
	for (int i=0;i<mail_count;i++){
		send_string(data_socket,"more");
		send_int(data_socket,i);//request each mail in order

		/* get mail contents */
		receive_string(data_socket,&header);
		receive_string(data_socket,&body);
		
		/* display mail */
		printf("+---+ Mail %d +----+\n",i);
		printf("%s\n",header);
		printf("+---+ Contents +---+\n");
		printf("%s",body);
		printf("+------------------+\n");

		/* cleanup after displaying each mail */
		free(header);
		free(body);
	}
	send_string(data_socket,"done");//signal that we are done getting mails
	close(data_socket);
}
void daemon_receive_mail(int socket){
	char *header;
	char *body;
	receive_string(socket,&header);
	printf("got header of %s\n",header);
	receive_string(socket,&body);
	printf("got body of %s\n",body);
	/* guaranteed atomicity of all operations */
	if (pthread_mutex_lock(&lock)<0){
		fprintf(stderr,"start_daemon: mutex failed to lock\n");
		exit(EXIT_FAILURE);
	}
	printf("incrementing mail count...\n");
	mail.count++;
	printf("copying header...\n");
	mail.header = realloc(mail.header,sizeof(char*)*(mail.count));
	mail.header[mail.count-1] = malloc(sizeof(char)*(strlen(header)+1));
	strcpy(mail.header[mail.count-1],header);
	printf("copying body...\n");
	mail.body = realloc(mail.body,sizeof(char*)*(mail.count));
	mail.body[mail.count-1] = malloc(sizeof(char)*(strlen(body)+1));
	strcpy(mail.body[mail.count-1],body);
	if (pthread_mutex_unlock(&lock)<0){
		fprintf(stderr,"start_daemon: mutex failed to unlock\n");
		exit(EXIT_FAILURE);
	}
	/* allow other threads to use hardware after this point */
	printf("cleaning up...\n");
	free(header);//cleanup
	free(body);
	printf("successfully got new mail!\n");
}
int client_send_mail(struct args args){
	if (args.number_other != 2){
		fprintf(stderr,"Expected 2 arguments of [header] and [body], but got %d\n",args.number_other);
		return 1;
	}
	printf("collecting mail from command line args...\n");
	int socket;
	char *header;
	char *body;
	printf("allocating space...\n");
	header = malloc(sizeof(char)*(strlen(args.other[0])+1));
	body = malloc(sizeof(char)*(strlen(args.other[1])+2));
	printf("copying args...\n");
	strcpy(header,args.other[0]);
	sprintf(body,"%s\n",args.other[1]);
	printf("connecting to daemon...\n");
	socket = connect_named_socket(SOCKET_FD);
	printf("sending data...\n");
	send_string(socket,"send");
	send_string(socket,(const char *)header);
	send_string(socket,(const char *)body);
	printf("freeing allocated memory...\n");
	free(header);
	free(body);
	printf("mail sent!\n");
	return 0;
}
int client_delete_mail(int mail_index){//safety checks completed back in main function
	printf("attempting to delete mail...\n");
	int status = 0;
	/* open comms with daemon */
	int data_socket = connect_named_socket(SOCKET_FD);
	send_string(data_socket,"delete");//tell the daemon that we want to delete mail
	send_int(data_socket,mail_index); //comunicate which mail should be deleted.
	status = receive_int(data_socket);
	if (status != 0){
		fprintf(stderr,ERROR"Could not delete mail of specified index. Are you sure it exists?\n"ERROR);

	}else{
		printf("Successful deletion!\n");
	}
	/* cleanup */
	close(data_socket);
	return status;
}
void daemon_delete_mail(int data_socket){
	char *error;
	int result = 0;
	int status = 0;
	char mail_file_path[180];
	char last_mail_file_path[180];
	int mail_index = receive_int(data_socket);
	printf("attempting to delete mail of index %d\n",mail_index);
	if (pthread_mutex_lock(&lock) != 0){
		fprintf(stderr,ERROR"Critical mutex locking error in daemon_delete_mail\n"ERROR);
	}
	result = snprintf(last_mail_file_path,180,"%s/%d",MAIL_LOCATION,mail.count-1);
	if (result >= 180){
		fprintf(stderr,ERROR"Could not calculate the last mail's file path, due to potential buffer overflow.\n"ERROR);
		status = 1;
	}
	/* validation checks */
	if ( ! ((mail_index >= 0) && (mail_index < mail.count))){// check if it within range
		printf("invalaid mail index for deletion given\n");
		send_int(data_socket,1);
		if (pthread_mutex_unlock(&lock) != 0){
			fprintf(stderr,ERROR"Critical mutex unlocking error in daemo n_delete_mail\n"ERROR);
		}
		return;
	}
	/* remove mail from mail struct */
	// we move the last element of the array to the position being deleted and shrink array by 1
	mail.header[mail_index] = mail.header[mail.count-1];
	mail.header = realloc(mail.header,sizeof(char*)*(mail.count-1));
	mail.body[mail_index] = mail.body[mail.count-1];
	mail.body = realloc(mail.body,sizeof(char*)*(mail.count-1));
	mail.count--;
	/* delete mail file if it exists */
	printf("searching for mail file with corresponding index\n");
	DIR *directory;
	struct dirent *dir;
	directory = opendir(MAIL_LOCATION);
	if (directory){
		while ((dir = readdir(directory)) != NULL){
			if (dir->d_type == DT_REG){//make sure it is a file
				/* rename last inded name file to index being deleted (same method as deleting mail entries within the struct) */
				if (((int)strtol(dir->d_name,&error,10) == mail_index)){
					result = snprintf(mail_file_path,180,"%s/%s",MAIL_LOCATION,dir->d_name);
					if (result >= 180){//size of mail_file_path
						fprintf(stderr,ERROR"File path was to long!\n"ERROR);
						status = 1;	
						break;
					}
					printf("checking for existence of %s and %s...\n",mail_file_path,last_mail_file_path);
					printf("checking it isn't the last mail...%d/%d \n",(int)strtol(dir->d_name,&error,10),mail.count);
					if (((access(mail_file_path,F_OK) == 0) && (((int)strtol(dir->d_name,&error,10) != mail.count)) && (access(last_mail_file_path,F_OK) == 0))){//the mail exists and it isnt the last element
						//move last mail element to the deleting mail index
						printf("attempting to remove by renaming...\n");
						result = rename((const char *)last_mail_file_path,(const char *)mail_file_path);//rename file (final step)
						if (result != 0){
							fprintf(stderr,ERROR"Could not rename file.\n"ERROR);
						}

					}else{
						//just delete the mail file
						printf("found mail file match %s, deleting...\n",dir->d_name);
						result = remove(mail_file_path);
						if (result != 0){
							fprintf(stderr,ERROR"Could not remove file %s. May be due to lack of permitions\n"ERROR,mail_file_path);
							status = 1;
							break;
						}
					}
					break;
				}
			}
		}
	}
	closedir(directory);
	printf("done\n");
	//do stuff with files
	if (pthread_mutex_unlock(&lock) != 0){
		fprintf(stderr,ERROR"Critical mutex unlocking error in daemo n_delete_mail\n"ERROR);
	}
	send_int(data_socket,status);//signal success if status is 0
	if (status == 0){
		printf("successful deletion of mail\n");
	}else{
		fprintf(stderr,ERROR"Could not fully remove mail of index %d.\n"ERROR,mail_index);
	}
}

void kill_daemon(){
	printf("killing daemon...\n");
	int socket = connect_named_socket(SOCKET_FD);
	send_string(socket,"kill");
	printf("kill signal sent\n");
}
