#ifndef PROTOCOL_H
#define PROTOCOL_H

#define AUTH_PACKET 0x01
#define GET_WEIGHT_PACKET 0x02
#define SEND_WEIGHT_PACKET 0x03
#define GET_LATEST_MODEL_PACKET 0x04

#include "globals.h"
#include "socket_server.h"

#ifndef DEBUG_PROTOCOL
#define DEBUG_PROTOCOL 0
#endif

// User must implement this function
int is_valid_metadata(mf_metadata_t *metadata, size_t buff_size);
int handle_packet_event(generic_session_t *__session);

#endif // PROTOCOL_H
