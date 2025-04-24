#ifndef BEHAVIOR_CLI_H
#define BEHAVIOR_CLI_H

#include "json_manager.h"

int cli_authenticate(init_params_client params, int sockfd);
int decode_transaction_history(init_params_client params, int sockfd);
int decode_client_lives(init_params_client params, int sockfd);
int cli_message_sender(init_params_client params, int sockfd, int request_type);
int cli_message_receiver(init_params_client params, int sockfd, int request_type);
int logic_cli_sender_recv(init_params_client params, int sockfd);
void print_transaction_table_header();
void print_transaction_row(const server_transaction_history* t);
void print_client_table_header();
void print_client_row(const server_client_alive* c);

#endif
