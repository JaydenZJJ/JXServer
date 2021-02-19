#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <stdint.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <dirent.h>
#include "structs.h"

#define SetBit(A,k)     ( A[(k/32)] |= (1 << (k%32)) )
#define TestBit(A,k)    ( A[(k/32)] & (1 << (k%32)) )

static struct server_session *global_session;

void set_bit(int *bit_array, int k){
	bit_array[k/32] |= 1<< (k%32);
}

int test_bit(int *bit_array, int k){
	if (bit_array[k/32]&(1<<(k%32))){
		return 1;
	}
	else{
		return 0;
	}
}

void data_free(struct connection_data *data){
	for (int x = 0; x<256;x++){
		free(data->dictionary[x]);
	}
	free(data->dictionary);
	for (int x = 0; x<*(global_session->number_sessions);x++){
		free(global_session->sessions_list[x]->filename);
		free(global_session->sessions_list[x]);
	}
	free(global_session->sessions_list);
	free(data);
}

int *bit_array_construction(){
	FILE *fp = fopen("compression.dict","rb");
	fseek(fp, 0, SEEK_END);
	// determine the length of file content
	long file_length = ftell(fp);
	// reset the cursor to start of file
	rewind(fp);
	// allocate memory to store the content
	uint8_t *result = malloc((file_length)*sizeof(uint8_t));
	// use a loop to read all the bytes into config_read
	for (int i = 0; i<file_length; i++){
		fread(result+i,1,1,fp);
	}
	fclose(fp);


	// now we have the dict in bytes in an array

	int *bit_array = calloc(sizeof(int),247);
	for (int x = 0; x<986; x++){
		int index = x*8;
		int count = 0;
		// for each byte
		for (int offset = 7; offset>=0; offset--){
			// for each bit
			unsigned char bit = (result[x]>> offset) & 0x01;
			// see if bit is 1, and map to bit array
			if (bit==1){
				set_bit(bit_array,index+count);
			}
			count += 1;
		}
	}
	free(result);
	return bit_array;

} 

int ** make_dictionary(int *bit_array){
	int ** dictionary = calloc(sizeof(int*),256);
	int dict_count = 0;
	int offset = 0;
	while (dict_count<256){
		uint8_t compressed_bit_size = 0;
		for (int x = 0; x<8; x++){
			compressed_bit_size<<=1;
			compressed_bit_size += test_bit(bit_array,x+offset);
		}
		// printf("DICT COUNT %d: %d\n",dict_count,compressed_bit_size);
		offset+=8;
		int *compressed_bits = malloc(sizeof(int)*(compressed_bit_size+1));
		for (int x = 0; x<compressed_bit_size; x++){
			compressed_bits[x+1] = test_bit(bit_array,x+offset);
		}
		compressed_bits[0] = compressed_bit_size;
		offset+=compressed_bit_size;
		dictionary[dict_count] = compressed_bits;
		dict_count+=1;
	}
	return dictionary;
}

uint8_t * compress_payload(uint8_t *payload,int **dictionary, uint64_t payload_length, uint64_t *compress_length){
	int size = 0;
	for (int x = 0; x<payload_length; x++){		
		size = size+dictionary[payload[x]][0];
	}
	int length;
	if (size%8==0){
		length = size/8;
	}else{
		length = ((size/8)+1);
	}
	*compress_length = length+1; 
	size = (length) * 8;
	int *compression = malloc(sizeof(int)*size);
	int counter = 0;
	int padding = 0;
	for (int x = 0; x<payload_length; x++){		
		for (int y=0; y<dictionary[payload[x]][0];y++){
			compression[counter++] = dictionary[payload[x]][y+1];
		}
	}
	while (counter<size){
		compression[counter] = 0;
		counter++;
		padding++;
	}
	int offset = 0;
	uint8_t *compressed_byte = malloc(length+1);
	for (int x = 0; x<length; x++){
		uint8_t byte = 0;
		for (int y = 0; y<8; y++){
			byte <<=1;
			byte += compression[offset+y];
		}
		compressed_byte[x] = byte;
		offset+=8;
	}
	compressed_byte[length] = (uint8_t)padding;
	free(compression);
	return compressed_byte;
	// return compression;
}

