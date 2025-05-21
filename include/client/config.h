#ifndef CONFIG_H
#define CONFIG_H

#include "json_manager.h"

#define SHM_PATH_SIZE 64
typedef struct
{
    char session_token[SESSION_TOKEN_SIZE];
    char shm_path[SHM_PATH_SIZE];
    char client_type[MIN_SIZE];
    char protocol[PROTOCOL_SIZE];
    char ip_address[MIN_SIZE];
    char ip_version[PROTOCOL_SIZE];
    char port[PROTOCOL_SIZE];
    char username[MIN_SIZE];
    char password[MIN_SIZE];

} identifiers;

/* @brief
 * Function to get the identifiers.
 * @return Pointer to the identifiers structure.
 */
identifiers* get_identifiers();

/* @brief
 * Function to set the session token.
 * @param session_token The session token to set.
 */
void set_session_token(const char* session_token);

/* @brief
 * Function to create the shm path.
 */
void set_shm_path();

/* @brief
 * Function to set the client type.
 * @param client_type The client type to set.
 */
void set_client_type(const char* client_type);

/* @brief
 * Function to set the protocol.
 * @param protocol The protocol to set.
 */
void set_protocol(const char* protocol);

/* @brief
 * Function to set the ip address.
 * @param ip_address The ip address to set.
 */
void set_ip_address(const char* ip_address);

/* @brief
 * Function to set the ip version.
 * @param ip_version The ip version be ipv4 or ipv6.
 */
void set_ip_version(const char* ip_version);

/* @brief
 * Function to set the port.
 * @param port The port to set.
 */
void set_port(const char* port);

/* @brief
 * Function to set the username.
 * @param username The username to set.
 */
void set_username(const char* username);

/* @brief
 * Function to set the port.
 * @param port The port to set.
 */
void set_password(const char* password);

/* @brief
 * Function to set the parameters.
 * @param params The parameters to set.
 */
void set_params(init_params_client params);

#endif
