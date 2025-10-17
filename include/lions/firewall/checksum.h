#include <stdint.h>
#include <lions/firewall/common.h>

/**
 * Calculates the Internet Checksum (RFC 1071).
 *
 * This function computes the 16-bit one's complement sum of all 16-bit words in
 * the provided buffer. If the buffer length is odd, the last byte is treated as
 * a 16-bit word with the high-order byte set to zero.
 *
 * @param pkt Address of the packet for which to calculate the checksum.
 * @param len Number of bytes of the packet to include in checksum calculation.
 * @return The calculated 16-bit Internet Checksum.
 */
static uint16_t fw_internet_checksum(void *pkt,
                                     uint16_t len)
{
    uint32_t sum = 0;
    uint16_t *buf = (uint16_t *)pkt;

    /* Sum all 16-bit words */
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }

    /* Add the remaining byte if length is odd */
    if (len == 1) {
        /* Cast to uint8_t* to access the single byte */
        sum += *(uint8_t *)buf;
    }

    /* Fold 32-bit sum to 16 bits (one's complement sum) */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    /* Take the one's complement of the final sum */
    return (uint16_t)~sum;
}

/* Psuedo-header used for UDP and TCP checksum calculation */
typedef struct fw_pseudo_header {
    uint32_t src_ip;
    uint32_t dst_ip;
    /* Always set to 0 */
    uint8_t  reserved;
    uint8_t  protocol;
    /* Transport layer packet length */
    uint16_t len;
} fw_pseudo_header_t;

/**
 * Calculates the transport layer checksum for UDP and TCP packets.
 *
 * This function computes the 16-bit one's complement sum of all 16-bit words in
 * the psuedo header and provided buffer. A psuedo header is used to avoid
 * needing to recalculate the checksum at each hop (i.e. IP header fields which
 * are updated at each hop like ttl are not included).
 *
 * @param pkt Address of the packet for which to calculate the checksum.
 * @param len Number of bytes of the packet to include in checksum calculation.
 * @param protocol IP protocol of the packet.
 * @param src_ip Source IP address in big endian byte order.
 * @param dst_ip Destination IP address in big endian byte order.
 * @return The calculated 16-bit Internet Checksum.
 */
uint16_t calculate_transport_checksum(void *pkt,
                                      uint16_t len,
                                      uint8_t protocol,
                                      uint32_t src_ip,
                                      uint32_t dst_ip)
{
    uint32_t sum = 0;
    uint16_t *pkt_ptr = (uint16_t *)pkt;

    /* Create the pseudo-header */
    fw_pseudo_header_t psh = { src_ip, dst_ip, 0, protocol, htons(len) };

    /* Sum up the psuedo-header */
    uint16_t *psh_ptr = (uint16_t *)&psh;
    for (uint8_t i = 0; i < sizeof(fw_pseudo_header_t) / sizeof(uint16_t); i++) {
        sum += psh_ptr[i];
    }

    /* Sum up the packet */
    while (len > 1) {
        sum += *pkt_ptr++;
        len -= 2;
    }

    /* Add the remaining byte if length is odd */
    if (len == 1) {
        /* Cast to uint8_t* to access the single byte */
        sum += *(uint8_t *)pkt_ptr;
    }

    /* Fold 32-bit sum to 16 bits (one's complement sum) */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    /* Take the one's complement of the final sum */
    return (uint16_t)(~sum);
}