uint8_t *read_filenames(char *fp, int* number, int* num_bytes){
	DIR *dir;
	struct dirent *ent;
	int number_files = 0;
	if ((dir = opendir (fp)) != NULL) {
	/* print all the files and directories within directory */
		while ((ent = readdir (dir)) != NULL) {
			if (!strcmp(ent->d_name,".") || !strcmp(ent->d_name, "..") || ent->d_type==DT_DIR){
				
			}
			else{
				number_files ++;
			}
		}
		closedir (dir);
	} else {
		/* could not open directory */
		printf("FILE NOT FOUND");
		return NULL;
	}
	unsigned char **names = malloc(number_files*sizeof(unsigned char *));
	int count = 0;
	int number_bytes = 0;
	if ((dir = opendir (fp)) != NULL) {
	/* print all the files and directories within directory */
		while ((ent = readdir (dir)) != NULL) {
			if (!strcmp(ent->d_name,".") || !strcmp(ent->d_name, "..") || ent->d_type==DT_DIR){
				
			}
			else{
				// printf("%s\n",ent->d_name);
				names[count] = malloc(strlen(ent->d_name)*2+1);
				int i = 0;
				int loop = 0;
				while(ent->d_name[loop] != '\0'){
					sprintf((char*)(names[count]+i),"%02X", ent->d_name[loop]);
					loop+=1;
					i+=2;				
				}
				//insert NULL at the end of the output string
				names[count][i++] = '\0';
				number_bytes+=strlen((const char *)names[count])/2+1;
				count ++;
				
			}
		}
		closedir (dir);
	} else {
		/* could not open directory */
		return NULL;
	}
	uint8_t *hex_names = malloc(number_bytes);
	count = 0;
	for (int x = 0; x<number_files; x++){
		char *pos = (char *)names[x];
		int y = 0;
		while (pos[y]!='\0'){
			char temp[3];
			temp[0] = pos[y++];
			temp[1] = pos[y++];
			temp[2] = '\0';
			hex_names[count++] = (uint8_t)strtol(temp,NULL,16);	
		}
		hex_names[count++] = 0x00;
	}

	// for (int x = 0; x<number_files; x++){
	// 	printf("%s\n",names[x]);
	// }


	for (int x = 0; x<number_files; x++){
		free(names[x]);
	}
	free(names);

	*number = number_files;
	*num_bytes = number_bytes;

	return hex_names;
}

uint8_t * read_config(FILE *fp, int *config_length){
	// place the cursor in the file to end of file
	fseek(fp, 0, SEEK_END);
	// determine the length of file content
	long file_length = ftell(fp);
	// reset the cursor to start of file
	rewind(fp);
	// allocate memory to store the content
	uint8_t *result = malloc((file_length)*sizeof(uint8_t));
	// use a loop to read all the bytes into config_read
	for (int i = 0; i<file_length; i++){
		fread(result+i,1,1,fp);
	}
	fclose(fp);
	*config_length = (int)file_length;
	return result;
}

