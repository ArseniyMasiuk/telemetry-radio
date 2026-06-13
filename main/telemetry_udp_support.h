#include <inttypes.h>

void configure_udp_manager(void);
bool udp_client_add(const char *ip_str, uint8_t mac_addr[6]);
void udp_client_remove(uint8_t target_mac_addr[6]);
void udp_send_all(const uint8_t *data, size_t len);
