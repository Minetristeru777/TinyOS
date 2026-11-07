/* Minimal polling driver for the QEMU ne2k_pci device. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define PCI_CONFIG_ADDRESS 0x0cf8
#define PCI_CONFIG_DATA 0x0cfc
#define NE_RX_START 0x46
#define NE_RX_STOP 0x80
#define NE_RX_FIRST 0x47
#define NE_TX_PAGE 0x40

static u16 ne_io;
static u8 rx_page = NE_RX_FIRST;
static u8 mac_address[6];
static const u8 local_ip[4] = {10, 0, 2, 15};
static const u8 gateway_ip[4] = {10, 0, 2, 2};
static const u8 dns_ip[4] = {10, 0, 2, 3};
static u8 gateway_mac[6];
static int gateway_mac_valid;
static u8 cached_arp_ip[4];
static u8 cached_arp_mac[6];
static int cached_arp_valid;
static u8 rx_frame[1600];
static u8 tx_frame[1600];
static u8 ip_packet[1500];
static u8 dns_reply[600];
static int dns_reply_length;
static u32 ip_id = 1;
static u16 next_tcp_port = 0xc350;

int ne2k_send(const u8 *frame, u16 length);
int ne2k_receive(u8 *frame, u16 capacity);

static u16 read_be16(const u8 *p) { return ((u16)p[0] << 8) | p[1]; }
static void write_be16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static u32 read_be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void write_be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static u16 checksum(const u8 *data, u32 length) {
    u32 sum = 0;
    for (u32 i = 0; i + 1 < length; i += 2) sum += read_be16(data + i);
    if (length & 1) sum += (u16)data[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u16)~sum;
}

static u16 tcp_checksum(const u8 *source, const u8 *destination,
                        const u8 *segment, u16 length) {
    u32 sum = 0;
    for (int i = 0; i < 4; i += 2) {
        sum += read_be16(source + i);
        sum += read_be16(destination + i);
    }
    sum += 6;
    sum += length;
    for (u16 i = 0; i + 1 < length; i += 2) sum += read_be16(segment + i);
    if (length & 1) sum += (u16)segment[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u16)~sum;
}

static int ip_equal(const u8 *a, const u8 *b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int parse_ipv4(const char *text, u8 out[4]) {
    int part = 0, value = 0, digits = 0;
    for (int i = 0; ; ++i) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + c - '0';
            if (++digits > 3 || value > 255) return 0;
        } else if ((c == '.' || c == 0) && digits && part < 4) {
            out[part++] = (u8)value;
            value = digits = 0;
            if (!c) return part == 4;
        } else return 0;
    }
}

static int ethernet_send(const u8 *destination, u16 type, const u8 *payload, u16 length) {
    if (length + 14 > sizeof tx_frame) return 0;
    for (int i = 0; i < 6; ++i) { tx_frame[i] = destination[i]; tx_frame[6 + i] = mac_address[i]; }
    write_be16(tx_frame + 12, type);
    for (u16 i = 0; i < length; ++i) tx_frame[14 + i] = payload[i];
    u16 frame_length = length + 14;
    while (frame_length < 60) tx_frame[frame_length++] = 0;
    return ne2k_send(tx_frame, frame_length);
}

static void process_arp(const u8 *frame, int length) {
    if (length < 42 || read_be16(frame + 14) != 1 || read_be16(frame + 16) != 0x0800 ||
        frame[18] != 6 || frame[19] != 4) return;
    u16 operation = read_be16(frame + 20);
    const u8 *sender_mac = frame + 22;
    const u8 *sender_ip = frame + 28;
    const u8 *target_ip = frame + 38;
    for (int i = 0; i < 4; ++i) cached_arp_ip[i] = sender_ip[i];
    for (int i = 0; i < 6; ++i) cached_arp_mac[i] = sender_mac[i];
    cached_arp_valid = 1;
    if (ip_equal(sender_ip, gateway_ip)) {
        for (int i = 0; i < 6; ++i) gateway_mac[i] = sender_mac[i];
        gateway_mac_valid = 1;
    }
    if (operation != 1 || !ip_equal(target_ip, local_ip)) return;
    u8 reply[28];
    write_be16(reply, 1); write_be16(reply + 2, 0x0800);
    reply[4] = 6; reply[5] = 4; write_be16(reply + 6, 2);
    for (int i = 0; i < 6; ++i) reply[8 + i] = mac_address[i];
    for (int i = 0; i < 4; ++i) reply[14 + i] = local_ip[i];
    for (int i = 0; i < 6; ++i) reply[18 + i] = sender_mac[i];
    for (int i = 0; i < 4; ++i) reply[24 + i] = sender_ip[i];
    (void)ethernet_send(sender_mac, 0x0806, reply, sizeof reply);
}

static int poll_frame(void) {
    int length = ne2k_receive(rx_frame, sizeof rx_frame);
    if (length <= 0) return 0;
    if (length >= 14 && read_be16(rx_frame + 12) == 0x0806)
        process_arp(rx_frame, length);
    return length;
}

static int arp_resolve(const u8 *ip, u8 mac[6]) {
    if (cached_arp_valid && ip_equal(ip, cached_arp_ip)) {
        for (int i = 0; i < 6; ++i) mac[i] = cached_arp_mac[i];
        return 1;
    }
    u8 request[28], broadcast[6] = {255,255,255,255,255,255};
    write_be16(request, 1); write_be16(request + 2, 0x0800);
    request[4] = 6; request[5] = 4; write_be16(request + 6, 1);
    for (int i = 0; i < 6; ++i) request[8 + i] = mac_address[i];
    for (int i = 0; i < 4; ++i) request[14 + i] = local_ip[i];
    for (int i = 0; i < 6; ++i) request[18 + i] = 0;
    for (int i = 0; i < 4; ++i) request[24 + i] = ip[i];
    if (!ethernet_send(broadcast, 0x0806, request, sizeof request)) return 0;
    for (u32 wait = 0; wait < 300000; ++wait) {
        (void)poll_frame();
        if (cached_arp_valid && ip_equal(ip, cached_arp_ip)) {
            for (int i = 0; i < 6; ++i) mac[i] = cached_arp_mac[i];
            return 1;
        }
        if (ip_equal(ip, local_ip)) {
            for (int i = 0; i < 6; ++i) mac[i] = mac_address[i];
            return 1;
        }
    }
    return 0;
}

static int ip_send(const u8 *destination, u8 protocol, const u8 *payload, u16 length) {
    if ((u32)length + 20 > sizeof ip_packet) return 0;
    u8 next_mac[6];
    const u8 *next_ip = destination;
    if (destination[0] != 10 || destination[1] != 0 || destination[2] != 2)
        next_ip = gateway_ip;
    if (!arp_resolve(next_ip, next_mac)) return 0;
    u8 *h = ip_packet;
    h[0] = 0x45; h[1] = 0;
    write_be16(h + 2, length + 20);
    write_be16(h + 4, (u16)ip_id++);
    write_be16(h + 6, 0x4000);
    h[8] = 64; h[9] = protocol;
    h[10] = h[11] = 0;
    for (int i = 0; i < 4; ++i) { h[12 + i] = local_ip[i]; h[16 + i] = destination[i]; }
    write_be16(h + 10, checksum(h, 20));
    for (u16 i = 0; i < length; ++i) h[20 + i] = payload[i];
    return ethernet_send(next_mac, 0x0800, h, length + 20);
}

static int udp_send(const u8 *destination, u16 source_port, u16 dest_port,
                    const u8 *payload, u16 length) {
    u8 packet[1200];
    if (length > sizeof packet - 8) return 0;
    write_be16(packet, source_port); write_be16(packet + 2, dest_port);
    write_be16(packet + 4, length + 8); write_be16(packet + 6, 0);
    for (u16 i = 0; i < length; ++i) packet[8 + i] = payload[i];
    return ip_send(destination, 17, packet, length + 8);
}

static void process_udp(const u8 *packet, u16 length, const u8 *source_ip) {
    if (length < 8) return;
    u16 src_port = read_be16(packet), dst_port = read_be16(packet + 2);
    u16 data_length = read_be16(packet + 4);
    if (data_length < 8 || data_length > length) return;
    if (dst_port == 53000 && src_port == 53 && ip_equal(source_ip, dns_ip)) {
        dns_reply_length = data_length - 8;
        if (dns_reply_length <= (int)sizeof dns_reply)
            for (int i = 0; i < dns_reply_length; ++i) dns_reply[i] = packet[8 + i];
    }
}

static int poll_ip(void) {
    int length = poll_frame();
    if (length < 34 || read_be16(rx_frame + 12) != 0x0800) return length;
    const u8 *ip = rx_frame + 14;
    int header_length = (ip[0] & 15) * 4;
    u16 total = read_be16(ip + 2);
    if ((ip[0] >> 4) != 4 || header_length < 20 || total < header_length ||
        total > length - 14 || !ip_equal(ip + 16, local_ip)) return length;
    if (ip[9] == 17) process_udp(ip + header_length, total - header_length, ip + 12);
    return length;
}

static int dns_read_name(const u8 *packet, int length, int *offset, char *out, int capacity) {
    int pos = *offset, out_len = 0, jumped = 0, resume = 0;
    for (int steps = 0; steps < 64 && pos < length; ++steps) {
        u8 size = packet[pos++];
        if (!size) { if (!jumped) *offset = pos; else *offset = resume; out[out_len] = 0; return 1; }
        if ((size & 0xc0) == 0xc0) {
            if (pos >= length) return 0;
            int target = ((size & 0x3f) << 8) | packet[pos++];
            if (!jumped) { resume = pos; jumped = 1; }
            pos = target;
            continue;
        }
        if (size > 63 || pos + size > length) return 0;
        if (out_len && out_len < capacity - 1) out[out_len++] = '.';
        for (int i = 0; i < size; ++i) {
            if (out_len >= capacity - 1) return 0;
            out[out_len++] = (char)packet[pos++];
        }
    }
    return 0;
}

static int dns_lookup(const char *host, u8 result[4]) {
    u8 query[256];
    int length = 12;
    query[0] = 0x42; query[1] = 0x31; query[2] = 1; query[3] = 0;
    query[4] = 0; query[5] = 1; query[6] = query[7] = query[8] = query[9] = query[10] = query[11] = 0;
    int i = 0;
    while (host[i]) {
        int start = i;
        while (host[i] && host[i] != '.') ++i;
        int n = i - start;
        if (!n || n > 63 || length + n + 6 >= (int)sizeof query) return 0;
        query[length++] = (u8)n;
        for (int j = start; j < i; ++j) {
            char c = host[j];
            query[length++] = (u8)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        if (host[i] == '.') ++i;
    }
    query[length++] = 0;
    write_be16(query + length, 1); length += 2;
    write_be16(query + length, 1); length += 2;
    dns_reply_length = 0;
    if (!udp_send(dns_ip, 53000, 53, query, length)) return 0;
    for (u32 wait = 0; wait < 800000 && !dns_reply_length; ++wait) (void)poll_ip();
    if (dns_reply_length < 12 || dns_reply[0] != 0x42 || dns_reply[1] != 0x31 ||
        !(dns_reply[3] & 0x80)) return 0;
    u16 questions = read_be16(dns_reply + 4), answers = read_be16(dns_reply + 6);
    int pos = 12;
    char name[128];
    while (questions--) {
        if (!dns_read_name(dns_reply, dns_reply_length, &pos, name, sizeof name) || pos + 4 > dns_reply_length) return 0;
        pos += 4;
    }
    while (answers-- && pos < dns_reply_length) {
        if (!dns_read_name(dns_reply, dns_reply_length, &pos, name, sizeof name) || pos + 10 > dns_reply_length) return 0;
        u16 type = read_be16(dns_reply + pos), klass = read_be16(dns_reply + pos + 2);
        u16 data_len = read_be16(dns_reply + pos + 8);
        pos += 10;
        if (pos + data_len > dns_reply_length) return 0;
        if (type == 1 && klass == 1 && data_len == 4) {
            for (int j = 0; j < 4; ++j) result[j] = dns_reply[pos + j];
            return 1;
        }
        pos += data_len;
    }
    return 0;
}

static int receive_tcp(const u8 *remote_ip, u16 local_port, u16 remote_port,
                       u32 *sequence, u8 *flags, const u8 **payload, u16 *payload_length) {
    int frame_length = poll_frame();
    if (frame_length < 34 || read_be16(rx_frame + 12) != 0x0800) return 0;
    const u8 *ip = rx_frame + 14;
    int ihl = (ip[0] & 15) * 4;
    u16 ip_length = read_be16(ip + 2);
    if ((ip[0] >> 4) != 4 || ihl < 20 || ip_length > frame_length - 14) return 0;
    if (ip[9] == 1 && ip_length >= ihl + 8 + 28 && ip_equal(ip + 16, local_ip)) {
        const u8 *icmp = ip + ihl;
        const u8 *original = icmp + 8;
        if (icmp[0] == 3 && (original[0] >> 4) == 4 && original[9] == 6 &&
            ip_equal(original + 12, local_ip) && ip_equal(original + 16, remote_ip)) {
            int old_ihl = (original[0] & 15) * 4;
            const u8 *old_tcp = original + old_ihl;
            if (read_be16(old_tcp) == local_port && read_be16(old_tcp + 2) == remote_port)
                return icmp[1] == 3 ? -1 : -2;
        }
        return 0;
    }
    if (ip[9] != 6 || ip_length < ihl + 20 ||
        !ip_equal(ip + 12, remote_ip) || !ip_equal(ip + 16, local_ip)) return 0;
    const u8 *tcp = ip + ihl;
    if (read_be16(tcp) != remote_port || read_be16(tcp + 2) != local_port) return 0;
    int thl = (tcp[12] >> 4) * 4;
    if (thl < 20 || ip_length < ihl + thl) return 0;
    *sequence = read_be32(tcp + 4);
    *flags = tcp[13];
    *payload = tcp + thl;
    *payload_length = ip_length - ihl - thl;
    return 1;
}

static int tcp_send_segment(const u8 *remote_ip, u16 local_port, u16 remote_port,
                            u32 sequence, u32 acknowledgement, u8 flags,
                            const u8 *payload, u16 payload_length) {
    u8 segment[1400];
    if ((u32)payload_length + 20 > sizeof segment) return 0;
    write_be16(segment, local_port); write_be16(segment + 2, remote_port);
    write_be32(segment + 4, sequence); write_be32(segment + 8, acknowledgement);
    segment[12] = 0x50; segment[13] = flags;
    write_be16(segment + 14, 4096); write_be16(segment + 16, 0); write_be16(segment + 18, 0);
    for (u16 i = 0; i < payload_length; ++i) segment[20 + i] = payload[i];
    write_be16(segment + 16, tcp_checksum(local_ip, remote_ip, segment, payload_length + 20));
    return ip_send(remote_ip, 6, segment, payload_length + 20);
}

int net_tcp_request(const char *host, u16 port, const u8 *request, u16 request_length,
                    u8 *response, u16 capacity) {
    if (!ne_io || !host || !request || !request_length || !response || capacity < 64) return -1;
    u8 remote_ip[4];
    if (!parse_ipv4(host, remote_ip) && !dns_lookup(host, remote_ip)) return -2;
    u16 local_port = next_tcp_port++;
    if (next_tcp_port < 0xc350) next_tcp_port = 0xc350;
    u32 seq = 0x193a7c21u + ip_id, acknowledgement = 0;
    if (!tcp_send_segment(remote_ip, local_port, port, seq, 0, 0x02, 0, 0)) return -3;
    u32 remote_seq = 0;
    u8 flags = 0;
    const u8 *payload;
    u16 payload_length;
    int connected = 0;
    for (u32 wait = 0; wait < 800000; ++wait) {
        int received = receive_tcp(remote_ip, local_port, port, &remote_seq, &flags, &payload, &payload_length);
        if (received == -1) return -4;
        if (received < 0) return -9;
        if (!received) continue;
        if (flags & 0x04) return -4;
        if ((flags & 0x12) == 0x12) {
            acknowledgement = remote_seq + 1;
            ++seq;
            connected = 1;
            break;
        }
    }
    if (!connected) return -5;
    if (!tcp_send_segment(remote_ip, local_port, port, seq, acknowledgement, 0x10, 0, 0)) return -6;

    if (!tcp_send_segment(remote_ip, local_port, port, seq, acknowledgement, 0x18,
                          request, request_length)) return -8;
    seq += (u32)request_length;

    int used = 0, saw_fin = 0;
    for (u32 wait = 0; wait < 1200000 && !saw_fin; ++wait) {
        int received_packet = receive_tcp(remote_ip, local_port, port, &remote_seq, &flags, &payload, &payload_length);
        if (received_packet < 0) return used ? used : (received_packet == -1 ? -4 : -9);
        if (!received_packet) continue;
        if (flags & 0x04) break;
        if (payload_length && remote_seq == acknowledgement) {
            u16 copy = payload_length;
            if (copy > capacity - used) copy = capacity - used;
            for (u16 i = 0; i < copy; ++i) response[used + i] = payload[i];
            used += copy;
            acknowledgement += payload_length;
        }
        if (flags & 0x01) { ++acknowledgement; saw_fin = 1; }
        if (payload_length || (flags & 0x01))
            (void)tcp_send_segment(remote_ip, local_port, port, seq, acknowledgement, 0x10, 0, 0);
        if (used >= capacity) break;
    }
    return used;
}

static inline void outb(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(u16 port, u16 value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline u16 inw(u16 port) {
    u16 value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(u16 port, u32 value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline u32 inl(u16 port) {
    u32 value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static u32 pci_read(u8 device, u8 offset) {
    u32 address = 0x80000000u | ((u32)device << 11) | (offset & 0xfcu);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write(u8 device, u8 offset, u32 value) {
    u32 address = 0x80000000u | ((u32)device << 11) | (offset & 0xfcu);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static int find_ne2k(void) {
    for (u8 device = 0; device < 32; ++device) {
        u32 id = pci_read(device, 0);
        if ((id & 0xffffu) == 0x10ecu && (id >> 16) == 0x8029u) {
            u32 bar = pci_read(device, 0x10);
            if (!(bar & 1u)) return 0;
            ne_io = (u16)(bar & 0xfffcu);
            u32 command = pci_read(device, 4);
            pci_write(device, 4, (command & 0xffff0000u) | ((command & 0xffffu) | 1u));
            return ne_io != 0;
        }
    }
    return 0;
}

static int dma_ready(void) {
    for (u32 n = 0; n < 100000; ++n)
        if (inb(ne_io + 7) & 0x40) return 1;
    return 0;
}

static int dma_read(u16 address, u8 *out, u16 length) {
    if (!length) return 1;
    outb(ne_io, 0x22);
    outb(ne_io + 8, (u8)address);
    outb(ne_io + 9, (u8)(address >> 8));
    outb(ne_io + 10, (u8)length);
    outb(ne_io + 11, (u8)(length >> 8));
    outb(ne_io, 0x0a);
    for (u16 i = 0; i < length; i += 2) {
        u16 word = inw(ne_io + 0x10);
        out[i] = (u8)word;
        if (i + 1 < length) out[i + 1] = (u8)(word >> 8);
    }
    outb(ne_io, 0x22);
    if (!dma_ready()) return 0;
    outb(ne_io + 7, 0x40);
    return 1;
}

static int dma_write(u16 address, const u8 *data, u16 length) {
    if (!length) return 1;
    outb(ne_io, 0x22);
    outb(ne_io + 8, (u8)address);
    outb(ne_io + 9, (u8)(address >> 8));
    outb(ne_io + 10, (u8)length);
    outb(ne_io + 11, (u8)(length >> 8));
    outb(ne_io, 0x12);
    for (u16 i = 0; i < length; i += 2) {
        u16 word = data[i];
        if (i + 1 < length) word |= (u16)data[i + 1] << 8;
        outw(ne_io + 0x10, word);
    }
    outb(ne_io, 0x22);
    if (!dma_ready()) return 0;
    outb(ne_io + 7, 0x40);
    return 1;
}

int ne2k_init(void) {
    if (!find_ne2k()) return 0;
    u8 reset = inb(ne_io + 0x1f);
    outb(ne_io + 0x1f, reset);
    for (u32 n = 0; n < 100000; ++n)
        if (inb(ne_io + 7) & 0x80) break;
    outb(ne_io, 0x21);
    /* The NE2000 PROM stores each MAC byte in every other byte. */
    outb(ne_io + 0x0e, 0x48); /* 8-bit remote DMA for PROM */
    outb(ne_io + 0x0a, 32);
    outb(ne_io + 0x0b, 0);
    outb(ne_io + 8, 0);
    outb(ne_io + 9, 0);
    outb(ne_io, 0x0a);
    u8 prom[32];
    for (int i = 0; i < 32; ++i) prom[i] = inb(ne_io + 0x10);
    for (int i = 0; i < 6; ++i) mac_address[i] = prom[i * 2];

    outb(ne_io, 0x21);
    outb(ne_io + 0x0e, 0x49); /* 16-bit remote DMA for packet buffers */
    outb(ne_io + 0x0a, 0);
    outb(ne_io + 0x0b, 0);
    outb(ne_io + 0x0c, 0x20);
    outb(ne_io + 0x0d, 0x02);
    outb(ne_io + 4, NE_TX_PAGE);
    outb(ne_io + 1, NE_RX_START);
    outb(ne_io + 2, NE_RX_STOP);
    outb(ne_io + 3, NE_RX_START);
    outb(ne_io + 7, 0xff);
    outb(ne_io + 0x0f, 0);
    outb(ne_io, 0x61);
    for (int i = 0; i < 6; ++i) {
        outb(ne_io + 1 + i, mac_address[i]);
    }
    for (int i = 0; i < 8; ++i) outb(ne_io + 8 + i, 0xff);
    outb(ne_io + 7, NE_RX_FIRST);
    outb(ne_io, 0x22);
    outb(ne_io + 3, NE_RX_START);
    outb(ne_io + 0x0d, 0);
    outb(ne_io + 0x0c, 0x05);
    outb(ne_io + 7, 0xff);
    rx_page = NE_RX_FIRST;
    return 1;
}

