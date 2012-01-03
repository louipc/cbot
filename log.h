#ifndef LOG_H
#define LOG_H

struct recv_data; // defined in main.h

void log_init();
void log_message(struct recv_data *in);
void log_terminate();

/* vim: set ts=8 sw=8 tw=0 noexpandtab cindent softtabstop=8 :*/
#endif//LOG_H