void send_err(void * arg, int error_type){
	struct connection_data *data = (struct connection_data*)arg;
	// we send a message with 0xf0 indicating the type and 0 for the rest to indicate empty payload
	if (error_type==2){
		// empty directory
		uint8_t buffer[10] = {0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
		// Send it using exactly the same syscalls as for other file descriptors
		for (int x = 0; x<10; x++){
			printf("%x ",buffer[x]);
		}
		printf("\n");
		write(data->socketfd,buffer,10);
	}
	else{
		// default error message
		uint8_t buffer[9] = {0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
		// Send it using exactly the same syscalls as for other file descriptors
		write(data->socketfd,buffer,9);
		// we need to close the connection
		close(data->socketfd);
	}
	
}

uint8_t * retrieve_content(uint64_t offset, uint64_t length, char *filename){
	FILE *fp = fopen(filename,"r");
	fseek(fp,offset,SEEK_SET);
	uint8_t *content = malloc(length);
	fread(content,1,length,fp);
	return content;
}

uint8_t *decompress(struct node *my_tree, uint8_t *bits, uint64_t *decompressed_size, uint64_t *bits_length){
	uint8_t *decompressed_payload = malloc(1);
	int counter = 0;
	struct node *ptr = my_tree;  
	for (int x = 0; x<*bits_length; x++){
		// turn right
		if (bits[x]){
			if (ptr->right != NULL){
				ptr = ptr->right;
			}
			else{
				counter++;
				decompressed_payload = realloc(decompressed_payload,counter);
				decompressed_payload[counter-1] = ptr->decompression;
				ptr = my_tree;
				x--;
			}
		}
		else{
			if (ptr->left != NULL){
				ptr = ptr->left;
			}
			else{
				counter++;
				decompressed_payload = realloc(decompressed_payload,counter);
				decompressed_payload[counter-1] = ptr->decompression;
				ptr = my_tree;
				x--;
				
			}
		}
	}
	counter ++;
	decompressed_payload = realloc(decompressed_payload,counter);
	decompressed_payload[counter-1] = ptr->decompression;

	*decompressed_size = counter;
	return decompressed_payload;
}

uint8_t *bits_extractions(uint8_t *compressed, uint64_t *compressed_length, uint64_t* bits_length){
	int padding = compressed[(*compressed_length)-1];
	*bits_length = ((*compressed_length)-1)*8;
	uint8_t *padded_bits = malloc(*bits_length);
	int bits_offset = 0;
	for (int x = 0; x<*compressed_length-1; x++){
		uint8_t decompress_byte = compressed[x];
		for (int i = 0; i < 8; i++) {
			// Mask each bit in the byte and store it
			padded_bits[7-i+bits_offset] = (decompress_byte >> i) & 1;
		}
		bits_offset+=8;
	}
	//00101100
	//00101100
	// uint8_t *bits = realloc(padded_bits,(((*compressed_length)-1)*8)-padding);
	*bits_length = *bits_length-padding;
	return padded_bits;
}

struct node *plant_tree(int **dictionary){
	struct node *root = malloc(sizeof(struct node));
	root->left = NULL;
	root->right = NULL;
	root->index = "";
	root->decompression = -1;
	int counter = 0;
	for (int x = 0; x<256; x++){
		struct node *ptr = root;
		int compressed_length = dictionary[x][0];
		for (int y = 0; y<compressed_length; y++){
			// if 1
			if (dictionary[x][y+1]){
				if (ptr->right==NULL){
					if (y+1==compressed_length){
						ptr->right = malloc(sizeof(struct node));
						counter++;
						ptr->right->left = NULL;
						ptr->right->right = NULL;
						char *index = malloc(sizeof(char)*strlen(ptr->index)+2);
						strncpy(index,ptr->index,strlen(ptr->index));
						index[strlen(ptr->index)] = '1';
						index[strlen(ptr->index)+1] = '\0';
						ptr->right->index=index;
						ptr->right->decompression = x;
						break;
					}
					ptr->right = malloc(sizeof(struct node));
					counter++;
					ptr->right->left = NULL;
					ptr->right->right = NULL;
					char *index = malloc(sizeof(char)*strlen(ptr->index)+2);
					strncpy(index,ptr->index,strlen(ptr->index));
					index[strlen(ptr->index)] = '1';
					index[strlen(ptr->index)+1] = '\0';
					ptr->right->index=index;
					ptr->decompression = -1;
					
					ptr = ptr->right;
				}
				else{
					ptr = ptr->right;
				}
			}
			else{
				if (ptr->left==NULL){
					if (y+1==compressed_length){
						ptr->left = malloc(sizeof(struct node));
						counter++;
						ptr->left->left = NULL;
						ptr->left->right = NULL;
						char *index = malloc(sizeof(char)*strlen(ptr->index)+2);
						strncpy(index,ptr->index,strlen(ptr->index));
						index[strlen(ptr->index)] = '0';
						index[strlen(ptr->index)+1] = '\0';
						ptr->left->decompression = x;
						ptr->left->index=index;
						break;
					}
					ptr->left = malloc(sizeof(struct node));
					counter++;
					ptr->left->left = NULL;
					ptr->left->right = NULL;
					char *index = malloc(sizeof(char)*strlen(ptr->index)+2);
					strncpy(index,ptr->index,strlen(ptr->index));
					index[strlen(ptr->index)] = '0';
					index[strlen(ptr->index)+1] = '\0';
					ptr->decompression = -1;
					ptr->left->index=index;
					
					ptr = ptr->left;
				}
				else{
					ptr = ptr->left;
				}
			}
		}
	}
	return root;
}


void * connection_handler(void * arg){
	// when we receive a connection, we will need to store the request and analyze
	struct connection_data *data = (struct connection_data*)arg;
	// we first read the 1 byte header to determine the type and retrieve relevant flags
	while(1){
		uint8_t *message_type = malloc(sizeof(uint8_t));
		if (read(data->socketfd,message_type,sizeof(uint8_t))<=0){
			free(message_type);
			break;
		}
		// read and mask to get the corresponding bits for compression 
		unsigned char type_byte = (*message_type) >> 4; // shift 4 bits to the right, remaining is type digit
		unsigned char is_compression = ((*message_type) >> 3) & 0x01; // shift 3 bits to the right, and mask all but first bit
		unsigned char need_compression = ((*message_type) >> 2) & 0x01; // shift 2 bits to the right, and mask all but first bit
		
		// if the request is **ECHO**
		if (type_byte==0){
			uint8_t *message_header = malloc(8);
			uint8_t *payload_length = malloc(8);
			// read the 8 bytes representing the length
			if (read(data->socketfd,message_header,8)<=0){
				close(data->socketfd);
				free(message_type);
				data_free(data);
				exit(0);
			};
			// change to host byte order
			for (int x =7; x>=0;x--){
				payload_length[7-x] = message_header[x];
			}
			// payload_length into int
			uint64_t length = *((uint64_t *)payload_length);

			// read the payload and store it in response_message
			uint8_t *read_payload = malloc(length);
			if (read(data->socketfd,read_payload,length)<0){
				close(data->socketfd);
				free(message_type);
				data_free(data);
				exit(0);
			}
			// if the request is compressed and need compression
			// just send as is
			if (is_compression){
				// allocate memory for resposne msg
				uint8_t *response_message = malloc(1+8+(int)length);
				// response type
				response_message[0]=0x18;
				// payload length
				for (int x = 7; x>=0; x--){
					response_message[8-x] = payload_length[x];
				}
				
				for (int x = 0; x<length; x++){
					response_message[9+x] = read_payload[x];
				}
				write(data->socketfd, response_message, 1+8+(int)length);

				// free after action
				free(response_message);
				free(read_payload);
				free(message_header);
				free(message_type);
				message_type = NULL;
				free(payload_length);
				continue;
			}
			// check if it requries compression
			else{
				// get the compressed length and compressed payload
				uint64_t *compressed_length = malloc(8);
				uint8_t *compressed_payload = compress_payload(read_payload, data->dictionary, length, compressed_length);
				
				// craft response
				uint8_t *response_message = malloc(1+8+*compressed_length);
				
				// response type byte
				response_message[0] = 0x18; // 0001 1000
				int response_offset = 1;
				
				// payload length
				for (int x = 0; x<8;x++){
					response_message[x+response_offset] = (*compressed_length)>>(8*(7-x));
				}
				response_offset += 8;
				// payload content
				for (int x = 0; x<*compressed_length; x++){
					response_message[x+response_offset] = compressed_payload[x];
				}
				write(data->socketfd, response_message, 1+8+*compressed_length);
				// free after action
				free(response_message);
				free(read_payload);
				free(message_header);
				free(message_type);
				free(compressed_length);
				free(compressed_payload);
				message_type = NULL;
				free(payload_length);
			}
			
		}
		// if the request is **DIRECTORY LISTING**
		else if (type_byte==2){
			uint8_t message_header[8] = {0};
			uint8_t payload_length[8] = {0};
			// read the 8 bytes representing the length
			if (read(data->socketfd,&message_header[0],8)<=0){
				free(message_type);
				close(data->socketfd);
				close(data->server_socket);
				data_free(data);
				exit(0);
			};
			// change to host byte order
			for (int x =7; x>=0;x--){
				payload_length[7-x] = message_header[x];
			}
			// payload_length into int
			uint64_t length = *((uint64_t *)&payload_length[0]);
			if (length!=0){
				free(message_type);
				close(data->socketfd);
				close(data->server_socket);
				data_free(data);
				exit(0);
			}
			int *number_of_files = calloc(sizeof(int),1);
			*number_of_files = -1;
			int *num_bytes = malloc(sizeof(int));
			uint8_t *filename_list = read_filenames(data->filepath,number_of_files,num_bytes);
			if (*number_of_files==0){
				send_err(data,2);
				free(num_bytes);
				free(number_of_files);
				free(filename_list);
				free(message_type);
				message_type = NULL;
				continue;
			}
			else if (filename_list == NULL){
				send_err(data,2);
				free(num_bytes);
				free(number_of_files);
				free(filename_list);
				free(message_type);
				message_type = NULL;
				continue;
			}
			else{
				if (need_compression){
					uint64_t *compressed_length = malloc(8);
					uint8_t *compressed_payload = compress_payload(filename_list, data->dictionary, (uint64_t)(*num_bytes), compressed_length);

					// craft response
					uint8_t *response_message = malloc(1+8+*compressed_length);

					// response type byte
					response_message[0] = 0x38; // 0x3 1000
					int response_offset = 1;

					// payload length
					for (int x = 0; x<8;x++){
						response_message[x+response_offset] = (*compressed_length)>>(8*(7-x));
					}
					response_offset += 8;

					// payload content
					for (int x = 0; x<*compressed_length; x++){
						response_message[x+response_offset] = compressed_payload[x];
					}

					write(data->socketfd, response_message, 1+8+*compressed_length);

					free(message_type);
					free(num_bytes);
					free(number_of_files);
					free(filename_list);
					free(compressed_length);
					free(compressed_payload);
					free(response_message);
				}
				else{
					// craft response
					uint8_t *response_message = malloc(1+8+(int)*num_bytes);
					// type digit
					response_message[0] = 0x30;
					// payload length
					response_message[1] = (uint64_t)*num_bytes>>56;
					response_message[2] = (uint64_t)*num_bytes>>48;
					response_message[3] = (uint64_t)*num_bytes>>40;
					response_message[4] = (uint64_t)*num_bytes>>32;
					response_message[5] = (uint64_t)*num_bytes>>24;
					response_message[6] = (uint64_t)*num_bytes>>16;
					response_message[7] = (uint64_t)*num_bytes>>8;
					response_message[8] = (uint64_t)*num_bytes>>0;

					// payload content
					for (int x = 0; x<*num_bytes; x++){
						response_message[x+9] = filename_list[x];
					}

					write(data->socketfd, response_message, 1+8+(int)*num_bytes);
					free(message_type);
					free(num_bytes);
					free(number_of_files);
					free(filename_list);
					free(response_message);
				}
			}
		}
		// if the request is **SHUTDOWN**
		else if (type_byte==8){
			free(message_type);
			close(data->server_socket);
			close(data->socketfd);
			data_free(data);
			exit(0);
		}
		// if the request is **FILE SIZE QUERY**
		else if (type_byte==4){
			// get payload length
			uint8_t message_header[8] = {0};
			uint8_t payload_length[8] = {0};
			if (read(data->socketfd,&message_header[0],8)<=0){
				free(message_type);
				close(data->socketfd);
				close(data->server_socket);
				data_free(data);
				exit(0);
			};
			// change to host byte order
			for (int x =7; x>=0;x--){
				payload_length[7-x] = message_header[x];
			}
			// payload_length into int
			uint64_t length = *((uint64_t *)&payload_length[0]);
			int directory_length = strlen(data->filepath);
			char * filename = malloc(sizeof(char)*length+1+directory_length+1);
			strcpy(filename,data->filepath);
			filename[directory_length] = '/';
			if (read(data->socketfd,&filename[directory_length+1],length+1)<=0){
				free(message_type);
				close(data->socketfd);
				close(data->server_socket);
				data_free(data);
				exit(0);
			}
			FILE *fp = fopen(filename,"r");
			if (fp == NULL){
				send_err(data,-1);
				free(filename);
				free(message_type);
				continue;
			}
			// place the cursor in the file to end of file
			fseek(fp, 0, SEEK_END);
			// determine the length of file content
			uint64_t file_length = (uint64_t)ftell(fp);
			// reset the cursor to start of file
			rewind(fp);
			fclose(fp);

			if (need_compression){
				uint8_t filesize[8] = {0};
				// pre compressed payload
				for (int x = 0; x<8;x++){
					filesize[x] = (file_length)>>(8*(7-x));
				}
				uint64_t *compressed_length = malloc(8);
				uint8_t *compressed_payload = compress_payload(filesize, data->dictionary, (uint64_t)8, compressed_length);
				
				// craft response
				uint8_t *response_message = malloc(1+8+*compressed_length);
				
				// response type byte
				response_message[0] = 0x58; // 0001 1000
				int response_offset = 1;
				
				// payload length
				for (int x = 0; x<8;x++){
					response_message[x+response_offset] = (*compressed_length)>>(8*(7-x));
				}
				response_offset += 8;
				// payload content
				for (int x = 0; x<*compressed_length; x++){
					response_message[x+response_offset] = compressed_payload[x];
				}
				write(data->socketfd, response_message, 1+8+*compressed_length);
				free(message_type);
				free(response_message);
				free(filename);
				free(compressed_length);
				free(compressed_payload);
			}
			else{
				uint8_t *response_message = malloc(1+8+8);
				// type digit
				response_message[0] = 0x50;
				// payload length
				response_message[1] = 0x00;
				response_message[2] = 0x00;
				response_message[3] = 0x00;
				response_message[4] = 0x00;
				response_message[5] = 0x00;
				response_message[6] = 0x00;
				response_message[7] = 0x00;
				response_message[8] = 0x08;
				// payload content
				response_message[9] = file_length>>56;
				response_message[10] = file_length>>48;
				response_message[11] = file_length>>40;
				response_message[12] = file_length>>32;
				response_message[13] = file_length>>24;
				response_message[14] = file_length>>16;
				response_message[15] = file_length>>8;
				response_message[16] = file_length;

				write(data->socketfd, response_message, 1+8+8);
				free(message_type);
				free(response_message);
				free(filename);
			}
		}
		// if the request is **RETRIEVE FILE**
		else if (type_byte==6){
			
			struct session *curr_session;

			// get payload length
			uint8_t payload_length_pre[8] = {0};
			uint8_t payload_length_swap[8] = {0};
			if (read(data->socketfd,&payload_length_pre[0],8)<=0){
				free(message_type);
				close(data->socketfd);
				close(data->server_socket);
				data_free(data);
				exit(0);
			}
			// change to host byte order
			for (int x =7; x>=0;x--){
				payload_length_swap[7-x] = payload_length_pre[x];
			}
			uint64_t payload_length = *((uint64_t *)&payload_length_swap[0]);

			uint64_t content_length;
			uint8_t session_pre_id[4] = {0};
			uint8_t offset_pre[8];
			uint8_t length_pre[8];
			uint8_t *content;

			if (is_compression){
				uint8_t* request_payload = malloc(payload_length);
				if (read(data->socketfd,request_payload,payload_length)<=0){
					free(message_type);
					free(request_payload);
					close(data->socketfd);
					close(data->server_socket);
					data_free(data);
					exit(0);
				}
				
				uint64_t *bits_length = malloc(8);
				uint8_t *bits = bits_extractions(request_payload, &payload_length, bits_length);
				
				// decompress read payload
				uint64_t *decompressed_length = malloc(8);
				uint8_t *decompressed_payload = decompress(data->tree, bits, decompressed_length, bits_length);
				free(request_payload);
				request_payload = decompressed_payload;
				free(bits_length);
				free(bits);

				// now request_payload is decompressed

				// get session ID
				int request_offset = 0;
				for (int x = 0; x<4; x++){
					session_pre_id[x] = request_payload[request_offset+x];
				}
				request_offset+=4;
				// session ID into host int
				uint32_t session_id = *((uint32_t *)&session_pre_id[0]);
				session_id = ntohl(session_id);

				// read offset
				uint8_t offset_swap[8] = {0};
				for (int x = 0; x<8; x++){
					offset_pre[x] = request_payload[request_offset+x];
				}
				request_offset+=8;

				// read content length 
				uint8_t length_swap[8] = {0};
				for (int x = 0; x<8; x++){
					length_pre[x] = request_payload[request_offset+x];
				}
				request_offset+=8;
				
				// change to host byte order
				for (int x =7; x>=0;x--){
					offset_swap[7-x] = offset_pre[x];
					length_swap[7-x] = length_pre[x];
				}
				// content_length and offset to int
				content_length = *((uint64_t *)&length_swap[0]);
				uint64_t offset = *((uint64_t *)&offset_swap[0]);

				// read filename				
				int directory_length = strlen(data->filepath);
				int filename_length = directory_length+1;
				char *filename = malloc(sizeof(char)*255);
				strcpy(filename,data->filepath);
				filename[strlen(data->filepath)] = '/';
				while (request_payload[request_offset]!='\0'){
					filename[filename_length++] = request_payload[request_offset++];
				}
				filename[filename_length] = '\0';

				int session_found = 0;
				int breaker = 0;
				// lock the list when checking if session is in use
				pthread_mutex_lock(&(global_session->lock));
				// if no session
				if (*(global_session->number_sessions)==0){
					*(global_session->number_sessions)+=1;
					
					// create session
					struct session *new_session = malloc(sizeof(struct session));
					new_session->availability = 0;
					new_session->filename = malloc(sizeof(char)*256);
					strcpy(new_session->filename,filename);
					// new_session->filename = filename;
					new_session->offset = offset;
					new_session->length = content_length;
					new_session->sessionID = session_id;

					global_session->sessions_list[0] = new_session;
					curr_session = new_session;
				}
				else{
					for (int x = 0; x<*(global_session->number_sessions);x++){
						// if session id is available for use
						if (global_session->sessions_list[x]->availability){
							// if a session id already exists for use
							if (session_id == global_session->sessions_list[x]->sessionID){
								curr_session = global_session->sessions_list[x];
								curr_session->availability=0;
								free(curr_session->filename);
								curr_session->filename = malloc(sizeof(char)*256);
								strcpy(curr_session->filename,filename);
								// curr_session->filename = filename;
								curr_session->offset = offset;
								curr_session->length = content_length;
								session_found = 1;
							}
						}
						// if session is already in use
						else{
							// if the session id is equal
							if (session_id == global_session->sessions_list[x]->sessionID){
								send_err(data,-1);
								breaker = 1;
								break;
							}
						}
					}
				}
				if (breaker){
					free(message_type);
					free(filename);
					pthread_mutex_unlock(&(global_session->lock));
					break;
				}
				// if no session id has been created
				if (session_found==0){
					*(global_session->number_sessions)+=1;
					global_session->sessions_list = realloc(global_session->sessions_list,sizeof(struct session *)**(global_session->number_sessions));
					// create session
					struct session *new_session = malloc(sizeof(struct session));
					new_session->availability = 0;
					new_session->filename = malloc(sizeof(char)*256);
					strcpy(new_session->filename,filename);
					// new_session->filename = filename;
					new_session->offset = offset;
					new_session->length = content_length;
					new_session->sessionID = session_id;

					curr_session = new_session;
					global_session->sessions_list[*(global_session->number_sessions)-1]= new_session;
				}
				pthread_mutex_unlock(&(global_session->lock));


				// read content from file
				FILE *fp = fopen(filename,"r");
				if (fp==NULL){
					send_err(data,-1);
					free(message_type);
					free(filename);
					curr_session->availability=1;
					continue;
				}
				fseek(fp,0,SEEK_END);
				long file_length = ftell(fp);
				rewind(fp);
				if (content_length+offset>file_length){
					send_err(data,-1);
					free(message_type);
					rewind(fp);
					fclose(fp);
					free(filename);
					curr_session->availability=1;
					continue;
				}
				fseek(fp,offset,SEEK_SET);
				content = malloc(content_length);
				// use a loop to read all the bytes into content
				for (int i = 0; i<content_length; i++){
					fread(content+i,1,1,fp);
				}
				rewind(fp);
				fclose(fp);
				free(filename);
				free(decompressed_length);

			}
			else{
				// read session ID
				if (read(data->socketfd,&session_pre_id[0],4)<=0){
					free(message_type);
					close(data->socketfd);
					close(data->server_socket);
					data_free(data);
					exit(0);
				};
				
				// session ID into host int
				uint32_t session_id = *((uint32_t *)&session_pre_id[0]);
				session_id = ntohl(session_id);

				// read offset
				uint8_t offset_swap[8] = {0};
				if (read(data->socketfd,&offset_pre[0],8)<=0){
					free(message_type);
					close(data->socketfd);
					close(data->server_socket);
					data_free(data);
					exit(0);
				};
				
				uint8_t length_swap[8] = {0};
				if (read(data->socketfd,&length_pre[0],8)<=0){
					free(message_type);
					close(data->socketfd);
					close(data->server_socket);
					data_free(data);
					exit(0);
				};
				
				// change to host byte order
				for (int x =7; x>=0;x--){
					offset_swap[7-x] = offset_pre[x];
					length_swap[7-x] = length_pre[x];
				}
				// content_length and offset to int
				content_length = *((uint64_t *)&length_swap[0]);
				uint64_t offset = *((uint64_t *)&offset_swap[0]);
				
				// read filename
				int directory_length = strlen(data->filepath);
				char *filename = malloc(sizeof(char)*payload_length-4-8-8+1+directory_length+1);
				strcpy(filename,data->filepath);
				filename[strlen(data->filepath)] = '/';
				if (read(data->socketfd,&filename[directory_length+1],payload_length-4-8-8+1)<=0){
					free(message_type);
					close(data->socketfd);
					close(data->server_socket);
					data_free(data);
					free(filename);
					exit(0);
				}

				int session_found = 0;
				int breaker = 0;
				// if no session
				// pthread_mutex_lock(&(data->lock));
				pthread_mutex_lock(&(global_session->lock));
				if (*(global_session->number_sessions)==0){
					*(global_session->number_sessions)+=1;
					
					// create session
					struct session *new_session = malloc(sizeof(struct session));
					new_session->availability = 0;
					new_session->filename = malloc(sizeof(char)*256);
					strcpy(new_session->filename,filename);
					// new_session->filename = filename;
					new_session->offset = offset;
					new_session->length = content_length;
					new_session->sessionID = session_id;

					global_session->sessions_list[0] = new_session;
					curr_session = new_session;
				}
				else{
					for (int x = 0; x<*(global_session->number_sessions);x++){
						// if session id is available for use
						if (global_session->sessions_list[x]->availability){
							// if a session id already exists for use
							if (session_id == global_session->sessions_list[x]->sessionID){
								curr_session = global_session->sessions_list[x];
								curr_session->availability=0;
								free(curr_session->filename);
								curr_session->filename = malloc(sizeof(char)*256);
								strcpy(curr_session->filename,filename);
								// curr_session->filename = filename;
								curr_session->offset = offset;
								curr_session->length = content_length;
								session_found = 1;
							}
						}
						// if session is already in use
						else{
							// if the session id is equal
							if (session_id == global_session->sessions_list[x]->sessionID){
								send_err(data,-1);
								breaker = 1;
								break;
							}
						}
					}
				}
				if (breaker){
					pthread_mutex_unlock(&(global_session->lock));
					free(message_type);
					free(filename);
					break;
				}
				// pthread_mutex_unlock(&(data->lock));
				// if no session id has been created
				if (session_found==0){
					// pthread_mutex_lock(&(data->lock));
					*(global_session->number_sessions)+=1;
					global_session->sessions_list = realloc(global_session->sessions_list,sizeof(struct session *)**(global_session->number_sessions));
					// create session
					struct session *new_session = malloc(sizeof(struct session));
					new_session->availability = 0;
					// new_session->filename = realloc(new_session->filename,strlen(filename)+1);
					// strcpy(new_session->filename,filename);
					new_session->filename = malloc(sizeof(char)*256);
					strcpy(new_session->filename,filename);
					// new_session->filename = filename;
					new_session->offset = offset;
					new_session->length = content_length;
					new_session->sessionID = session_id;

					curr_session = new_session;
					global_session->sessions_list[*(global_session->number_sessions)-1]= new_session;
					// pthread_mutex_unlock(&(data->lock));
				}
				pthread_mutex_unlock(&(global_session->lock));
				// pthread_mutex_unlock(&(data->lock));
				
				// read content from file
				FILE *fp = fopen(filename,"r");
				if (fp==NULL){
					send_err(data,-1);
					free(message_type);
					free(filename);
					curr_session->availability=1;
					continue;
				}
				fseek(fp,0,SEEK_END);
				long file_length = ftell(fp);
				rewind(fp);
				if (content_length+offset>file_length){
					send_err(data,-1);
					free(message_type);
					rewind(fp);
					fclose(fp);
					free(filename);
					curr_session->availability=1;
					continue;
				}
				fseek(fp,offset,SEEK_SET);
				content = malloc(content_length);
				// use a loop to read all the bytes into content
				for (int i = 0; i<content_length; i++){
					fread(content+i,1,1,fp);
				}
				rewind(fp);
				fclose(fp);
				free(filename);
			}
		
			if (need_compression){
				uint64_t uncompressed_length = 4+8+8+content_length;
				uint8_t *uncompressed_payload = malloc(4+8+8+content_length);
				uint64_t *compressed_length = malloc(8); 
				int uncompressed_offset = 0;
				// session ID
				for (int x = 0; x<4; x++){
					uncompressed_payload[x+uncompressed_offset] = session_pre_id[x];
				}
				uncompressed_offset+=4;
				// offset
				for (int x = 0; x<8; x++){
					uncompressed_payload[x+uncompressed_offset] = offset_pre[x];
				}
				uncompressed_offset+=8;
				// length
				for (int x = 0; x<8; x++){
					uncompressed_payload[x+uncompressed_offset] = length_pre[x];
				}
				uncompressed_offset+=8;
				for (int x = 0; x<content_length; x++){
					uncompressed_payload[x+uncompressed_offset] = content[x];
				}
				uint8_t *compressed_payload = compress_payload(uncompressed_payload, data->dictionary, uncompressed_length, compressed_length);
				
				// craft response
				uint8_t *response_message = malloc(1+8+*compressed_length);

				// response type byte
				response_message[0] = 0x78; // 0x3 1000
				int response_offset = 1;

				// payload length
				for (int x = 0; x<8;x++){
					response_message[x+response_offset] = (*compressed_length)>>(8*(7-x));
				}
				response_offset += 8;

				// payload content
				for (int x = 0; x<*compressed_length; x++){
					response_message[x+response_offset] = compressed_payload[x];
				}

				write(data->socketfd, response_message, 1+8+*compressed_length);
				free(message_type);
				free(content);
				free(response_message);
				free(compressed_length);
				free(compressed_payload);
				free(uncompressed_payload);

				curr_session->availability = 1;
			}
			else{
				// craft response
				uint8_t *response = malloc(1+8+4+8+8+content_length);
				// response type
				response[0] = 0x70;
				int response_offset = 1;
				// payload length
				for (int x = 0; x<8;x++){
					response[x+response_offset] = (content_length+4+8+8)>>(8*(7-x));
				}
				response_offset+=8;
				// session ID
				for (int x = 0; x<4; x++){
					response[x+response_offset] = session_pre_id[x];
				}
				response_offset+=4;
				// offset
				for (int x = 0; x<8; x++){
					response[x+response_offset] = offset_pre[x];
				}
				response_offset+=8;
				// length
				for (int x = 0; x<8; x++){
					response[x+response_offset] = length_pre[x];
				}
				response_offset+=8;
				for (int x = 0; x<content_length; x++){
					response[x+response_offset] = content[x];
				}
				// for (int x = 0; x<1+8+4+8+8+content_length; x++){
				// 	printf("%x",response[x]);
				// 	if (x==0){
				// 		printf("\n");
				// 	}
				// 	else if (x==8){
				// 		printf("\n");
				// 	}
				// 	else if (x==12){
				// 		printf("\n");
				// 	}
				// 	else if (x==20){
				// 		printf("\n");
				// 	}
				// 	else if (x==28){
				// 		printf("\n");
				// 	}
				// }
				// printf("\n");
				write(data->socketfd,response,1+8+4+8+8+content_length);
				free(message_type);
				free(response);
				free(content);

				curr_session->availability=1;
			}

			
		}
		// if the request is not valid
		else{
			send_err(data,-1);
			// free memory
			free(message_type);
			message_type = NULL;
		}
	}
	close(data->socketfd);
	free(data);
	return NULL;
}



int main (int argc, char *argv[]){
	// The first step is to process the config file given in the  command line argument

	// checks the number of argument is correct
	if (argc!=2){
		puts("Incorrect amount of arguments\n");
		exit(0);
	}
	
	// open the file specified by the command line argument
	char *path = argv[1];
	FILE *fp = fopen(path,"rb");
	if (fp == NULL){
		puts("Cant open file\n");
		exit(0);
	}

	// read the contents of config
	int *config_length_ptr = malloc(sizeof(int));
	uint8_t *config_read = read_config(fp,config_length_ptr);
	int config_length = *config_length_ptr;
	free(config_length_ptr);
	
	// payload_length the content to path string
	char *target_directory = malloc((config_length)-5);
	for (int i = 6; i<config_length; i++){
		target_directory[i-6] = config_read[i];
	}
	target_directory[(config_length)-6] = '\0';
	// printf("%s\n",target_directory);
	// get the port number -- index 4 & 5
	// long winded conversion from uint8 to u16int
	uint8_t port_8[2];
	port_8[0] = *(config_read+4);
	port_8[1] = *(config_read+5);
	uint16_t *port_16_ptr = (uint16_t *)port_8;
	uint16_t port = *port_16_ptr;
		
	// get the address
	// first put the address into string format
	char str_addr[30];
	sprintf(&str_addr[0], "%d.%d.%d.%d",*(config_read),*(config_read+1),*(config_read+2),*(config_read+3));



	// struct in_addr is how the address is stored
	struct in_addr inaddr;
	// payload_length address from dotted quad notation (e.g. 127.0.0.1) to network byte order integer, and also save it to the struct
	// Access the integer representation itself with the s_addr field of struct in_addr
	inet_pton(AF_INET, str_addr, &inaddr);
	
	// At this point, we have the address and the port
	// we will now create a socket

	int option = 1;
	// Create socket, and check for error
	// AF_INET = this is an IPv4 socket
	// SOCK_STREAM = this is a TCP socket
	// connection is already a file descriptor. It just isn't connected to anything yet.
	int server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket < 0) {
		perror("socket");
		return 1;
	}

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = port;
	server.sin_addr = inaddr;

	setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(int));

	// we have now bind a port with a socket
	if (bind(server_socket, (struct sockaddr*)&server, sizeof(struct sockaddr_in))){
		puts("BIND!\n");
		exit(1);
	}

	listen(server_socket,SOMAXCONN);

	// load dictionary
	int *bit_array = bit_array_construction();
	int ** dictionary = make_dictionary(bit_array);
	free(bit_array);

	struct node *my_tree = plant_tree(dictionary);

	global_session = calloc(sizeof(struct server_session),1);
	global_session->number_sessions = calloc(sizeof(int),1);
	global_session->sessions_list = calloc(sizeof(struct session*),1);
	pthread_mutex_init(&(global_session->lock),NULL);
	// continually run the server
	while(1){
		// accept connection from one client
		uint32_t addrlen = sizeof(struct sockaddr_in);
		int clientsocket_fd = accept(server_socket, (struct sockaddr*)&server, &addrlen);

		// we now read the the data from the client socket
		struct connection_data *d = malloc(sizeof(struct connection_data));
		// pthread_mutex_init(&(d->lock), NULL);	
		d->socketfd = clientsocket_fd;
		d->filepath = target_directory;
		d->server_socket = server_socket;
		d->dictionary = dictionary;
		d->tree = my_tree;
		pthread_t thread;
		pthread_create(&thread, NULL, connection_handler, d);
		// pthread_detach(thread);
	}
	close(server_socket);
	return 0;
	
	

}