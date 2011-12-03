// for getaddrinfo and the likes
#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pcre.h>
#include <pthread.h>
#include <sys/select.h>

#define BUFFER 512

// structs
struct recv_data {
	char nick[32];
	char user[32];
	char server[64];
	char channel[32];
	char message[BUFFER];
};

struct patterns {
	pcre *privmsg;
	pcre *kick;
};


// Sending stuff
char *send_buffer = 0;
int send_buffer_size = 0;
int send_buffer_used = 0;
pthread_mutex_t *send_mutex = 0;
pthread_t *send_thread = 0;
int send_thread_running = 0;

int socket_id;


// prototypes
void compile_patterns(struct patterns *patterns);
void die(const char *msg, const char *err);
void parse_input(char *msg, struct recv_data *in, struct patterns *patterns);
void send_str(int socket_id, char *msg, int length);

void compile_patterns(struct patterns *patterns)
{
	const char *pcre_err;
	int pcre_err_off;
	char *pattern = ":([^!]+)!([^@]+)@(\\S+)\\sPRIVMSG\\s(\\S+)\\s:([^\\b]+)";
	if ((patterns->privmsg = pcre_compile(pattern, PCRE_CASELESS | PCRE_UTF8, &pcre_err, &pcre_err_off, 0)) == NULL)
		die("pcre compile privmsg", 0);
}

void die(const char *msg, const char *error)
{
	fprintf(stderr, "%s: %s\n", msg, error);
	exit(1);
}

void parse_input(char *msg, struct recv_data *in, struct patterns *patterns)
{
	//TODO: check 30
	int offsets[30];
	int offsetcount = 30;
	offsetcount = pcre_exec(patterns->privmsg, NULL, msg, strlen(msg), 0, 0, offsets, offsetcount);
	if (offsetcount == 6) {
		pcre_copy_substring(msg, offsets, offsetcount, 1, in->nick, 32);
		pcre_copy_substring(msg, offsets, offsetcount, 2, in->user, 32);
		pcre_copy_substring(msg, offsets, offsetcount, 3, in->server, 64);
		pcre_copy_substring(msg, offsets, offsetcount, 4, in->channel, 32);
		pcre_copy_substring(msg, offsets, offsetcount, 5, in->message, BUFFER);
	//	printf("user: %s\nserver: %s\nnick: %s\nchannel: %s\nmessage: %s\n", in->user, in->server, in->nick, in->channel, in->message);
	}
}

void send_str(int socket_id, char *msg, int length)
{
	pthread_mutex_lock(send_mutex);
	if (send_buffer == 0) {
		send_buffer = malloc(BUFFER);
		send_buffer_size = BUFFER;
	}

	// Check if we have enough space
	if (length > send_buffer_size - send_buffer_used) {
		int new_buffer_size = send_buffer_size + BUFFER;
		if (!realloc(send_buffer, new_buffer_size)) {
			die("Unable to allocate more memory for send buffer", strerror(errno));
		}
		send_buffer_size = new_buffer_size;
	}

	memcpy(&send_buffer[send_buffer_used], msg, length);
	send_buffer_used += length;
	printf("lolbuf:%s\n",send_buffer);

	pthread_mutex_unlock(send_mutex);

	// Print out what we have done
	char send_str[BUFFER];
	sprintf(send_str, "%s\n", msg);
	printf("--> %s", send_str);
}

void *send_loop(void *arg) {
	while (send_thread_running) {
		pthread_mutex_lock(send_mutex);
		while (send_buffer_used > 0) {
			int sent = send(socket_id, send_buffer, send_buffer_used, 0);
			if (sent == -1) {
				die("Unable to send", strerror(errno));
			}
			printf("actually sent: %d\n", send_buffer_used);
			send_buffer_used -= sent;
		}
		pthread_mutex_unlock(send_mutex);
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 6) {
		printf("Usage: %s nick user host port channel\n", argv[0]);
		exit(0);
	}
	const char *nick = argv[1];
	const char *user = argv[2];
	const char *host = argv[3];
	const char *port = argv[4];
	const char *channel = argv[5];

	int err, recv_size;
	char buffer[BUFFER];
	struct addrinfo hints;
	struct addrinfo *srv;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;

	// Connect
	if ((err = getaddrinfo(host, port, &hints, &srv)) != 0)
		die("getaddrinfo", gai_strerror(err));
	if ((socket_id = socket(srv->ai_family, srv->ai_socktype, 0)) < 0)
		die("socket", gai_strerror(socket_id));
	if ((err = connect(socket_id, srv->ai_addr, srv->ai_addrlen)) != 0)
		die("connect", gai_strerror(err));

	// Create our mutexes
	send_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(send_mutex, 0);

	// Create our sending thread
	send_thread = (pthread_t*)malloc(sizeof(pthread_t));
	send_thread_running = 1;
	pthread_create(send_thread, 0, &send_loop, 0);

	// Select param
	fd_set socket_set;
	FD_ZERO(&socket_set);
	FD_SET(socket_id, &socket_set);

	int len = sprintf(buffer, "USER %s host realmname :%s\nNICK %s\nJOIN #%s", user, nick, nick, channel);
	send_str(socket_id, buffer, len);

	struct recv_data *irc = malloc(sizeof(*irc));
	struct patterns *patterns = malloc(sizeof(*patterns));
	compile_patterns(patterns);

	while (select(sizeof(socket_set)*8, &socket_set, 0, 0, 0) != -1) {
		recv_size = recv(socket_id, buffer, BUFFER, 0);
		//overwrite \n with \0
		buffer[recv_size-1] = '\0';
		puts(buffer);
		if (strncmp(buffer, "PING :", 6) == 0) {
            		// turn the ping into a pong :D
            		buffer[1] = 'O';
			send_str(socket_id, buffer, recv_size);
		}
		else {
			parse_input(buffer, irc, patterns);
		}
	}

	close(socket_id);
	free(irc);
	free(patterns);
	return 0;
}

// vim: set formatoptions+=ro cindent expandtab=0
