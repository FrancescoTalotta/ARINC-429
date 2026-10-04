#pragma once

#include <stdint.h>
#include <string.h>

typedef enum {
    ARINC_RX_NOT_COMMAND,
    ARINC_RX_INVALID_COMMAND,
    ARINC_RX_SPEED_LOW,
    ARINC_RX_SPEED_HIGH,
    ARINC_RX_PORT,
} arinc_rx_command_t;

/* RX commands are separate datagrams, one key=value only. Surrounding ASCII
 * whitespace is allowed; mixed TX/RX, duplicate keys and extra tokens are not.
 * Reserve rx_* even inside a TX command so the permissive legacy TX parser
 * cannot silently accept a malformed RX request or apply part of it. */
static inline arinc_rx_command_t arinc_rx_parse_command(const char *line, uint16_t *port)
{
    const char *delimiters = " \t\r\n,;";
    const char *p = line;
    int has_rx = 0;
    while (*p) {
        p += strspn(p, delimiters);
        if (strncmp(p, "rx_", 3) == 0) {
            has_rx = 1;
        }
        p += strcspn(p, delimiters);
    }
    if (!has_rx) {
        return ARINC_RX_NOT_COMMAND;
    }
    line += strspn(line, " \t\r\n");
    size_t len = strlen(line);
    while (len && strchr(" \t\r\n", line[len - 1])) {
        --len;
    }
    if (len == 12 && memcmp(line, "rx_speed=low", 12) == 0) {
        return ARINC_RX_SPEED_LOW;
    }
    if (len == 13 && memcmp(line, "rx_speed=high", 13) == 0) {
        return ARINC_RX_SPEED_HIGH;
    }
    if (len > 8 && memcmp(line, "rx_port=", 8) == 0) {
        uint32_t value = 0;
        for (size_t i = 8; i < len; ++i) {
            if (line[i] < '0' || line[i] > '9') {
                return ARINC_RX_INVALID_COMMAND;
            }
            value = value * 10U + (unsigned)(line[i] - '0');
            if (value > 65535U) {
                return ARINC_RX_INVALID_COMMAND;
            }
        }
        *port = (uint16_t)value;
        return ARINC_RX_PORT;
    }
    return ARINC_RX_INVALID_COMMAND;
}