int ne2k_available(void) { return ne_io != 0; }

int ne2k_send(const u8 *frame, u16 length) {
    if (!ne_io || length < 14 || length > 1514) return 0;
    outb(ne_io + 7, 0x0a);
    if (!dma_write((u16)(NE_TX_PAGE << 8), frame, length)) return 0;
    outb(ne_io + 4, NE_TX_PAGE);
    outb(ne_io + 5, (u8)length);
    outb(ne_io + 6, (u8)(length >> 8));
    outb(ne_io, 0x26);
    for (u32 n = 0; n < 1000000; ++n) {
        u8 status = inb(ne_io + 7);
        if (status & 0x0a) {
            outb(ne_io + 7, status & 0x0a);
            return (status & 0x02) != 0;
        }
    }
    return 0;
}

static int ring_read(u16 address, u8 *out, u16 length) {
    u16 ring_start = (u16)NE_RX_START << 8;
    u16 ring_end = (u16)NE_RX_STOP << 8;
    while (length) {
        u16 part = (u16)(ring_end - address);
        if (part > length) part = length;
        if (!dma_read(address, out, part)) return 0;
        out += part;
        length -= part;
        address = ring_start;
    }
    return 1;
}

int ne2k_receive(u8 *frame, u16 capacity) {
    if (!ne_io) return 0;
    u8 status = inb(ne_io + 7);
    if (status & 0x10) {
        outb(ne_io, 0x22);
        outb(ne_io + 7, 0x10);
    }
    if (!(status & 1)) return 0;
    outb(ne_io, 0x62);
    u8 current = inb(ne_io + 7);
    outb(ne_io, 0x22);
    if (rx_page == current) return 0;
    u8 header[4];
    if (!ring_read((u16)rx_page << 8, header, 4)) return 0;
    u8 next = header[1];
    u16 length = (u16)header[2] | ((u16)header[3] << 8);
    if ((header[0] & 1) && length >= 4 && length - 4 <= capacity && next >= NE_RX_FIRST && next < NE_RX_STOP) {
        if (!ring_read(((u16)rx_page << 8) + 4, frame, length - 4)) return 0;
        length -= 4;
    } else length = 0;
    rx_page = next;
    outb(ne_io + 3, rx_page == NE_RX_START ? NE_RX_STOP - 1 : rx_page - 1);
    return length;
}

const u8 *ne2k_mac(void) { return mac_address; }
