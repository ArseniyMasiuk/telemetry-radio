#include <inttypes.h>

void configure_udp_manager(void);
bool udp_client_add(const char *ip_str, uint16_t port);
void udp_client_remove(const char *ip_str);
void udp_send_all(const uint8_t *data, size_t len);
