#ifndef DHL_CLIENT_H
#define DHL_CLIENT_H

#include "facade.h"

/* @brief
 * Function to start the client.
 * @param client The client index in the config file.
 * @return 0 on success, 1 on failure.
 */
int start_client(char* client);

/* @brief
 * Function to start the command line interface.
 * @return 0 on success, 1 on failure.
 */
int start_cli();

#endif
