#ifndef core_h_
#define core_h_
struct buffer{
	int buffer_length; //number of strings in the buffer
	int *lengths; //array of lengths corresponding to strings in buffer (not including null byte)
	char **buffer; //for storing the buffer of commands to be processed by the daemon
};
struct locks{
	int * lock; //an array for locks (0 for unlocked, 1 for locked)
};

void free_buffer(struct buffer *buffer);
void free_locks(struct locks *locks);
#endif
