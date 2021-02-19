struct connection_data{
	int socketfd;
	int server_socket;
	char* filepath;
	int ** dictionary;
	struct node* tree;
	// int *number_sessions;
	// pthread_mutex_t lock;
	// struct session **sessions_list;
};

struct node{
	uint8_t decompression;
	char *index;
	struct node *left;
	struct node *right;
};

struct server_session{
	int *number_sessions;
	struct session **sessions_list;
	pthread_mutex_t lock;	
};

struct session{
	uint32_t sessionID;
	char *filename;
	uint64_t offset;
	uint64_t length;
	int availability;
};