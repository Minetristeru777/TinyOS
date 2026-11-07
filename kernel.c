typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned int u32;

#define VGA ((volatile u8 *)0xA0000)
#define FRAME_SIZE 64000
#define FS_SIZE 65536
#define FS_SECTOR_SIZE 512
#define FS_SECTORS (FS_SIZE / FS_SECTOR_SIZE)
#define FS_META_SIZE 1024
#define FS_ENTRIES 16
#define FS_ENTRY_SIZE 32
#define FS_ENTRY_BASE 32
#define FS_NAME_SIZE 20
#define FS_ENTRY_SIZE_OFF 20
#define FS_ENTRY_FIRST_OFF 24
#define FS_ENTRY_FLAGS_OFF 26
#define FS_ENTRY_USED 1
#define FS_FAT_OFF (FS_ENTRY_BASE + FS_ENTRIES * FS_ENTRY_SIZE)
#define FS_BLOCK_SIZE 512
#define FS_BLOCKS ((FS_SIZE - FS_META_SIZE) / FS_BLOCK_SIZE)
#define FS_DATA_START FS_META_SIZE
#define FS_FAT_FREE 0xff
#define FS_FAT_END 0xfe
#define FS_MAX_FILE_SIZE (FS_BLOCKS * FS_BLOCK_SIZE)
#define LEGACY_FS_SIZE 4096
#define LEGACY_ENTRY_COUNT 15
#define LEGACY_ENTRY_SIZE 256
#define LEGACY_ENTRY_BASE 16
#define LEGACY_NAME_SIZE 24
#define LEGACY_TYPE_OFF 24
#define LEGACY_LENGTH_OFF 25
#define LEGACY_DATA_OFF 28
#define EDIT_BUFFER_SIZE (FS_MAX_FILE_SIZE + 1)
#define TERM_BUFFER_SIZE 40
#define TERM_HISTORY_SIZE 8
#define TERM_OUTPUT_SIZE 32

static u8 frame_buffer[FRAME_SIZE] __attribute__((aligned(4)));
static u8 presented_frame[FRAME_SIZE];
static int framebuffer_force_redraw;
static u8 *const video = frame_buffer;
static u32 linear_framebuffer;
static u16 framebuffer_pitch;
static u8 framebuffer_bpp;
static int cursor_x = 150;
static int cursor_y = 100;
static int files_open;
static int notes_open;
static int terminal_open;
static int browser_open;
static int settings_open;
static int bsod_mode;
static int selected_entry = -1;
static int file_scroll_index;
static int file_dialog;
static int file_dialog_len;
static int fs_error;
static int fs_loaded;
static int active_disk;
static int editing_entry = -1;
static int editor_len;
static int editor_cursor;
static int editor_scroll_line;
static int editor_from_files;
static int editor_saved;
static int terminal_len;
static char terminal_history[TERM_HISTORY_SIZE][TERM_BUFFER_SIZE];
static int terminal_history_count;
static int terminal_history_pos = -1;
static char terminal_output[TERM_OUTPUT_SIZE][TERM_BUFFER_SIZE];
static int terminal_output_count;
static int terminal_output_scroll;
static int start_open;
static int start_selected;
static int active_window;
static int file_x = 24, file_y = 20;
static int notes_x = 42, notes_y = 20;
static int terminal_x = 40, terminal_y = 26;
static int browser_x = 10, browser_y = 10;
static int settings_x = 50, settings_y = 26;
static int file_w = 268, file_h = 158;
static int notes_w = 240, notes_h = 158;
static int terminal_w = 244, terminal_h = 148;
static int browser_w = 300, browser_h = 170;
static int settings_w = 220, settings_h = 142;
static int app_minimized[5];
static int app_maximized[5];
static int restore_x[5], restore_y[5], restore_w[5], restore_h[5];
static int dragging_window = -1;
static int drag_offset_x, drag_offset_y;
static int context_open;
static int context_x, context_y;
static int mouse_buttons;
static int mouse_packet_index;
static int mouse_packet_size = 3;
static int ui_dirty = 1;
static u8 mouse_packet[4];
static u8 fs[FS_SIZE];
static u8 legacy_fs[LEGACY_FS_SIZE];
static char editor_buffer[EDIT_BUFFER_SIZE];
static char file_dialog_name[FS_NAME_SIZE];
static char terminal_line[TERM_BUFFER_SIZE];
static char terminal_message[TERM_BUFFER_SIZE];
static u8 repo_response[FS_MAX_FILE_SIZE + 64];
static char browser_url[96];
static int browser_url_len;
static u8 browser_response[8192];
static int browser_response_len;
static char browser_text[4096];
static u8 browser_text_colors[4096];
static int browser_text_len;
static int browser_scroll;
static int browser_address_active;
static char browser_status[TERM_BUFFER_SIZE];
static u8 browser_bg_color = 15;

extern u8 __bss_start;
extern u8 __bss_end;
extern int ne2k_init(void);
extern int net_tcp_request(const char *host, u16 port, const u8 *request, u16 request_length,
                           u8 *response, u16 capacity);
static int net_ready;
static int wallpaper_id;
static char clock_time[6] = "00:00";
static char clock_date[6] = "01/01";
static u8 rtc_last_second = 0xff;
static u32 rtc_poll_count;

static void bytes_zero(u8 *p, int count);
static void bytes_copy(u8 *to, const u8 *from, int count);
static void select_next(int direction);
static void switch_task(void);
static int fs_save(void);
static int fs_read_file(int index, char *out, u32 capacity, u32 *length_out);
static int fs_write_file(int index, const char *data, u32 length);
static int fs_find_name(const char *name);
static int fs_create_named(const char *name);
static int terminal_get_arg(char *out, int capacity);
static void terminal_set_message(const char *s);
static void terminal_show_data(const u8 *data, int length);
static void terminal_output_push(const char *line);

static int repo_get(const char *path, u8 *out, u16 capacity) {
    char request[96];
    int n = 0;
    const char *prefix = "TINY/1 GET ";
    for (int i = 0; prefix[i]; ++i) request[n++] = prefix[i];
    for (int i = 0; path[i] && n < (int)sizeof request - 2; ++i) request[n++] = path[i];
    request[n++] = '\n';
    int received = net_tcp_request("10.0.2.2", 8080, (const u8 *)request, (u16)n,
                                   repo_response, sizeof repo_response);
    if (received < 0) return -1;
    int header_end = 0;
    while (header_end < received && repo_response[header_end] != '\n') ++header_end;
    if (header_end < 12 || repo_response[0] != 'T' || repo_response[7] != '2' ||
        repo_response[8] != '0' || repo_response[9] != '0' || repo_response[10] != ' ') return -1;
    u32 length = 0;
    for (int i = 11; i < header_end; ++i) {
        if (repo_response[i] < '0' || repo_response[i] > '9') return -1;
        length = length * 10 + repo_response[i] - '0';
    }
    int body = header_end + 1;
    if (length != (u32)(received - body) || length > capacity) return -1;
    bytes_copy(out, repo_response + body, (int)length);
    return (int)length;
}

static int text_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}

static const char *json_string_after(const char *json, const char *key) {
    int key_len = 0;
    while (key[key_len]) ++key_len;
    for (int i = 0; json[i]; ++i) {
        int j = 0;
        while (j < key_len && json[i + j] == key[j]) ++j;
        if (j == key_len) {
            const char *p = json + i + key_len;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') ++p;
            if (*p == '"') return p + 1;
        }
    }
    return 0;
}

static void terminal_pkg(void) {
    char arg[FS_NAME_SIZE], action[FS_NAME_SIZE];
    if (!terminal_get_arg(action, sizeof action)) {
        terminal_set_message("PKG LIST OR INSTALL NAME");
        return;
    }
    char manifest[2048];
    int length = repo_get("/repo.json", (u8 *)manifest, sizeof manifest - 1);
    if (length < 0) { terminal_set_message("REPO CONNECT FAILED"); return; }
    manifest[length] = 0;
    if (text_equal(action, "LIST")) {
        const char *p = manifest;
        while ((p = json_string_after(p, "\"name\"")) != 0) {
            char package[FS_NAME_SIZE];
            int out = 0;
            while (*p && *p != '"' && out < FS_NAME_SIZE - 1) package[out++] = *p++;
            package[out] = 0;
            terminal_output_push(package);
            if (!*p) break;
        }
        terminal_set_message("REPOSITORY PACKAGES");
        return;
    }
    int arg_at = 0;
    while (terminal_line[arg_at] && terminal_line[arg_at] != ' ') ++arg_at;
    while (terminal_line[arg_at] == ' ') ++arg_at;
    while (terminal_line[arg_at] && terminal_line[arg_at] != ' ') ++arg_at;
    while (terminal_line[arg_at] == ' ') ++arg_at;
    int arg_len = 0;
    while (terminal_line[arg_at] && terminal_line[arg_at] != ' ' && arg_len < FS_NAME_SIZE - 1)
        arg[arg_len++] = terminal_line[arg_at++];
    arg[arg_len] = 0;
    if (!text_equal(action, "INSTALL") || !arg_len) {
        terminal_set_message("PKG LIST OR INSTALL NAME");
        return;
    }
    const char *p = manifest;
    char filename[FS_NAME_SIZE] = {0};
    int found = 0;
    while ((p = json_string_after(p, "\"name\"")) != 0) {
        char package[FS_NAME_SIZE];
        int i = 0;
        while (*p && *p != '"' && i < FS_NAME_SIZE - 1) package[i++] = *p++;
        package[i] = 0;
        const char *next = json_string_after(p, "\"file\"");
        if (text_equal(package, arg) && next) {
            i = 0;
            while (next[i] && next[i] != '"' && i < FS_NAME_SIZE - 1) {
                filename[i] = next[i];
                ++i;
            }
            filename[i] = 0;
            found = i > 0;
            break;
        }
        if (!*p) break;
    }
    if (!found) { terminal_set_message("PACKAGE NOT FOUND"); return; }
    char path[FS_NAME_SIZE + 2];
    path[0] = '/';
    int i = 0;
    while (filename[i]) { path[i + 1] = filename[i]; ++i; }
    path[i + 1] = 0;
    int bytes = repo_get(path, repo_response, sizeof repo_response);
    if (bytes < 0) { terminal_set_message("PACKAGE DOWNLOAD FAILED"); return; }
    int index = fs_find_name(filename);
    if (index < 0) index = fs_create_named(filename);
    terminal_set_message(index >= 0 && fs_write_file(index, (const char *)repo_response, (u32)bytes)
                         ? "PACKAGE INSTALLED" : "INSTALL FAILED");
}

static void bce_put_u32(u8 *out, int *length, int value) {
    out[(*length)++] = (u8)value;
    out[(*length)++] = (u8)(value >> 8);
    out[(*length)++] = (u8)(value >> 16);
    out[(*length)++] = (u8)(value >> 24);
}

static int terminal_compile_tbc(void) {
    char source_name[FS_NAME_SIZE];
    if (!terminal_get_arg(source_name, sizeof source_name)) {
        terminal_set_message("COMP NAME.TBC");
        return 0;
    }
    int source_index = fs_find_name(source_name);
    static char source[2048];
    u32 source_length = 0;
    if (source_index < 0 || !fs_read_file(source_index, source, sizeof source, &source_length)) {
        terminal_set_message("SOURCE NOT FOUND OR TOO BIG");
        return 0;
    }
    static u8 output[4096];
    int output_length = 0;
    output[output_length++] = 'B'; output[output_length++] = 'C';
    output[output_length++] = 'E'; output[output_length++] = '1';
    int pos = 0;
    while (pos < (int)source_length) {
        char line[128];
        int n = 0;
        while (pos < (int)source_length && source[pos] != '\n' && n < (int)sizeof line - 1)
            line[n++] = source[pos++];
        if (pos < (int)source_length && source[pos] == '\n') ++pos;
        line[n] = 0;
        int at = 0, operation = 0;
        if (n >= 6 && line[0] == 'P' && line[1] == 'R' && line[2] == 'I' &&
            line[3] == 'N' && line[4] == 'T' && line[5] == ' ') { operation = 1; at = 6; }
        else if (n >= 4 && line[0] == 'L' && line[1] == 'E' && line[2] == 'T' && line[3] == ' ') { operation = 2; at = 4; }
        else if (n >= 4 && line[0] == 'A' && line[1] == 'D' && line[2] == 'D' && line[3] == ' ') { operation = 3; at = 4; }
        else if (n >= 4 && line[0] == 'S' && line[1] == 'U' && line[2] == 'B' && line[3] == ' ') { operation = 4; at = 4; }
        else if (n >= 5 && line[0] == 'F' && line[1] == 'I' && line[2] == 'L' && line[3] == 'E' && line[4] == ' ') { operation = 6; at = 5; }
        else if (n >= 6 && line[0] == 'F' && line[1] == 'E' && line[2] == 'T' && line[3] == 'C' && line[4] == 'H' && line[5] == ' ') { operation = 7; at = 6; }
        else if (!n) continue;
        else { terminal_set_message("COMP ERROR: BAD STATEMENT"); return 0; }
        while (at < n && line[at] == ' ') ++at;
        if (operation == 6 || operation == 7) {
            if (at >= n || line[at++] != '"') { terminal_set_message("COMP ERROR: USE QUOTED PATH"); return 0; }
            int start = at;
            while (at < n && line[at] != '"') ++at;
            int count = at - start;
            if (!count || count > 63 || output_length + count + 2 > (int)sizeof output) {
                terminal_set_message("COMP ERROR: PATH TOO LONG"); return 0;
            }
            output[output_length++] = (u8)operation;
            output[output_length++] = (u8)count;
            for (int i = 0; i < count; ++i) output[output_length++] = (u8)line[start + i];
        } else if (operation == 1) {
            if (at < n && line[at] == '"') {
                ++at;
                int start = at;
                while (at < n && line[at] != '"') ++at;
                int count = at - start;
                if (count > 255 || output_length + count + 2 > (int)sizeof output) {
                    terminal_set_message("COMP ERROR: STRING TOO LONG"); return 0;
                }
                output[output_length++] = 1;
                output[output_length++] = (u8)count;
                for (int i = 0; i < count; ++i) output[output_length++] = (u8)line[start + i];
            } else if (at < n && line[at] >= 'A' && line[at] <= 'Z') {
                output[output_length++] = 5;
                output[output_length++] = (u8)(line[at] - 'A');
            } else { terminal_set_message("COMP ERROR: PRINT TEXT OR VAR"); return 0; }
        } else {
            if (at >= n || line[at] < 'A' || line[at] > 'Z') {
                terminal_set_message("COMP ERROR: VARIABLE A-Z"); return 0;
            }
            int variable = line[at++] - 'A';
            while (at < n && line[at] == ' ') ++at;
            if (operation == 2) {
                if (at >= n || line[at++] != '=') { terminal_set_message("COMP ERROR: LET A = N"); return 0; }
                while (at < n && line[at] == ' ') ++at;
            }
            int sign = 1;
            if (at < n && line[at] == '-') { sign = -1; ++at; }
            int value = 0, digits = 0;
            while (at < n && line[at] >= '0' && line[at] <= '9') {
                if (value < 10000000) value = value * 10 + line[at] - '0';
                ++at; ++digits;
            }
            if (!digits || output_length + 6 > (int)sizeof output) {
                terminal_set_message("COMP ERROR: EXPECTED NUMBER"); return 0;
            }
            output[output_length++] = (u8)operation;
            output[output_length++] = (u8)variable;
            bce_put_u32(output, &output_length, value * sign);
        }
    }
    output[output_length++] = 0;
    int source_len = 0;
    while (source_name[source_len]) ++source_len;
    if (source_len >= 4 && source_name[source_len - 4] == '.' && source_name[source_len - 3] == 'T' &&
        source_name[source_len - 2] == 'B' && source_name[source_len - 1] == 'C') {
        source_name[source_len - 3] = 'B';
        source_name[source_len - 2] = 'C';
        source_name[source_len - 1] = 'E';
    } else {
        if (source_len + 4 >= FS_NAME_SIZE) { terminal_set_message("BCE NAME TOO LONG"); return 0; }
        source_name[source_len++] = '.'; source_name[source_len++] = 'B';
        source_name[source_len++] = 'C'; source_name[source_len++] = 'E'; source_name[source_len] = 0;
    }
    int output_index = fs_find_name(source_name);
    if (output_index < 0) output_index = fs_create_named(source_name);
    if (output_index < 0 || !fs_write_file(output_index, (const char *)output, (u32)output_length)) {
        terminal_set_message("COMPILE SAVE FAILED"); return 0;
    }
    terminal_set_message("COMPILED TO BCE");
    return 1;
}

static int bce_get_u32(const u8 *program, int *position, int length, int *value) {
    if (*position + 4 > length) return 0;
    *value = (int)((u32)program[*position] | ((u32)program[*position + 1] << 8) |
                   ((u32)program[*position + 2] << 16) | ((u32)program[*position + 3] << 24));
    *position += 4;
    return 1;
}

static void bce_append_number(int *out, int value) {
    char digits[12];
    int count = 0;
    if (value < 0) { if (*out < TERM_BUFFER_SIZE - 1) terminal_message[(*out)++] = '-'; value = -value; }
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < 10);
    while (count && *out < TERM_BUFFER_SIZE - 1) terminal_message[(*out)++] = digits[--count];
}

static void terminal_run_bce(const u8 *program, int length) {
    int variables[26];
    for (int i = 0; i < 26; ++i) variables[i] = 0;
    int pos = 4, out = 0;
    while (pos < length) {
        u8 op = program[pos++];
        if (!op) break;
        if (op == 1) {
            if (pos >= length) { terminal_set_message("BCE FILE CORRUPT"); return; }
            int count = program[pos++];
            if (pos + count > length) { terminal_set_message("BCE FILE CORRUPT"); return; }
            if (out && out < TERM_BUFFER_SIZE - 1) terminal_message[out++] = '|';
            for (int i = 0; i < count && out < TERM_BUFFER_SIZE - 1; ++i) terminal_message[out++] = (char)program[pos + i];
            pos += count;
        } else if (op == 5) {
            if (pos >= length || program[pos] >= 26) { terminal_set_message("BCE FILE CORRUPT"); return; }
            if (out && out < TERM_BUFFER_SIZE - 1) terminal_message[out++] = '|';
            bce_append_number(&out, variables[program[pos++]]);
        } else if (op >= 2 && op <= 4) {
            if (pos >= length || program[pos] >= 26) { terminal_set_message("BCE FILE CORRUPT"); return; }
            int variable = program[pos++], value;
            if (!bce_get_u32(program, &pos, length, &value)) { terminal_set_message("BCE FILE CORRUPT"); return; }
            if (op == 2) variables[variable] = value;
            else if (op == 3) variables[variable] += value;
            else variables[variable] -= value;
        } else if (op == 6 || op == 7) {
            if (pos >= length) { terminal_set_message("BCE FILE CORRUPT"); return; }
            int count = program[pos++];
            if (!count || pos + count > length || count >= 64) { terminal_set_message("BCE FILE CORRUPT"); return; }
            char path[64];
            for (int i = 0; i < count; ++i) path[i] = (char)program[pos++];
            path[count] = 0;
            static u8 data[4096];
            int bytes = -1;
            if (op == 6) {
                int index = fs_find_name(path);
                u32 file_length = 0;
                if (index >= 0 && fs_read_file(index, (char *)data, sizeof data, &file_length)) bytes = (int)file_length;
            } else bytes = repo_get(path, data, sizeof data);
            if (bytes < 0) { terminal_set_message(op == 6 ? "BCE FILE MISSING" : "BCE FETCH FAILED"); return; }
            terminal_show_data(data, bytes);
        } else { terminal_set_message("BCE OPCODE ERROR"); return; }
    }
    terminal_message[out] = 0;
    if (!out) terminal_set_message("BCE PROGRAM OK");
}

static void terminal_run_tbc(void) {
    char filename[FS_NAME_SIZE];
    if (!terminal_get_arg(filename, sizeof filename)) { terminal_set_message("RUN NAME.TBC"); return; }
    int index = fs_find_name(filename);
    static u8 program[4096];
    u32 length = 0;
    if (index < 0 || !fs_read_file(index, (char *)program, sizeof program, &length)) {
        terminal_set_message("PROGRAM NOT FOUND"); return;
    }
    if (length >= 4 && program[0] == 'B' && program[1] == 'C' && program[2] == 'E' && program[3] == '1') {
        terminal_run_bce(program, (int)length);
        return;
    }
    int out = 0;
    int variables[26];
    for (int i = 0; i < 26; ++i) variables[i] = 0;
    int pos = 0;
    while (pos < (int)length && out < TERM_BUFFER_SIZE - 1) {
        char line[96];
        int n = 0;
        while (pos < (int)length && program[pos] != '\n' && n < (int)sizeof line - 1)
            line[n++] = (char)program[pos++];
        if (pos < (int)length && program[pos] == '\n') ++pos;
        line[n] = 0;
        if (!n) continue;
        int at = 0, command = 0;
        if (n >= 4 && line[0] == 'L' && line[1] == 'E' && line[2] == 'T' && line[3] == ' ') {
            command = 1; at = 4;
        } else if (n >= 4 && line[0] == 'A' && line[1] == 'D' && line[2] == 'D' && line[3] == ' ') {
            command = 2; at = 4;
        } else if (n >= 4 && line[0] == 'S' && line[1] == 'U' && line[2] == 'B' && line[3] == ' ') {
            command = 3; at = 4;
        } else if (n >= 6 && line[0] == 'P' && line[1] == 'R' && line[2] == 'I' &&
                   line[3] == 'N' && line[4] == 'T' && line[5] == ' ') {
            command = 4; at = 6;
        } else if (n >= 5 && line[0] == 'F' && line[1] == 'I' && line[2] == 'L' && line[3] == 'E' && line[4] == ' ') {
            command = 5; at = 5;
        } else if (n >= 6 && line[0] == 'F' && line[1] == 'E' && line[2] == 'T' && line[3] == 'C' && line[4] == 'H' && line[5] == ' ') {
            command = 6; at = 6;
        } else {
            terminal_set_message("TBC: LET ADD SUB PRINT FILE FETCH");
            return;
        }
        while (at < n && line[at] == ' ') ++at;
        if (command == 5 || command == 6) {
            if (at >= n || line[at++] != '"') { terminal_set_message("TBC USE FILE \"NAME\""); return; }
            char path[64]; int path_len = 0;
            while (at < n && line[at] != '"' && path_len < 63) path[path_len++] = line[at++];
            path[path_len] = 0;
            static u8 data[4096];
            int bytes = -1;
            if (command == 5) {
                int file = fs_find_name(path); u32 size = 0;
                if (file >= 0 && fs_read_file(file, (char *)data, sizeof data, &size)) bytes = (int)size;
            } else bytes = repo_get(path, data, sizeof data);
            if (bytes < 0) { terminal_set_message(command == 5 ? "TBC FILE NOT FOUND" : "TBC FETCH FAILED"); return; }
            terminal_show_data(data, bytes);
        } else if (command == 4) {
            if (out && out < TERM_BUFFER_SIZE - 1) terminal_message[out++] = '|';
            if (at < n && line[at] == '"') {
                ++at;
                while (at < n && line[at] != '"' && out < TERM_BUFFER_SIZE - 1)
                    terminal_message[out++] = line[at++];
            } else if (at < n && line[at] >= 'A' && line[at] <= 'Z') {
                int value = variables[line[at++] - 'A'];
                char digits[12];
                int count = 0;
                if (value < 0) { if (out < TERM_BUFFER_SIZE - 1) terminal_message[out++] = '-'; value = -value; }
                do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < 10);
                while (count && out < TERM_BUFFER_SIZE - 1) terminal_message[out++] = digits[--count];
            } else {
                terminal_set_message("TBC PRINT TEXT OR VARIABLE");
                return;
            }
        } else {
            if (at >= n || line[at] < 'A' || line[at] > 'Z') {
                terminal_set_message("TBC VARIABLE MUST BE A-Z");
                return;
            }
            int variable = line[at++] - 'A';
            while (at < n && line[at] == ' ') ++at;
            if (command == 1) {
                if (at >= n || line[at++] != '=') { terminal_set_message("TBC LET A = NUMBER"); return; }
                while (at < n && line[at] == ' ') ++at;
            }
            int sign = 1;
            if (at < n && line[at] == '-') { sign = -1; ++at; }
            int value = 0, digits = 0;
            while (at < n && line[at] >= '0' && line[at] <= '9') {
                value = value * 10 + line[at++] - '0';
                if (value > 100000) value = 100000;
                ++digits;
            }
            if (!digits) { terminal_set_message("TBC EXPECTED NUMBER"); return; }
            value *= sign;
            if (command == 1) variables[variable] = value;
            else if (command == 2) variables[variable] += value;
            else variables[variable] -= value;
        }
    }
    terminal_message[out] = 0;
    if (!out) terminal_set_message("PROGRAM OK");
}

static const char font_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-:|./>";
static const u8 font[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    {0,0,0,31,0,0,0}, {0,4,4,0,4,4,0},
    {4,4,4,4,4,4,4}, {0,0,0,0,0,12,12}, {1,2,4,8,16,0,0},
    {16,8,4,2,4,8,16}
};

static inline void outb(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline u16 inw(u16 port) {
    u16 value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(u16 port, u16 value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static int ata_wait(int need_data) {
    for (u32 timeout = 0; timeout < 1000000; ++timeout) {
        u8 status = inb(0x1f7);
        if (status == 0xff || (status & 1)) return 0;
        if (!(status & 0x80) && (!need_data || (status & 8))) return 1;
    }
    return 0;
}

static void ata_select(void) {
    outb(0x1f6, (u8)(0xe0 | (active_disk ? 0x10 : 0))); /* master or slave */
    for (int i = 0; i < 4; ++i) (void)inb(0x3f6);
}

static int ata_read_fs(void) {
    ata_select();
    if (inb(0x1f7) == 0) return 0;
    outb(0x1f2, FS_SECTORS);
    outb(0x1f3, 0);
    outb(0x1f4, 0);
    outb(0x1f5, 0);
    outb(0x1f7, 0x20);
    for (int sector = 0; sector < FS_SECTORS; ++sector) {
        if (!ata_wait(1)) return 0;
        for (int word = 0; word < 256; ++word) {
            u16 value = inw(0x1f0);
            fs[sector * 512 + word * 2] = (u8)value;
            fs[sector * 512 + word * 2 + 1] = (u8)(value >> 8);
        }
    }
    return 1;
}

static int ata_write_fs(void) {
    ata_select();
    if (inb(0x1f7) == 0) return 0;
    outb(0x1f2, FS_SECTORS);
    outb(0x1f3, 0);
    outb(0x1f4, 0);
    outb(0x1f5, 0);
    outb(0x1f7, 0x30);
    for (int sector = 0; sector < FS_SECTORS; ++sector) {
        if (!ata_wait(1)) return 0;
        for (int word = 0; word < 256; ++word) {
            int pos = sector * 512 + word * 2;
            u16 value = (u16)fs[pos] | ((u16)fs[pos + 1] << 8);
            outw(0x1f0, value);
        }
    }
    outb(0x1f7, 0xe7); /* flush drive cache */
    return ata_wait(0);
}

static u8 *entry_at(int index) {
    return &fs[FS_ENTRY_BASE + index * FS_ENTRY_SIZE];
}

static int entry_used(int index) {
    return (entry_at(index)[FS_ENTRY_FLAGS_OFF] & FS_ENTRY_USED) != 0 && entry_at(index)[0] != 0;
}

static u32 entry_length(int index) {
    u8 *e = entry_at(index);
    return (u32)e[FS_ENTRY_SIZE_OFF] | ((u32)e[FS_ENTRY_SIZE_OFF + 1] << 8) |
           ((u32)e[FS_ENTRY_SIZE_OFF + 2] << 16) | ((u32)e[FS_ENTRY_SIZE_OFF + 3] << 24);
}

static int entry_first(int index) {
    u8 *e = entry_at(index);
    return (int)e[FS_ENTRY_FIRST_OFF] | ((int)e[FS_ENTRY_FIRST_OFF + 1] << 8);
}

static void entry_set_length(int index, u32 length) {
    u8 *e = entry_at(index);
    e[FS_ENTRY_SIZE_OFF] = (u8)length;
    e[FS_ENTRY_SIZE_OFF + 1] = (u8)(length >> 8);
    e[FS_ENTRY_SIZE_OFF + 2] = (u8)(length >> 16);
    e[FS_ENTRY_SIZE_OFF + 3] = (u8)(length >> 24);
}

static int fat_get(int block) { return fs[FS_FAT_OFF + block]; }
static void fat_set(int block, u8 next) { fs[FS_FAT_OFF + block] = next; }

static void fs_free_chain(int first) {
    int block = first;
    for (int n = 0; n < FS_BLOCKS && block >= 0 && block < FS_BLOCKS; ++n) {
        int next = fat_get(block);
        fat_set(block, FS_FAT_FREE);
        if (next == FS_FAT_END || next >= FS_BLOCKS) break;
        block = next;
    }
}

static void fs_format(void) {
    bytes_zero(fs, FS_SIZE);
    fs[0] = 'C'; fs[1] = 'F'; fs[2] = 'S'; fs[3] = '2';
    fs[4] = 2;
    fs[5] = FS_BLOCK_SIZE / 256;
    fs[6] = FS_BLOCKS;
    fs[7] = FS_META_SIZE / 512;
    for (int i = 0; i < FS_BLOCKS; ++i) fat_set(i, FS_FAT_FREE);
}

static int fs_read_file(int index, char *out, u32 capacity, u32 *length_out) {
    if (index < 0 || index >= FS_ENTRIES || !entry_used(index)) return 0;
    u32 length = entry_length(index);
    if (length > FS_MAX_FILE_SIZE) return 0;
    u32 copied = 0;
    int block = entry_first(index);
    for (int n = 0; copied < length && n < FS_BLOCKS; ++n) {
        if (block < 0 || block >= FS_BLOCKS || fat_get(block) == FS_FAT_FREE) return 0;
        u32 part = length - copied;
        if (part > FS_BLOCK_SIZE) part = FS_BLOCK_SIZE;
        if (copied < capacity) {
            u32 take = part;
            if (take > capacity - copied) take = capacity - copied;
            bytes_copy((u8 *)out + copied, &fs[FS_DATA_START + block * FS_BLOCK_SIZE], (int)take);
        }
        copied += part;
        if (copied < length) block = fat_get(block);
    }
    if (copied != length) return 0;
    if (length_out) *length_out = length;
    return 1;
}

static int fs_write_file(int index, const char *data, u32 length) {
    if (index < 0 || index >= FS_ENTRIES || !entry_used(index) || length > FS_MAX_FILE_SIZE) return 0;
    int old_chain[FS_BLOCKS], old_count = 0;
    int block = entry_first(index);
    while (block >= 0 && block < FS_BLOCKS && old_count < FS_BLOCKS && fat_get(block) != FS_FAT_FREE) {
        old_chain[old_count++] = block;
        int next = fat_get(block);
        if (next == FS_FAT_END) break;
        block = next;
    }
    int need = (int)((length + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE);
    int chain[FS_BLOCKS], count = 0;
    for (int i = 0; i < old_count && count < need; ++i) chain[count++] = old_chain[i];
    for (int i = 0; i < FS_BLOCKS && count < need; ++i) {
        int free = fat_get(i) == FS_FAT_FREE;
        for (int j = 0; free && j < old_count; ++j) if (old_chain[j] == i) free = 0;
        if (free) chain[count++] = i;
    }
    if (count != need) return 0;
    for (int i = 0; i < old_count; ++i) fat_set(old_chain[i], FS_FAT_FREE);
    for (int i = 0; i < count; ++i) fat_set(chain[i], i + 1 < count ? (u8)chain[i + 1] : FS_FAT_END);
    u32 copied = 0;
    for (int i = 0; i < count; ++i) {
        u32 part = length - copied;
        if (part > FS_BLOCK_SIZE) part = FS_BLOCK_SIZE;
        u8 *dest = &fs[FS_DATA_START + chain[i] * FS_BLOCK_SIZE];
        bytes_zero(dest, FS_BLOCK_SIZE);
        if (part) bytes_copy(dest, (const u8 *)data + copied, (int)part);
        copied += part;
    }
    u8 *e = entry_at(index);
    e[FS_ENTRY_FIRST_OFF] = count ? (u8)chain[0] : 0xff;
    e[FS_ENTRY_FIRST_OFF + 1] = count ? (u8)(chain[0] >> 8) : 0xff;
    entry_set_length(index, length);
    return fs_save();
}

static void bytes_zero(u8 *p, int count) {
    for (int i = 0; i < count; ++i) p[i] = 0;
}

static void bytes_copy(u8 *to, const u8 *from, int count) {
    for (int i = 0; i < count; ++i) to[i] = from[i];
}

static int fs_save(void) {
    if (!ata_write_fs()) { fs_error = 1; return 0; }
    fs_error = 0;
    return 1;
}

static void fs_create_at(int index, const char *name) {
    u8 *e = entry_at(index);
    bytes_zero(e, FS_ENTRY_SIZE);
    int n = 0;
    while (name[n] && n < FS_NAME_SIZE - 1) { e[n] = (u8)name[n]; ++n; }
    e[FS_ENTRY_FLAGS_OFF] = FS_ENTRY_USED;
    e[FS_ENTRY_FIRST_OFF] = 0xff;
    e[FS_ENTRY_FIRST_OFF + 1] = 0xff;
}

static int fs_first_free(void) {
    for (int i = 0; i < FS_ENTRIES; ++i)
        if (!entry_used(i)) return i;
    return -1;
}

static int fs_find_name(const char *name) {
    for (int i = 0; i < FS_ENTRIES; ++i) {
        if (!entry_used(i)) continue;
        int j = 0;
        while (name[j] && entry_at(i)[j] == (u8)name[j]) ++j;
        if (!name[j] && !entry_at(i)[j]) return i;
    }
    return -1;
}

static int fs_load(void) {
    if (!ata_read_fs()) { fs_error = 1; return 0; }
    fs_error = 0;
    if (fs[0] == 'C' && fs[1] == 'F' && fs[2] == 'S' && fs[3] == '2' && fs[4] == 2) {
        fs_loaded = 1;
        return 1;
    }
    int legacy = fs[0] == 'C' && fs[1] == 'F' && fs[2] == 'S' && fs[3] == '1' && fs[4] == 1;
    if (legacy) bytes_copy(legacy_fs, fs, LEGACY_FS_SIZE);
    fs_format();
    if (legacy) {
        for (int old = 0; old < LEGACY_ENTRY_COUNT; ++old) {
            u8 *src = &legacy_fs[LEGACY_ENTRY_BASE + old * LEGACY_ENTRY_SIZE];
            if (!src[0] || src[LEGACY_TYPE_OFF] != 2) continue;
            char name[FS_NAME_SIZE];
            int n = 0;
            while (n < FS_NAME_SIZE - 1 && n < LEGACY_NAME_SIZE && src[n]) { name[n] = (char)src[n]; ++n; }
            name[n] = 0;
            if (!name[0] || fs_find_name(name) >= 0) continue;
            int slot = fs_first_free();
            if (slot < 0) break;
            fs_create_at(slot, name);
            u32 len = src[LEGACY_LENGTH_OFF] | ((u32)src[LEGACY_LENGTH_OFF + 1] << 8);
            if (len > LEGACY_ENTRY_SIZE - LEGACY_DATA_OFF) len = LEGACY_ENTRY_SIZE - LEGACY_DATA_OFF;
            (void)fs_write_file(slot, (const char *)&src[LEGACY_DATA_OFF], len);
        }
    }
    if (fs_first_free() == 0) {
        fs_create_at(0, "README.TXT");
        (void)fs_write_file(0, "HELLO FROM C OS", 15);
    }
    if (fs_find_name("NOTES.TXT") < 0) {
        int slot = fs_first_free();
        if (slot >= 0) fs_create_at(slot, "NOTES.TXT");
    }
    if (selected_entry < 0) select_next(1);
    fs_loaded = 1;
    return fs_save();
}

static int fs_create_named(const char *name) {
    int len = 0;
    while (name[len] && len < FS_NAME_SIZE) ++len;
    if (len == 0 || len >= FS_NAME_SIZE || fs_find_name(name) >= 0) return -1;
    for (int i = 0; i < len; ++i) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return -1;
    }
    int free_slot = fs_first_free();
    if (free_slot < 0) return -1;
    fs_create_at(free_slot, name);
    selected_entry = free_slot;
    if (!fs_save()) return -1;
    return free_slot;
}

static int fs_add_numbered(void) {
    char name[12];
    name[0]='F'; name[1]='I'; name[2]='L'; name[3]='E';
    name[4]='0'; name[5]='0'; name[6]='.'; name[7]='T'; name[8]='X'; name[9]='T'; name[10]=0;
    for (int i = 0; i < 100; ++i) {
        name[4] = (char)('0' + i / 10); name[5] = (char)('0' + i % 10);
        if (fs_find_name(name) < 0) break;
    }
    if (fs_find_name(name) >= 0) return -1;
    return fs_create_named(name);
}

static int fs_delete_selected(void) {
    if (selected_entry < 0 || !entry_used(selected_entry)) return 0;
    fs_free_chain(entry_first(selected_entry));
    bytes_zero(entry_at(selected_entry), FS_ENTRY_SIZE);
    selected_entry = -1;
    return fs_save();
}

static void select_next(int direction) {
    if (selected_entry < 0) {
        for (int i = 0; i < FS_ENTRIES; ++i)
            if (entry_used(i)) { selected_entry = i; return; }
        return;
    }
    for (int n = 1; n <= FS_ENTRIES; ++n) {
        int i = selected_entry + direction * n;
        if (i < 0) i += FS_ENTRIES;
        if (i >= FS_ENTRIES) i -= FS_ENTRIES;
        if (entry_used(i)) { selected_entry = i; return; }
    }
}

static void edit_entry(int index, int from_files) {
    if (index < 0 || index >= FS_ENTRIES || !entry_used(index)) return;
    editing_entry = index;
    editor_from_files = from_files;
    selected_entry = index;
    u32 length = 0;
    if (!fs_read_file(index, editor_buffer, FS_MAX_FILE_SIZE, &length)) length = 0;
    editor_len = (int)length;
    editor_cursor = editor_len;
    editor_buffer[editor_len] = 0;
    editor_saved = 1;
    notes_open = 1;
    active_window = 1;
    context_open = 0;
    ui_dirty = 1;
}

static int save_editor(void) {
    if (editing_entry < 0 || editing_entry >= FS_ENTRIES || !entry_used(editing_entry)) return 0;
    return fs_write_file(editing_entry, editor_buffer, (u32)editor_len);
}

static void close_editor(void) {
    if (!editor_saved && editing_entry >= 0) editor_saved = save_editor();
    notes_open = 0;
    editing_entry = -1;
    if (editor_from_files && files_open) active_window = 0;
    else if (files_open) active_window = 0;
    else if (terminal_open) active_window = 2;
    else if (browser_open) active_window = 3;
    editor_from_files = 0;
    context_open = 0;
    ui_dirty = 1;
}

static void editor_row_col(int position, int *row_out, int *col_out) {
    int row = 0, col = 0;
    if (position > editor_len) position = editor_len;
    for (int i = 0; i < position; ++i) {
        if (editor_buffer[i] == '\n') { ++row; col = 0; }
        else {
            if (col >= 34) { ++row; col = 0; }
            ++col;
        }
    }
    *row_out = row;
    *col_out = col;
}

static int editor_index_at(int target_row, int target_col) {
    int row = 0, col = 0;
    for (int i = 0; i <= editor_len; ++i) {
        if (row == target_row && col >= target_col) return i;
        if (i == editor_len) break;
        if (editor_buffer[i] == '\n') {
            if (row == target_row) return i;
            ++row; col = 0;
        } else {
            if (col >= 34) { ++row; col = 0; }
            ++col;
        }
    }
    return editor_len;
}

static void editor_move_vertical(int direction) {
    int row, col;
    editor_row_col(editor_cursor, &row, &col);
    if (direction < 0 && row > 0) --row;
    if (direction > 0) ++row;
    editor_cursor = editor_index_at(row, col);
}

static void editor_keep_cursor_visible(int visible_lines) {
    int row, col;
    editor_row_col(editor_cursor, &row, &col);
    (void)col;
    if (row < editor_scroll_line) editor_scroll_line = row;
    if (row >= editor_scroll_line + visible_lines) editor_scroll_line = row - visible_lines + 1;
}

static void editor_insert_char(char c) {
    if (editing_entry < 0 || editor_len >= FS_MAX_FILE_SIZE) return;
    for (int i = editor_len; i > editor_cursor; --i) editor_buffer[i] = editor_buffer[i - 1];
    editor_buffer[editor_cursor++] = c;
    editor_buffer[++editor_len] = 0;
    editor_saved = 0;
}

static void editor_backspace(void) {
    if (editor_cursor <= 0 || editor_len <= 0) return;
    for (int i = editor_cursor - 1; i < editor_len - 1; ++i) editor_buffer[i] = editor_buffer[i + 1];
    --editor_cursor;
    editor_buffer[--editor_len] = 0;
    editor_saved = 0;
}

static void editor_delete(void) {
    if (editor_cursor >= editor_len) return;
    for (int i = editor_cursor; i < editor_len - 1; ++i) editor_buffer[i] = editor_buffer[i + 1];
    editor_buffer[--editor_len] = 0;
    editor_saved = 0;
}

static int tag_is(const char *tag, int length, const char *name) {
    int i = 0;
    while (name[i] && i < length) {
        char c = tag[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != name[i]) return 0;
        ++i;
    }
    return !name[i] && (i == length || tag[i] == ' ' || tag[i] == '/' || tag[i] == '\t');
}

static void browser_load_url(void) {
    const char *p = browser_url;
    const char *scheme = "tiny://";
    for (int i = 0; scheme[i]; ++i) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != scheme[i]) {
            browser_status[0] = 'U'; browser_status[1] = 'S'; browser_status[2] = 'E';
            browser_status[3] = ' '; browser_status[4] = 'T'; browser_status[5] = 'I';
            browser_status[6] = 'N'; browser_status[7] = 'Y'; browser_status[8] = ':';
            browser_status[9] = '/'; browser_status[10] = '/'; browser_status[11] = 0;
            browser_text_len = 0;
            return;
        }
    }
    p += 7;
    while (*p && *p != '/') ++p;
    if (!*p || !net_ready) {
        const char *message = net_ready ? "BAD TINY URL" : "NO ETHERNET";
        int i = 0; while (message[i] && i < TERM_BUFFER_SIZE - 1) { browser_status[i] = message[i]; ++i; }
        browser_status[i] = 0;
        browser_text_len = 0;
        return;
    }
    int received = repo_get(p, browser_response, sizeof browser_response);
    if (received < 0) {
        const char *message = "REPO REQUEST FAILED";
        int i = 0; while (message[i] && i < TERM_BUFFER_SIZE - 1) { browser_status[i] = message[i]; ++i; }
        browser_status[i] = 0;
        browser_text_len = 0;
        return;
    }
    browser_text_len = 0;
    for (int i = 0; i < received && browser_text_len < (int)sizeof browser_text - 1; ++i) {
        char c = (char)browser_response[i];
        if (c == '\r') continue;
        if (c == '\n' || c == '\t' || (c >= 32 && c < 127)) {
            browser_text[browser_text_len] = c;
            browser_text_colors[browser_text_len++] = 0;
        }
    }
    browser_text[browser_text_len] = 0;
    browser_scroll = 0;
    browser_bg_color = 15;
    browser_address_active = 0;
    const char *message = "TINYREPO LOADED";
    int i = 0; while (message[i] && i < TERM_BUFFER_SIZE - 1) { browser_status[i] = message[i]; ++i; }
    browser_status[i] = 0;
    ui_dirty = 1;
}

static void terminal_set_message(const char *s) {
    bytes_zero((u8 *)terminal_message, TERM_BUFFER_SIZE);
    int i = 0;
    while (s[i] && i < TERM_BUFFER_SIZE - 1) {
        terminal_message[i] = s[i];
        ++i;
    }
}

static void settings_load(void) {
    int index = fs_find_name("WALLPAPR.CFG");
    char value[4];
    u32 length = 0;
    if (index >= 0 && fs_read_file(index, value, sizeof value, &length) && length == 1 &&
        value[0] >= '0' && value[0] <= '2') {
        wallpaper_id = value[0] - '0';
        return;
    }
    /* Read the old theme preference as a one-time compatibility fallback. */
    index = fs_find_name("THEME.CFG");
    if (index >= 0 && fs_read_file(index, value, sizeof value, &length) && length == 1)
        wallpaper_id = value[0] == 'G' ? 2 : value[0] == 'B' ? 0 : 1;
}

static void wallpaper_set(int id) {
    if (id < 0 || id > 2) return;
    char value = (char)('0' + id);
    int index = fs_find_name("WALLPAPR.CFG");
    if (index < 0) index = fs_create_named("WALLPAPR.CFG");
    if (index >= 0 && fs_write_file(index, &value, 1)) {
        wallpaper_id = id;
        ui_dirty = 1;
    }
}


static int terminal_command_is(const char *word) {
    int i = 0;
    while (word[i] && terminal_line[i] == word[i]) ++i;
    return !word[i] && (terminal_line[i] == 0 || terminal_line[i] == ' ');
}

static int terminal_get_arg(char *out, int capacity) {
    int i = 0;
    while (terminal_line[i] && terminal_line[i] != ' ') ++i;
    while (terminal_line[i] == ' ') ++i;
    int n = 0;
    while (terminal_line[i] && terminal_line[i] != ' ' && n < capacity - 1)
        out[n++] = terminal_line[i++];
    out[n] = 0;
    return n;
}

static void terminal_output_push(const char *line) {
    if (terminal_output_count >= TERM_OUTPUT_SIZE) {
        for (int i = 1; i < TERM_OUTPUT_SIZE; ++i)
            bytes_copy((u8 *)terminal_output[i - 1], (const u8 *)terminal_output[i], TERM_BUFFER_SIZE);
        terminal_output_count = TERM_OUTPUT_SIZE - 1;
    }
    int row = terminal_output_count++;
    int i = 0;
    while (line[i] && i < TERM_BUFFER_SIZE - 1) {
        terminal_output[row][i] = line[i];
        ++i;
    }
    terminal_output[row][i] = 0;
    terminal_output_scroll = 0;
}

static void terminal_show_data(const u8 *data, int length) {
    char line[TERM_BUFFER_SIZE];
    int n = 0;
    for (int i = 0; i < length; ++i) {
        char c = (char)data[i];
        if (c == '\r') continue;
        if (c == '\n' || n >= TERM_BUFFER_SIZE - 1) {
            line[n] = 0;
            terminal_output_push(line);
            n = 0;
        } else if (c >= 32 || c == '\t') line[n++] = c == '\t' ? ' ' : c;
    }
    if (n || !length) { line[n] = 0; terminal_output_push(line); }
}

static void terminal_history_save(void) {
    if (!terminal_len) return;
    if (terminal_history_count && text_equal(terminal_history[terminal_history_count - 1], terminal_line)) return;
    if (terminal_history_count >= TERM_HISTORY_SIZE) {
        for (int i = 1; i < TERM_HISTORY_SIZE; ++i)
            bytes_copy((u8 *)terminal_history[i - 1], (const u8 *)terminal_history[i], TERM_BUFFER_SIZE);
        terminal_history_count = TERM_HISTORY_SIZE - 1;
    }
    bytes_copy((u8 *)terminal_history[terminal_history_count++], (const u8 *)terminal_line, TERM_BUFFER_SIZE);
    terminal_history_pos = -1;
}

static void terminal_history_move(int direction) {
    if (!terminal_history_count) return;
    if (direction < 0) {
        if (terminal_history_pos < 0) terminal_history_pos = terminal_history_count - 1;
        else if (terminal_history_pos > 0) --terminal_history_pos;
    } else {
        if (terminal_history_pos < 0) return;
        if (++terminal_history_pos >= terminal_history_count) terminal_history_pos = -1;
    }
    if (terminal_history_pos < 0) {
        bytes_zero((u8 *)terminal_line, TERM_BUFFER_SIZE);
        terminal_len = 0;
    } else {
        bytes_copy((u8 *)terminal_line, (const u8 *)terminal_history[terminal_history_pos], TERM_BUFFER_SIZE);
        terminal_len = 0;
        while (terminal_line[terminal_len] && terminal_len < TERM_BUFFER_SIZE - 1) ++terminal_len;
    }
    ui_dirty = 1;
}

static void terminal_run_command(void) {
    char arg[FS_NAME_SIZE];
    bytes_zero((u8 *)terminal_message, TERM_BUFFER_SIZE);
    if (terminal_len) {
        char echo[TERM_BUFFER_SIZE];
        echo[0] = '>';
        int i = 0;
        while (terminal_line[i] && i < TERM_BUFFER_SIZE - 2) { echo[i + 1] = terminal_line[i]; ++i; }
        echo[i + 1] = 0;
        terminal_history_save();
        terminal_output_push(echo);
    }
    if (terminal_command_is("HELP")) {
        terminal_set_message("HELP LS CAT COMP RUN PKG DOWNL");
        terminal_output_push("TBC FILE \"NAME\" reads a disk file");
        terminal_output_push("TBC FETCH \"/path\" reads TinyRepo");
        terminal_output_push("DISK 0 or DISK 1 selects a 64K drive");
    } else if (terminal_command_is("DISK")) {
        if (!terminal_get_arg(arg, sizeof arg) || (arg[0] != '0' && arg[0] != '1') || arg[1]) {
            terminal_set_message("USAGE DISK 0 OR DISK 1");
        } else if (notes_open && editing_entry >= 0) {
            terminal_set_message("CLOSE NOTES BEFORE DISK CHANGE");
        } else {
            int old_disk = active_disk;
            active_disk = arg[0] - '0';
            fs_loaded = 0;
            selected_entry = -1;
            file_scroll_index = 0;
            if (fs_load()) {
                select_next(1);
                terminal_set_message(active_disk ? "DISK 1 READY" : "DISK 0 READY");
            }
            else { active_disk = old_disk; fs_loaded = 0; (void)fs_load(); terminal_set_message("DISK SWITCH FAILED"); }
        }
    } else if (terminal_command_is("PKG")) {
        terminal_pkg();
    } else if (terminal_command_is("COMP")) {
        (void)terminal_compile_tbc();
    } else if (terminal_command_is("RUN")) {
        terminal_run_tbc();
    } else if (terminal_command_is("BSOD")) {
        terminal_set_message("RESTART WITH R OR ESCAPE TO RESUME");
        bsod_mode = 1;
    } else if (terminal_command_is("NET")) {
        terminal_set_message(net_ready ? "NE2000 READY" : "NO ETHERNET DEVICE");
    } else if (terminal_command_is("DOWNL")) {
        static u8 response[FS_MAX_FILE_SIZE + 64];
        char host[TERM_BUFFER_SIZE], request[TERM_BUFFER_SIZE + 16];
        int arg_at = 0;
        while (terminal_line[arg_at] && terminal_line[arg_at] != ' ') ++arg_at;
        while (terminal_line[arg_at] == ' ') ++arg_at;
        int host_len = 0;
        while (terminal_line[arg_at + host_len] && terminal_line[arg_at + host_len] != '/' &&
               host_len < (int)sizeof host - 1) {
            host[host_len] = terminal_line[arg_at + host_len];
            ++host_len;
        }
        host[host_len] = 0;
        if (!host_len || terminal_line[arg_at + host_len] != '/') {
            terminal_set_message("DOWNL HOST/PATH");
        } else if (!net_ready) {
            terminal_set_message("NO ETHERNET DEVICE");
        } else {
            int path_at = arg_at + host_len;
            int req_len = 0;
            const char *prefix = "TINY/1 GET ";
            for (int i = 0; prefix[i]; ++i) request[req_len++] = prefix[i];
            while (terminal_line[path_at] && req_len < (int)sizeof request - 2)
                request[req_len++] = terminal_line[path_at++];
            request[req_len++] = '\n';
            int received = net_tcp_request(host, 8080, (const u8 *)request, (u16)req_len,
                                           response, sizeof response);
            int header_end = 0;
            while (header_end < received && response[header_end] != '\n') ++header_end;
            int payload_at = header_end + 1;
            u32 payload_length = 0;
            int digit = header_end > 12 && response[0] == 'T' && response[7] == '2' &&
                        response[8] == '0' && response[9] == '0' && response[10] == ' ';
            for (int i = 11; digit && i < header_end; ++i) {
                if (response[i] < '0' || response[i] > '9') { digit = 0; break; }
                payload_length = payload_length * 10 + response[i] - '0';
            }
            if (received < 0 || !digit || payload_length != (u32)(received - payload_at) ||
                payload_length > FS_MAX_FILE_SIZE) {
                terminal_set_message(received >= 10 && response[7] == '4' ? "REPO FILE NOT FOUND" : "DOWNLOAD FAILED");
            } else {
                int name_start = arg_at + host_len;
                for (int i = name_start; terminal_line[i]; ++i)
                    if (terminal_line[i] == '/') name_start = i + 1;
                char filename[FS_NAME_SIZE];
                int filename_len = 0;
                while (terminal_line[name_start + filename_len] && filename_len < FS_NAME_SIZE - 1) {
                    filename[filename_len] = terminal_line[name_start + filename_len];
                    ++filename_len;
                }
                filename[filename_len] = 0;
                int index = fs_find_name(filename);
                if (!filename_len) terminal_set_message("BAD FILE NAME");
                else {
                    if (index < 0) index = fs_create_named(filename);
                    if (index < 0 || !fs_write_file(index, (const char *)&response[payload_at], payload_length))
                        terminal_set_message("SAVE FAILED");
                    else terminal_set_message("DOWNLOAD SAVED");
                }
            }
        }
    } else if (terminal_command_is("CLEAR")) {
        terminal_output_count = 0;
        terminal_output_scroll = 0;
        terminal_set_message("");
    } else if (terminal_command_is("LS")) {
        bytes_zero((u8 *)terminal_message, TERM_BUFFER_SIZE);
        int out = 0;
        for (int i = 0; i < FS_ENTRIES; ++i) {
            if (!entry_used(i)) continue;
            if (out && out < TERM_BUFFER_SIZE - 1) terminal_message[out++] = ' ';
            u8 *e = entry_at(i);
            for (int j = 0; e[j] && out < TERM_BUFFER_SIZE - 1; ++j)
                terminal_message[out++] = (char)e[j];
        }
        if (!out) terminal_set_message("NO FILES");
    } else if (terminal_command_is("CAT")) {
        if (!terminal_get_arg(arg, sizeof arg)) terminal_set_message("USAGE CAT NAME");
        else {
            int index = fs_find_name(arg);
            if (index < 0) terminal_set_message("FILE NOT FOUND");
            else {
                char preview[TERM_BUFFER_SIZE];
                u32 length = 0;
                (void)fs_read_file(index, preview, sizeof preview - 1, &length);
                int out = 0;
                while (out < length && out < TERM_BUFFER_SIZE - 1 && preview[out] != '\n') {
                    terminal_message[out] = preview[out];
                    ++out;
                }
                terminal_message[out] = 0;
                if (!out) terminal_set_message("EMPTY FILE");
            }
        }
    } else if (terminal_command_is("NEW")) {
        if (!terminal_get_arg(arg, sizeof arg)) terminal_set_message("USAGE NEW NAME.TXT");
        else terminal_set_message(fs_create_named(arg) >= 0 ? "FILE CREATED" : "CREATE FAILED");
    } else if (terminal_command_is("EDIT")) {
        if (!terminal_get_arg(arg, sizeof arg)) terminal_set_message("USAGE EDIT NAME.TXT");
        else {
            int index = fs_find_name(arg);
            if (index < 0) terminal_set_message("FILE NOT FOUND");
            else edit_entry(index, 0);
        }
    } else if (terminal_command_is("DEL")) {
        if (!terminal_get_arg(arg, sizeof arg)) terminal_set_message("USAGE DEL NAME.TXT");
        else {
            int index = fs_find_name(arg);
            if (index < 0) terminal_set_message("FILE NOT FOUND");
            else {
                fs_free_chain(entry_first(index));
                bytes_zero(entry_at(index), FS_ENTRY_SIZE);
                (void)fs_save();
                terminal_set_message(fs_error ? "WRITE FAILED" : "FILE DELETED");
            }
        }
    } else if (terminal_command_is("EXIT")) {
        terminal_open = 0;
        active_window = notes_open ? 1 : files_open ? 0 : 2;
    } else if (terminal_line[0]) {
        terminal_set_message("UNKNOWN COMMAND");
    }
    if (terminal_message[0]) terminal_output_push(terminal_message);
    bytes_zero((u8 *)terminal_line, TERM_BUFFER_SIZE);
    terminal_len = 0;
    ui_dirty = 1;
}

static void rect(int x, int y, int w, int h, u8 color) {
    for (int row = 0; row < h; ++row) {
        u32 offset = (u32)(y + row) * 320u + (u32)x;
        for (int col = 0; col < w; ++col) video[offset + col] = color;
    }
}

static int glyph_index(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int i = 0; font_chars[i]; ++i)
        if (font_chars[i] == c) return i;
    return -1;
}

static void text_n(int x, int y, const char *s, int length, u8 color) {
    while (length-- > 0) {
        int gi = glyph_index(*s++);
        if (gi >= 0) {
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (font[gi][row] & (16 >> col))
                        video[(y + row) * 320 + x + col] = color;
        }
        x += 6;
    }
}

static void text(int x, int y, const char *s, u8 color) {
    int length = 0;
    while (s[length]) ++length;
    text_n(x, y, s, length, color);
}

static void button(int x, int y, int width, int height, const char *label) {
    rect(x, y, width, height, 7);
    rect(x, y, width, 1, 15);
    rect(x, y, 1, height, 15);
    rect(x, y + height - 1, width, 1, 8);
    rect(x + width - 1, y, 1, height, 8);
    int length = 0;
    while (label[length]) ++length;
    text(x + (width - length * 6) / 2, y + (height - 7) / 2, label, 0);
}

static void draw_window_controls(int x, int y, int width, int app) {
    button(x + width - 44, y + 2, 12, 10, "-");
    rect(x + width - 30, y + 2, 12, 10, 7);
    rect(x + width - 28, y + 4, 8, 6, 15);
    rect(x + width - 26, y + 6, 4, 2, 7);
    if (app >= 0 && app < 5 && app_maximized[app]) {
        rect(x + width - 28, y + 4, 5, 1, 15);
        rect(x + width - 28, y + 4, 1, 4, 15);
    }
    rect(x + width - 16, y + 2, 12, 10, 4);
    text(x + width - 16, y + 4, "X", 15);
}

static void window_get_rect(int app, int *x, int *y, int *w, int *h) {
    if (app == 0) { *x=file_x; *y=file_y; *w=file_w; *h=file_h; }
    else if (app == 1) { *x=notes_x; *y=notes_y; *w=notes_w; *h=notes_h; }
    else if (app == 2) { *x=terminal_x; *y=terminal_y; *w=terminal_w; *h=terminal_h; }
    else if (app == 3) { *x=browser_x; *y=browser_y; *w=browser_w; *h=browser_h; }
    else { *x=settings_x; *y=settings_y; *w=settings_w; *h=settings_h; }
}

static void window_set_rect(int app, int x, int y, int w, int h) {
    if (app == 0) { file_x=x; file_y=y; file_w=w; file_h=h; }
    else if (app == 1) { notes_x=x; notes_y=y; notes_w=w; notes_h=h; }
    else if (app == 2) { terminal_x=x; terminal_y=y; terminal_w=w; terminal_h=h; }
    else if (app == 3) { browser_x=x; browser_y=y; browser_w=w; browser_h=h; }
    else { settings_x=x; settings_y=y; settings_w=w; settings_h=h; }
}

static int app_is_open(int app) {
    if (app == 0) return files_open;
    if (app == 1) return notes_open;
    if (app == 2) return terminal_open;
    if (app == 3) return browser_open;
    return settings_open;
}

static void window_maximize_toggle(int app) {
    if (app < 0 || app > 4) return;
    if (app_maximized[app]) {
        window_set_rect(app, restore_x[app], restore_y[app], restore_w[app], restore_h[app]);
        app_maximized[app] = 0;
    } else {
        window_get_rect(app, &restore_x[app], &restore_y[app], &restore_w[app], &restore_h[app]);
        window_set_rect(app, 0, 0, 320, 184);
        app_maximized[app] = 1;
    }
    ui_dirty = 1;
}

static void window_minimize(int app) {
    if (app < 0 || app > 4) return;
    app_minimized[app] = 1;
    active_window = -1;
    switch_task();
    ui_dirty = 1;
}

static u8 cmos_read(u8 reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int from_bcd(u8 value) {
    return (value & 15) + ((value >> 4) * 10);
}

static int leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int month_days(int year, int month) {
    static const u8 days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2) return days[1] + leap_year(year);
    return month >= 1 && month <= 12 ? days[month - 1] : 31;
}

static int weekday(int year, int month, int day) {
    static const u8 offset[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (month < 3) --year;
    return (year + year / 4 - year / 100 + year / 400 + offset[month - 1] + day) % 7;
}

static int last_sunday(int year, int month) {
    int last = month_days(year, month);
    return last - weekday(year, month, last);
}

static int kyiv_summer_time(int year, int month, int day, int hour_utc) {
    if (month > 3 && month < 10) return 1;
    if (month == 3) {
        int start = last_sunday(year, 3);
        return day > start || (day == start && hour_utc >= 1);
    }
    if (month == 10) {
        int end = last_sunday(year, 10);
        return day < end || (day == end && hour_utc < 1);
    }
    return 0;
}

static void two_digits(char *out, int value) {
    out[0] = (char)('0' + (value / 10) % 10);
    out[1] = (char)('0' + value % 10);
}

static void clock_refresh(void) {
    if (++rtc_poll_count < 50000) return;
    rtc_poll_count = 0;
    while (cmos_read(0x0a) & 0x80) {}
    u8 raw_second = cmos_read(0x00);
    if (raw_second == rtc_last_second) return;
    rtc_last_second = raw_second;
    u8 second = raw_second;
    u8 minute = cmos_read(0x02);
    u8 raw_hour = cmos_read(0x04);
    u8 day = cmos_read(0x07);
    u8 month = cmos_read(0x08);
    u8 year = cmos_read(0x09);
    u8 century = cmos_read(0x32);
    u8 status = cmos_read(0x0b);
    int pm = (raw_hour & 0x80) != 0;
    u8 hour = raw_hour & 0x7f;
    if (!(status & 0x04)) {
        second = (u8)from_bcd(second);
        minute = (u8)from_bcd(minute);
        hour = (u8)from_bcd(hour);
        day = (u8)from_bcd(day);
        month = (u8)from_bcd(month);
        year = (u8)from_bcd(year);
        century = (u8)from_bcd(century);
    }
    if (!(status & 0x02)) {
        if (pm && hour < 12) hour += 12;
        if (!pm && hour == 12) hour = 0;
    }
    int full_year = century >= 19 && century <= 99 ? century * 100 + year : 2000 + year;
    hour += kyiv_summer_time(full_year, month, day, hour) ? 3 : 2;
    if (hour >= 24) {
        hour -= 24;
        if (++day > month_days(full_year, month)) {
            day = 1;
            if (++month > 12) { month = 1; ++full_year; }
        }
    }
    two_digits(clock_time, hour);
    clock_time[2] = ':';
    two_digits(clock_time + 3, minute);
    clock_time[5] = 0;
    two_digits(clock_date, day);
    clock_date[2] = '/';
    two_digits(clock_date + 3, month);
    clock_date[5] = 0;
    (void)second;
    ui_dirty = 1;
}

static void draw_wallpaper(void) {
    if (wallpaper_id == 0) {
        rect(0, 0, 320, 184, 9);
        rect(0, 134, 320, 50, 2);
        rect(0, 148, 320, 36, 10);
        rect(246, 34, 18, 18, 14);
        rect(230, 43, 12, 8, 14);
    } else if (wallpaper_id == 1) {
        rect(0, 0, 320, 184, 1);
        rect(0, 138, 320, 46, 8);
        rect(0, 152, 320, 32, 0);
        rect(45, 36, 3, 3, 15); rect(90, 65, 3, 3, 14);
        rect(215, 54, 3, 3, 15); rect(278, 88, 3, 3, 14);
        rect(34, 90, 13, 13, 14); rect(39, 85, 13, 13, 1);
    } else {
        rect(0, 0, 320, 184, 2);
        rect(0, 128, 320, 56, 10);
        rect(25, 93, 28, 54, 6); rect(35, 76, 8, 30, 6);
        rect(18, 65, 22, 35, 10); rect(37, 58, 22, 39, 10);
        rect(245, 88, 32, 64, 6); rect(256, 69, 9, 32, 6);
        rect(237, 58, 26, 43, 10); rect(257, 69, 25, 37, 10);
    }
    text(139, 82, "TINYOS", 0);
    text(137, 80, "TINYOS", 15);
    text(109, 94, "I386 PERSONAL OS", wallpaper_id == 1 ? 14 : 15);
}

static void draw_clock(void) {
    rect(262, 4, 54, 24, wallpaper_id == 1 ? 8 : 15);
    text(268, 6, clock_time, wallpaper_id == 1 ? 15 : 0);
    text(268, 16, clock_date, wallpaper_id == 1 ? 15 : 0);
}

static int file_count(void) {
    int count = 0;
    for (int i = 0; i < FS_ENTRIES; ++i) if (entry_used(i)) ++count;
    return count;
}

static int file_at_ordinal(int ordinal) {
    if (ordinal < 0) return -1;
    for (int i = 0; i < FS_ENTRIES; ++i) {
        if (!entry_used(i)) continue;
        if (!ordinal--) return i;
    }
    return -1;
}

static int file_selected_ordinal(void) {
    int ordinal = 0;
    for (int i = 0; i < FS_ENTRIES; ++i) {
        if (!entry_used(i)) continue;
        if (i == selected_entry) return ordinal;
        ++ordinal;
    }
    return -1;
}

static int file_visible_tiles(void) {
    int rows = (file_h - 72) / 36;
    if (rows < 1) rows = 1;
    return rows * 2;
}

static void files_keep_selection_visible(void) {
    int count = file_count();
    int visible = file_visible_tiles();
    int selected = file_selected_ordinal();
    if (selected >= 0 && selected < file_scroll_index) file_scroll_index = selected;
    else if (selected >= file_scroll_index + visible) file_scroll_index = selected - visible + 1;
    int maximum = count - visible;
    if (maximum < 0) maximum = 0;
    if (file_scroll_index > maximum) file_scroll_index = maximum;
    if (file_scroll_index < 0) file_scroll_index = 0;
}

static void draw_file_manager(void) {
    rect(file_x + 4, file_y + 4, file_w - 4, file_h - 4, 0);
    rect(file_x, file_y, file_w, file_h, 7);
    rect(file_x, file_y, file_w, 14, active_window == 0 ? 1 : 8);
    text(file_x + 10, file_y + 4, active_disk ? "FILES D1" : "FILES D0", 15);
    draw_window_controls(file_x, file_y, file_w, 0);
    rect(file_x + 6, file_y + 18, file_w - 12, file_h - 64, 15);
    if (fs_error) {
        text(file_x + 12, file_y + 28, "DISK ERROR", 4);
    } else {
        files_keep_selection_visible();
        int visible = file_visible_tiles();
        int tile_w = (file_w - 24) / 2;
        for (int ordinal = file_scroll_index; ordinal < file_scroll_index + visible; ++ordinal) {
            int index = file_at_ordinal(ordinal);
            if (index < 0) break;
            int col = (ordinal - file_scroll_index) % 2;
            int row = (ordinal - file_scroll_index) / 2;
            int x = file_x + 8 + col * (tile_w + 4);
            int y = file_y + 21 + row * 36;
            int selected = index == selected_entry;
            rect(x, y, tile_w, 33, selected ? 1 : 7);
            rect(x + 1, y + 1, tile_w - 2, 31, selected ? 1 : 15);
            /* Page-shaped file icon. */
            rect(x + 6, y + 6, 13, 18, selected ? 15 : 8);
            rect(x + 9, y + 11, 7, 1, selected ? 1 : 0);
            rect(x + 9, y + 15, 7, 1, selected ? 1 : 0);
            rect(x + 9, y + 19, 5, 1, selected ? 1 : 0);
            u8 *entry = entry_at(index);
            int name_len = 0;
            while (name_len < FS_NAME_SIZE && entry[name_len]) ++name_len;
            int name_limit = (tile_w - 30) / 6;
            if (name_len > name_limit) name_len = name_limit;
            text_n(x + 25, y + 7, (const char *)entry, name_len, selected ? 15 : 0);
            u32 bytes = entry_length(index);
            char size_label[8];
            if (bytes < 1024) {
                int value = (int)bytes, digits = 0;
                do { size_label[digits++] = (char)('0' + value % 10); value /= 10; } while (value && digits < 4);
                for (int i = 0; i < digits / 2; ++i) { char c = size_label[i]; size_label[i] = size_label[digits - 1 - i]; size_label[digits - 1 - i] = c; }
                size_label[digits++] = 'B'; size_label[digits] = 0;
            } else {
                int kb = (int)((bytes + 1023) / 1024);
                size_label[0] = (char)('0' + kb / 10); size_label[1] = (char)('0' + kb % 10);
                size_label[2] = 'K'; size_label[3] = 0;
            }
            text(x + 25, y + 19, size_label, selected ? 15 : 8);
        }
        if (!file_count()) text(file_x + 14, file_y + 28, "NO FILES", 0);
    }
    rect(file_x + file_w - 8, file_y + file_h - 5, 5, 2, 8);
    if (file_h >= 158) {
        int controls_y = file_y + file_h - 42;
        button(file_x + 8, controls_y, 48, 13, "NEW");
        button(file_x + 62, controls_y, 54, 13, "OPEN");
        button(file_x + 122, controls_y, 50, 13, "DEL");
        text(file_x + 10, file_y + file_h - 27, "WHEEL TO SCROLL  ENTER OPEN", 0);
        text(file_x + 10, file_y + file_h - 15, "FILES 16  DATA 63K", 0);
    } else text(file_x + 10, file_y + file_h - 14, "DRAG CORNER", 0);
}

static void draw_notes_window(void) {
    rect(notes_x + 4, notes_y + 4, notes_w - 4, notes_h - 4, 0);
    rect(notes_x, notes_y, notes_w, notes_h, 7);
    rect(notes_x, notes_y, notes_w, 14, active_window == 1 ? 1 : 8);
    if (editing_entry >= 0 && entry_used(editing_entry)) {
        u8 *e = entry_at(editing_entry);
        int title_len = 0;
        while (title_len < FS_NAME_SIZE && e[title_len]) ++title_len;
        text_n(notes_x + 10, notes_y + 4, (const char *)e, title_len, 15);
    } else {
        text(notes_x + 10, notes_y + 4, "NOTES", 15);
    }
    draw_window_controls(notes_x, notes_y, notes_w, 1);
    rect(notes_x + 6, notes_y + 18, notes_w - 12, notes_h - 40, 15);
    int visible_lines = (notes_h - 40) / 9;
    if (visible_lines < 1) visible_lines = 1;
    if (visible_lines > 11) visible_lines = 11;
    if (editing_entry >= 0) editor_keep_cursor_visible(visible_lines);
    int pos = 0;
    int logical_line = 0;
    while (pos < editor_len && logical_line < editor_scroll_line) {
        if (editor_buffer[pos++] == '\n') ++logical_line;
        else {
            int col = 1;
            while (pos < editor_len && editor_buffer[pos] != '\n' && col < 34) { ++pos; ++col; }
            if (col >= 34 && pos < editor_len && editor_buffer[pos] != '\n') ++logical_line;
            else if (pos < editor_len && editor_buffer[pos] == '\n') { ++pos; ++logical_line; }
        }
    }
    int line = 0;
    while (pos < editor_len && line < visible_lines) {
        int start = pos;
        int count = 0;
        while (pos < editor_len && editor_buffer[pos] != '\n' && count < 34) {
            ++pos;
            ++count;
        }
        text_n(notes_x + 12, notes_y + 23 + line * 9, editor_buffer + start, count, 0);
        if (pos < editor_len && editor_buffer[pos] == '\n') ++pos;
        ++line;
    }
    if (editing_entry < 0 && fs_error) text(notes_x + 12, notes_y + 23, "DISK ERROR", 4);
    else if (editor_len == 0) text(notes_x + 12, notes_y + 23, "TYPE TO WRITE", 8);
    if (editing_entry >= 0) {
        int cursor_line, cursor_col;
        editor_row_col(editor_cursor, &cursor_line, &cursor_col);
        if (cursor_line >= editor_scroll_line && cursor_line < editor_scroll_line + visible_lines)
            rect(notes_x + 12 + cursor_col * 6, notes_y + 23 + (cursor_line - editor_scroll_line) * 9, 4, 7, 14);
    }
    text(notes_x + 10, notes_y + notes_h - 14,
         fs_error ? "WRITE FAILED" : "CTRL S SAVE  AUTO CLOSE",
         fs_error ? 4 : 0);
    rect(notes_x + notes_w - 8, notes_y + notes_h - 5, 5, 2, 8);
}

static void draw_terminal_window(void) {
    rect(terminal_x + 4, terminal_y + 4, terminal_w - 4, terminal_h - 4, 0);
    rect(terminal_x, terminal_y, terminal_w, terminal_h, 7);
    rect(terminal_x, terminal_y, terminal_w, 14, active_window == 2 ? 1 : 8);
    text(terminal_x + 10, terminal_y + 4, "TERMINAL", 15);
    draw_window_controls(terminal_x, terminal_y, terminal_w, 2);
    rect(terminal_x + 6, terminal_y + 18, terminal_w - 12, terminal_h - 26, 0);
    int visible_chars = (terminal_w - 38) / 6;
    if (visible_chars < 1) visible_chars = 1;
    int visible_rows = (terminal_h - 58) / 9;
    if (visible_rows < 1) visible_rows = 1;
    if (visible_rows > TERM_OUTPUT_SIZE) visible_rows = TERM_OUTPUT_SIZE;
    int first = terminal_output_count - visible_rows - terminal_output_scroll;
    if (first < 0) first = 0;
    int last = first + visible_rows;
    if (last > terminal_output_count) last = terminal_output_count;
    int row = 0;
    for (int i = first; i < last; ++i, ++row) {
        int length = 0;
        while (terminal_output[i][length] && length < visible_chars) ++length;
        text_n(terminal_x + 12, terminal_y + 22 + row * 9, terminal_output[i], length, 15);
    }
    text(terminal_x + 12, terminal_y + terminal_h - 29, "UP DOWN HIST  PGUP SCROLL", 8);
    text(terminal_x + 12, terminal_y + terminal_h - 17, ">", 10);
    int line_start = terminal_len > visible_chars ? terminal_len - visible_chars : 0;
    int visible_len = terminal_len - line_start;
    text_n(terminal_x + 24, terminal_y + terminal_h - 17, terminal_line + line_start, visible_len, 15);
    rect(terminal_x + 24 + visible_len * 6, terminal_y + terminal_h - 17, 4, 7, 14);
    rect(terminal_x + terminal_w - 8, terminal_y + terminal_h - 5, 5, 2, 8);
}

static void draw_browser_window(void) {
    rect(browser_x + 4, browser_y + 4, browser_w - 4, browser_h - 4, 0);
    rect(browser_x, browser_y, browser_w, browser_h, 7);
    rect(browser_x, browser_y, browser_w, 14, active_window == 3 ? 1 : 8);
    text(browser_x + 8, browser_y + 4, "TINYREPO", 15);
    draw_window_controls(browser_x, browser_y, browser_w, 3);
    rect(browser_x + 6, browser_y + 17, browser_w - 78, 13, 15);
    int address_chars = (browser_w - 88) / 6;
    if (address_chars < 1) address_chars = 1;
    int start = browser_url_len > address_chars ? browser_url_len - address_chars : 0;
    text_n(browser_x + 10, browser_y + 20, browser_url + start, browser_url_len - start, 0);
    button(browser_x + browser_w - 66, browser_y + 17, 28, 13, "RLD");
    button(browser_x + browser_w - 34, browser_y + 17, 26, 13, "GO");
    rect(browser_x + 6, browser_y + 33, browser_w - 12, browser_h - 64, browser_bg_color);
    int visible_cols = (browser_w - 20) / 6;
    int visible_lines = (browser_h - 64) / 9;
    if (visible_cols < 1) visible_cols = 1;
    if (visible_lines < 1) visible_lines = 1;
    int line = 0, col = 0;
    for (int i = 0; i < browser_text_len && line < browser_scroll + visible_lines; ++i) {
        char c = browser_text[i];
        if (c == '\n') { ++line; col = 0; continue; }
        if (col >= visible_cols) { ++line; col = 0; }
        if (line >= browser_scroll && line < browser_scroll + visible_lines)
            text_n(browser_x + 10 + col * 6, browser_y + 38 + (line - browser_scroll) * 9,
                   &browser_text[i], 1, browser_text_colors[i]);
        ++col;
    }
    text(browser_x + 8, browser_y + browser_h - 23, browser_status, 0);
    text(browser_x + browser_w - 92, browser_y + browser_h - 12, "UP DOWN SCROLL", 0);
    rect(browser_x + browser_w - 8, browser_y + browser_h - 5, 5, 2, 8);
}

static void draw_settings_window(void) {
    rect(settings_x + 4, settings_y + 4, settings_w - 4, settings_h - 4, 0);
    rect(settings_x, settings_y, settings_w, settings_h, 7);
    rect(settings_x, settings_y, settings_w, 14, active_window == 4 ? 1 : 8);
    text(settings_x + 10, settings_y + 4, "SETTINGS", 15);
    draw_window_controls(settings_x, settings_y, settings_w, 4);
    text(settings_x + 12, settings_y + 25, "TINYOS WALLPAPER", 0);
    const char *names[3] = {"SKY", "NIGHT", "FOREST"};
    for (int i = 0; i < 3; ++i) {
        int x = settings_x + 12 + i * 64;
        rect(x, settings_y + 43, 56, 38, i == 0 ? 9 : i == 1 ? 1 : 2);
        if (i == 0) { rect(x + 34, settings_y + 49, 10, 10, 14); rect(x, settings_y + 67, 56, 14, 2); }
        else if (i == 1) { rect(x + 12, settings_y + 50, 2, 2, 15); rect(x + 39, settings_y + 59, 2, 2, 14); rect(x, settings_y + 69, 56, 12, 8); }
        else { rect(x, settings_y + 68, 56, 13, 10); rect(x + 22, settings_y + 48, 8, 22, 6); }
        button(x, settings_y + 85, 56, 15, names[i]);
        if (wallpaper_id == i) rect(x + 2, settings_y + 102, 52, 2, 14);
    }
    text(settings_x + 12, settings_y + 119, "SELECT A WALLPAPER", 0);
    rect(settings_x + settings_w - 8, settings_y + settings_h - 5, 5, 2, 8);
}

static void draw_context_menu(void) {
    rect(context_x + 2, context_y + 2, 116, 42, 0);
    rect(context_x, context_y, 116, 42, 7);
    for (int i = 0; i < 3; ++i) {
        int y = context_y + 2 + i * 13;
        int hover = cursor_x >= context_x && cursor_x < context_x + 116 &&
                    cursor_y >= y && cursor_y < y + 13;
        if (hover) rect(context_x + 2, y, 112, 13, 1);
        text(context_x + 7, y + 3,
             i == 0 ? "OPEN FILES" : i == 1 ? "OPEN NOTES" : "CLOSE WINDOW",
             hover ? 15 : 0);
    }
}

static void draw_start_menu(void) {
    rect(6, 96, 124, 82, 0);
    rect(4, 94, 124, 82, 7);
    const char *items[6] = {"FILES", "NOTES", "TERMINAL", "BROWSER", "SETTINGS", "SHUTDOWN"};
    for (int i = 0; i < 6; ++i) {
        int y = 96 + i * 13;
        if (i == start_selected) rect(6, y, 120, 13, 1);
        text(12, y + 3, items[i], i == start_selected ? 15 : 0);
    }
}

static void draw_pointer(void) {
    static const u8 shape[10][8] = {
        {1,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0}, {1,2,2,1,0,0,0,0},
        {1,2,2,2,1,0,0,0}, {1,2,2,2,2,1,0,0},
        {1,2,2,2,2,2,1,0}, {1,2,2,1,1,1,0,0},
        {1,1,0,0,1,2,1,0}, {0,0,0,0,0,1,0,0}
    };
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 8; ++x)
            if (shape[y][x]) video[(cursor_y + y) * 320 + cursor_x + x] = shape[y][x] == 1 ? 0 : 15;
}

static void present_frame(void) {
    if (linear_framebuffer && framebuffer_bpp == 16 && framebuffer_pitch >= 1280) {
        static const u32 doubled_colors[16] = {
            0x00000000, 0x00150015, 0x05400540, 0x05550555,
            0xa800a800, 0xa815a815, 0xaa80aa80, 0xad55ad55,
            0x52aa52aa, 0x52bf52bf, 0x57ea57ea, 0x57ff57ff,
            0xfaaafaaa, 0xfabffabf, 0xffeaffea, 0xffffffff
        };
        volatile u8 *base = (volatile u8 *)linear_framebuffer;
        int y = 0;
        for (int sy = 0; sy < 200; ++sy) {
            int repeats = ((sy + 1) * 480 / 200) - (sy * 480 / 200);
            for (int x = 0; x < 320; ++x) {
                int pos = sy * 320 + x;
                u8 color = frame_buffer[pos];
                if (!framebuffer_force_redraw && presented_frame[pos] == color) continue;
                u32 pair = doubled_colors[color & 15];
                for (int repeat = 0; repeat < repeats; ++repeat) {
                    volatile u32 *row = (volatile u32 *)(base + (y + repeat) * framebuffer_pitch);
                    row[x] = pair;
                }
                presented_frame[pos] = color;
            }
            y += repeats;
        }
        framebuffer_force_redraw = 0;
        return;
    }
    const u32 *source = (const u32 *)frame_buffer;
    volatile u32 *destination = (volatile u32 *)VGA;
    u32 count = FRAME_SIZE / 4;
    __asm__ volatile ("cld; rep movsl"
                      : "+S"(source), "+D"(destination), "+c"(count)
                      : : "memory");
}

static void draw_desktop(void) {
    if (bsod_mode) {
        rect(0, 0, 320, 200, 1);
        rect(38, 32, 244, 136, 1);
        text(54, 46, "TINYOS STOPPED", 15);
        text(54, 70, "A PROBLEM WAS DETECTED", 15);
        text(54, 82, "AND THE SYSTEM PAUSED", 15);
        text(54, 106, "THIS SCREEN WAS REQUESTED", 15);
        text(54, 118, "BY THE BSOD COMMAND", 15);
        text(54, 146, "R RESTART  ESC RESUME", 14);
        present_frame();
        return;
    }
    draw_wallpaper();
    draw_clock();

    /* Desktop shortcuts. */
    rect(15, 35, 28, 17, 14);
    rect(15, 39, 28, 13, 6);
    text(10, 55, "FILES", 15);
    rect(17, 75, 23, 21, 15);
    rect(20, 79, 17, 2, 8);
    rect(20, 84, 17, 2, 8);
    rect(20, 89, 12, 2, 8);
    text(10, 99, "NOTES", 15);

    rect(0, 184, 320, 16, 8);
    rect(4, 186, 42, 12, 7);
    text(8, 188, "START", 0);
    rect(50, 186, 54, 12, files_open && !app_minimized[0] ? 7 : 8);
    text(54, 188, "FILES", 0);
    rect(108, 186, 54, 12, notes_open && !app_minimized[1] ? 7 : 8);
    text(112, 188, "NOTES", 0);
    rect(166, 186, 52, 12, terminal_open && !app_minimized[2] ? 7 : 8);
    text(170, 188, "TERM", 0);
    rect(222, 186, 50, 12, browser_open && !app_minimized[3] ? 7 : 8);
    text(226, 188, "WEB", 0);
    rect(274, 186, 44, 12, settings_open && !app_minimized[4] ? 7 : 8);
    text(278, 188, "SET", 0);

    if (files_open && !app_minimized[0] && active_window != 0) draw_file_manager();
    if (notes_open && !app_minimized[1] && active_window != 1) draw_notes_window();
    if (terminal_open && !app_minimized[2] && active_window != 2) draw_terminal_window();
    if (browser_open && !app_minimized[3] && active_window != 3) draw_browser_window();
    if (settings_open && !app_minimized[4] && active_window != 4) draw_settings_window();
    if (files_open && !app_minimized[0] && active_window == 0) draw_file_manager();
    if (notes_open && !app_minimized[1] && active_window == 1) draw_notes_window();
    if (terminal_open && !app_minimized[2] && active_window == 2) draw_terminal_window();
    if (browser_open && !app_minimized[3] && active_window == 3) draw_browser_window();
    if (settings_open && !app_minimized[4] && active_window == 4) draw_settings_window();
    if (context_open) draw_context_menu();
    if (start_open) draw_start_menu();
    draw_pointer();
    present_frame();
}

static int alt_down;
static int shift_down;
static int ctrl_down;
static int extended_key;
static char typed_char;

static char scan_ascii(u8 code) {
    static const char letters[26] = "QWERTYUIOPASDFGHJKLZXCVBNM";
    static const u8 scans[26] = {
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,
        0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,
        0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32
    };
    for (int i = 0; i < 26; ++i) if (scans[i] == code) return letters[i];
    if (code >= 0x02 && code <= 0x0a) return (char)('1' + code - 0x02);
    if (code == 0x0b) return '0';
    if (code == 0x39) return ' ';
    if (code == 0x34) return '.';
    if (code == 0x35) return '/';
    if (code == 0x0c) return shift_down ? '_' : '-';
    if (code == 0x27) return ':';
    if (code == 0x28) return shift_down ? '"' : '\'';
    if (code == 0x0d) return shift_down ? '+' : '=';
    return 0;
}

static int keyboard_event(u8 code) {
    if (code == 0x38) { alt_down = 1; return 0; }
    if (code == 0xb8) { alt_down = 0; return 0; }
    if (code == 0x1d) { ctrl_down = 1; return 0; }
    if (code == 0x9d) { ctrl_down = 0; return 0; }
    if (code == 0x2a || code == 0x36) { shift_down = 1; return 0; }
    if (code == 0xaa || code == 0xb6) { shift_down = 0; return 0; }
    if (code == 0x0f && alt_down) return 12; /* Alt+Tab */
    if (code == 0x44 && shift_down) return 17; /* Shift+F10 context menu */
    if (code == 0x01 && ctrl_down) return 18; /* Ctrl+Esc Start menu */
    if (code == 0xe0) { extended_key = 1; return 0; }
    if (code & 0x80) { extended_key = 0; return 0; }
    if (extended_key) {
        extended_key = 0;
        if (code == 0x4b) return 1;
        if (code == 0x4d) return 2;
        if (code == 0x48) return 3;
        if (code == 0x50) return 4;
        if (code == 0x47) return 19; /* Home */
        if (code == 0x4f) return 20; /* End */
        if (code == 0x53) return 21; /* Delete */
        if (code == 0x49) return 23; /* Page up */
        if (code == 0x51) return 24; /* Page down */
        return 0;
    }
    if ((notes_open && active_window == 1) || (terminal_open && active_window == 2) ||
        (browser_open && active_window == 3)) {
        if (code == 0x0e) return 16;
        if (code == 0x1c) return 10;
        if (code == 0x01) return 11;
        if (notes_open && active_window == 1 && ctrl_down && code == 0x1f) return 14;
        if (browser_open && active_window == 3 && ctrl_down && code == 0x1e) return 22;
        typed_char = scan_ascii(code);
        return typed_char ? 15 : 0;
    }
    if (code == 0x11) return 5; /* W */
    if (code == 0x10) return 6; /* Q */
    if (code == 0x31) return 7; /* N */
    if (code == 0x2d) return 9; /* X */
    if (code == 0x1c) return 10; /* Enter */
    if (code == 0x01) return 11; /* Esc */
    if (code == 0x14) return 13; /* T */
    return 0;
}

static void wait_input_empty(void) {
    for (u32 i = 0; i < 100000; ++i)
        if (!(inb(0x64) & 2)) return;
}

static int mouse_command(u8 command) {
    wait_input_empty();
    outb(0x64, 0xd4);
    wait_input_empty();
    outb(0x60, command);
    for (u32 i = 0; i < 100000; ++i) {
        u8 status = inb(0x64);
        if (status & 1) {
            u8 response = inb(0x60);
            if (status & 0x20) return response == 0xfa;
        }
    }
    return 0;
}

static int mouse_read_id(u8 *id) {
    for (u32 i = 0; i < 100000; ++i) {
        u8 status = inb(0x64);
        if (status & 1) {
            u8 value = inb(0x60);
            if (status & 0x20) { *id = value; return 1; }
        }
    }
    return 0;
}

static void mouse_init(void) {
    wait_input_empty();
    outb(0x64, 0xa8); /* enable auxiliary PS/2 port */
    (void)mouse_command(0xf6);
    /* IntelliMouse sample-rate sequence enables a signed wheel byte on QEMU. */
    if (mouse_command(0xf3) && mouse_command(200) && mouse_command(0xf3) &&
        mouse_command(100) && mouse_command(0xf3) && mouse_command(80) && mouse_command(0xf2)) {
        u8 id = 0;
        if (mouse_read_id(&id) && (id == 3 || id == 4)) mouse_packet_size = 4;
    }
    (void)mouse_command(0xf4);
    mouse_packet_index = 0;
    mouse_buttons = 0;
}

static void open_files(void) {
    app_minimized[0] = 0;
    if (!files_open) {
        files_open = 1;
        if ((fs_loaded || fs_load()) && selected_entry < 0) select_next(1);
    }
    active_window = 0;
    context_open = 0;
    start_open = 0;
    ui_dirty = 1;
}

static void open_notes(void) {
    app_minimized[1] = 0;
    if (!fs_loaded && !fs_load()) {
        notes_open = 1;
        editing_entry = -1;
        active_window = 1;
        ui_dirty = 1;
        return;
    }
    int index = fs_find_name("NOTES.TXT");
    if (index < 0) index = fs_create_named("NOTES.TXT");
    if (index >= 0) edit_entry(index, 0);
    else {
        notes_open = 1;
        editing_entry = -1;
        active_window = 1;
    }
    start_open = 0;
    context_open = 0;
    ui_dirty = 1;
}

static void open_terminal(void) {
    app_minimized[2] = 0;
    if (!fs_loaded) (void)fs_load();
    terminal_open = 1;
    active_window = 2;
    start_open = 0;
    context_open = 0;
    ui_dirty = 1;
}

static void open_browser(void) {
    app_minimized[3] = 0;
    browser_open = 1;
    active_window = 3;
    start_open = 0;
    context_open = 0;
    browser_address_active = 1;
    if (!browser_url_len) {
        const char *initial = "tiny://10.0.2.2/repo.json";
        while (initial[browser_url_len] && browser_url_len < (int)sizeof browser_url - 1) {
            browser_url[browser_url_len] = initial[browser_url_len];
            ++browser_url_len;
        }
        browser_url[browser_url_len] = 0;
    }
    if (!browser_status[0]) {
        const char *message = "ENTER TO LOAD";
        int i = 0; while (message[i]) { browser_status[i] = message[i]; ++i; }
        browser_status[i] = 0;
    }
    ui_dirty = 1;
}

static void open_settings(void) {
    settings_open = 1;
    app_minimized[4] = 0;
    active_window = 4;
    start_open = 0;
    context_open = 0;
    ui_dirty = 1;
}

static void close_active_window(void) {
    if (active_window == 0 && files_open) {
        files_open = 0;
        if (notes_open) active_window = 1;
        else if (terminal_open) active_window = 2;
        else if (browser_open) active_window = 3;
    } else if (active_window == 1 && notes_open) {
        close_editor();
    } else if (active_window == 2 && terminal_open) {
        terminal_open = 0;
        if (files_open) active_window = 0;
        else if (notes_open) active_window = 1;
        else if (browser_open) active_window = 3;
    } else if (active_window == 3 && browser_open) {
        browser_open = 0;
        if (terminal_open) active_window = 2;
        else if (notes_open) active_window = 1;
        else if (files_open) active_window = 0;
    } else if (active_window == 4 && settings_open) {
        settings_open = 0;
    }
    active_window = -1;
    switch_task();
    dragging_window = -1;
    context_open = 0;
    ui_dirty = 1;
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void shutdown_os(void) {
    if (notes_open && editing_entry >= 0 && !editor_saved) (void)save_editor();
    outw(0x0604, 0x2000); /* ACPI power button control on QEMU PC */
    outw(0xb004, 0x2000);
    for (;;) __asm__ volatile ("cli; hlt");
}

static void context_action(int item) {
    if (item == 0) open_files();
    else if (item == 1) open_notes();
    else close_active_window();
}

static void start_action(int item) {
    start_open = 0;
    if (item == 0) open_files();
    else if (item == 1) open_notes();
    else if (item == 2) open_terminal();
    else if (item == 3) open_browser();
    else if (item == 4) open_settings();
    else shutdown_os();
}

static void click_start_menu(void) {
    int row = (cursor_y - 96) / 13;
    if (cursor_x >= 4 && cursor_x < 128 && cursor_y >= 96 && cursor_y < 174 && row >= 0 && row < 6)
        start_action(row);
    else start_open = 0;
    ui_dirty = 1;
}

static void switch_task(void) {
    for (int step = 1; step <= 5; ++step) {
        int app = (active_window + step) % 5;
        if (app_is_open(app) && !app_minimized[app]) {
            active_window = app;
            ui_dirty = 1;
            return;
        }
    }
    active_window = -1;
    ui_dirty = 1;
}

static void click_context_menu(void) {
    int row = (cursor_y - context_y - 2) / 13;
    if (cursor_x >= context_x && cursor_x < context_x + 116 && row >= 0 && row < 3)
        context_action(row);
    else context_open = 0;
    ui_dirty = 1;
}

static void focus_window(int app) {
    active_window = app;
    dragging_window = -1;
    if (app == 0 && files_open && in_rect(cursor_x, cursor_y, file_x, file_y, file_w, file_h)) {
        if (cursor_x >= file_x + file_w - 12 && cursor_y >= file_y + file_h - 10) {
            dragging_window = 4;
            drag_offset_x = cursor_x - file_x;
            drag_offset_y = cursor_y - file_y;
        } else
        if (cursor_y < file_y + 14) {
            if (cursor_x >= file_x + file_w - 16) { close_active_window(); return; }
            if (cursor_x >= file_x + file_w - 30) { window_maximize_toggle(0); return; }
            if (cursor_x >= file_x + file_w - 44) { window_minimize(0); return; }
            dragging_window = 0;
            drag_offset_x = cursor_x - file_x;
            drag_offset_y = cursor_y - file_y;
        } else if (cursor_y >= file_y + 21 && cursor_y < file_y + file_h - 46) {
            int tile_w = (file_w - 24) / 2;
            int local_x = cursor_x - (file_x + 8);
            int col = local_x / (tile_w + 4);
            int row = (cursor_y - file_y - 21) / 36;
            if (col >= 0 && col < 2 && row >= 0 && row < file_visible_tiles() / 2 &&
                local_x % (tile_w + 4) < tile_w && (cursor_y - file_y - 21) % 36 < 33) {
                int index = file_at_ordinal(file_scroll_index + row * 2 + col);
                if (index >= 0) selected_entry = index;
            }
        } else if (file_h >= 158 && cursor_y >= file_y + file_h - 42 && cursor_y < file_y + file_h - 29) {
            if (cursor_x >= file_x + 8 && cursor_x < file_x + 56) {
                int created = fs_add_numbered();
                if (created >= 0) selected_entry = created;
            } else if (cursor_x >= file_x + 62 && cursor_x < file_x + 116) {
                if (selected_entry >= 0) edit_entry(selected_entry, 1);
            } else if (cursor_x >= file_x + 122 && cursor_x < file_x + 172) {
                (void)fs_delete_selected();
            }
        }
    } else if (app == 1 && notes_open && in_rect(cursor_x, cursor_y, notes_x, notes_y, notes_w, notes_h)) {
        if (cursor_x >= notes_x + notes_w - 12 && cursor_y >= notes_y + notes_h - 10) {
            dragging_window = 5;
            drag_offset_x = cursor_x - notes_x;
            drag_offset_y = cursor_y - notes_y;
        } else
        if (cursor_y < notes_y + 14) {
            if (cursor_x >= notes_x + notes_w - 16) { close_active_window(); return; }
            if (cursor_x >= notes_x + notes_w - 30) { window_maximize_toggle(1); return; }
            if (cursor_x >= notes_x + notes_w - 44) { window_minimize(1); return; }
            dragging_window = 1;
            drag_offset_x = cursor_x - notes_x;
            drag_offset_y = cursor_y - notes_y;
        } else if (editing_entry >= 0 &&
                   in_rect(cursor_x, cursor_y, notes_x + 6, notes_y + 18, 228, 118)) {
            int row = (cursor_y - notes_y - 23) / 9;
            int col = (cursor_x - notes_x - 12) / 6;
            if (row < 0) row = 0;
            int visible_lines = (notes_h - 40) / 9;
            if (visible_lines > 11) visible_lines = 11;
            if (row >= visible_lines) row = visible_lines - 1;
            if (col < 0) col = 0;
            if (col > 34) col = 34;
            editor_cursor = editor_index_at(row + editor_scroll_line, col);
        }
    } else if (app == 2 && terminal_open && in_rect(cursor_x, cursor_y, terminal_x, terminal_y, terminal_w, terminal_h)) {
        if (cursor_x >= terminal_x + terminal_w - 12 && cursor_y >= terminal_y + terminal_h - 10) {
            dragging_window = 6;
            drag_offset_x = cursor_x - terminal_x;
            drag_offset_y = cursor_y - terminal_y;
        } else
        if (cursor_y < terminal_y + 14) {
            if (cursor_x >= terminal_x + terminal_w - 16) { close_active_window(); return; }
            if (cursor_x >= terminal_x + terminal_w - 30) { window_maximize_toggle(2); return; }
            if (cursor_x >= terminal_x + terminal_w - 44) { window_minimize(2); return; }
            dragging_window = 2;
            drag_offset_x = cursor_x - terminal_x;
            drag_offset_y = cursor_y - terminal_y;
        }
    } else if (app == 3 && browser_open && in_rect(cursor_x, cursor_y, browser_x, browser_y, browser_w, browser_h)) {
        if (cursor_x >= browser_x + browser_w - 12 && cursor_y >= browser_y + browser_h - 10) {
            dragging_window = 7;
            drag_offset_x = cursor_x - browser_x;
            drag_offset_y = cursor_y - browser_y;
        } else
        if (cursor_y < browser_y + 14) {
            if (cursor_x >= browser_x + browser_w - 16) { close_active_window(); return; }
            if (cursor_x >= browser_x + browser_w - 30) { window_maximize_toggle(3); return; }
            if (cursor_x >= browser_x + browser_w - 44) { window_minimize(3); return; }
            dragging_window = 3;
            drag_offset_x = cursor_x - browser_x;
            drag_offset_y = cursor_y - browser_y;
        } else if (cursor_y < browser_y + 31 && cursor_x >= browser_x + browser_w - 34) {
            browser_load_url();
            browser_address_active = 0;
        } else if (cursor_y < browser_y + 31 && cursor_x >= browser_x + browser_w - 66) {
            browser_load_url();
        } else if (cursor_y < browser_y + 31) {
            browser_address_active = 1;
        }
    } else if (app == 4 && settings_open && in_rect(cursor_x, cursor_y, settings_x, settings_y, settings_w, settings_h)) {
        if (cursor_x >= settings_x + settings_w - 12 && cursor_y >= settings_y + settings_h - 10) {
            dragging_window = 8;
            drag_offset_x = cursor_x - settings_x;
            drag_offset_y = cursor_y - settings_y;
        } else if (cursor_y < settings_y + 14) {
            if (cursor_x >= settings_x + settings_w - 16) { close_active_window(); return; }
            if (cursor_x >= settings_x + settings_w - 30) { window_maximize_toggle(4); return; }
            if (cursor_x >= settings_x + settings_w - 44) { window_minimize(4); return; }
            dragging_window = 9;
            drag_offset_x = cursor_x - settings_x;
            drag_offset_y = cursor_y - settings_y;
        } else if (cursor_y >= settings_y + 85 && cursor_y < settings_y + 100) {
            int option = (cursor_x - settings_x - 12) / 64;
            if (option >= 0 && option < 3) wallpaper_set(option);
        }
    }
    ui_dirty = 1;
}

static void mouse_click(void) {
    if (bsod_mode) return;
    if (start_open) { click_start_menu(); return; }
    if (context_open) { click_context_menu(); return; }
    if (cursor_y >= 184) {
        if (cursor_x >= 50 && cursor_x < 104) open_files();
        else if (cursor_x >= 108 && cursor_x < 162) open_notes();
        else if (cursor_x >= 166 && cursor_x < 220) open_terminal();
        else if (cursor_x >= 222 && cursor_x < 274) open_browser();
        else if (cursor_x >= 274 && cursor_x < 320) open_settings();
        else if (cursor_x < 46) {
            start_open = 1; start_selected = 0; context_open = 0; ui_dirty = 1;
        }
        return;
    }

    /* Focus whichever visible window is on top at the pointer location. */
    if (active_window == 2 && terminal_open && !app_minimized[2] && in_rect(cursor_x, cursor_y, terminal_x, terminal_y, terminal_w, terminal_h)) {
        focus_window(2); return;
    }
    if (active_window == 3 && browser_open && !app_minimized[3] && in_rect(cursor_x, cursor_y, browser_x, browser_y, browser_w, browser_h)) {
        focus_window(3); return;
    }
    if (active_window == 4 && settings_open && !app_minimized[4] && in_rect(cursor_x, cursor_y, settings_x, settings_y, settings_w, settings_h)) {
        focus_window(4); return;
    }
    if (active_window == 1 && notes_open && !app_minimized[1] && in_rect(cursor_x, cursor_y, notes_x, notes_y, notes_w, notes_h)) {
        focus_window(1); return;
    }
    if (active_window == 0 && files_open && !app_minimized[0] && in_rect(cursor_x, cursor_y, file_x, file_y, file_w, file_h)) {
        focus_window(0); return;
    }
    if (terminal_open && !app_minimized[2] && in_rect(cursor_x, cursor_y, terminal_x, terminal_y, terminal_w, terminal_h)) {
        focus_window(2); return;
    }
    if (browser_open && !app_minimized[3] && in_rect(cursor_x, cursor_y, browser_x, browser_y, browser_w, browser_h)) {
        focus_window(3); return;
    }
    if (settings_open && !app_minimized[4] && in_rect(cursor_x, cursor_y, settings_x, settings_y, settings_w, settings_h)) {
        focus_window(4); return;
    }
    if (notes_open && !app_minimized[1] && in_rect(cursor_x, cursor_y, notes_x, notes_y, notes_w, notes_h)) {
        focus_window(1); return;
    }
    if (files_open && !app_minimized[0] && in_rect(cursor_x, cursor_y, file_x, file_y, file_w, file_h)) {
        focus_window(0); return;
    }
    if (cursor_x >= 8 && cursor_x < 68 && cursor_y >= 32 && cursor_y < 72) {
        open_files(); return;
    }
    if (cursor_x >= 8 && cursor_x < 68 && cursor_y >= 72 && cursor_y < 116) {
        open_notes(); return;
    }
    ui_dirty = 1;
}

static int mouse_byte(u8 value) {
    if (mouse_packet_index == 0 && !(value & 8)) return 0;
    mouse_packet[mouse_packet_index++] = value;
    if (mouse_packet_index < mouse_packet_size) return 0;
    mouse_packet_index = 0;

    int old_x = cursor_x, old_y = cursor_y;
    int old_buttons = mouse_buttons;
    mouse_buttons = mouse_packet[0] & 7;
    if (!(mouse_packet[0] & 0xc0)) {
        cursor_x += (s8)mouse_packet[1];
        cursor_y -= (s8)mouse_packet[2];
    }
    if (mouse_packet_size == 4) {
        int wheel = mouse_packet[3] & 0x0f;
        if (wheel & 8) wheel -= 16;
        if (wheel) {
            int app = active_window;
            if (app == 0 && files_open && !app_minimized[0]) {
                file_scroll_index += wheel * 2;
                int maximum = file_count() - file_visible_tiles();
                if (maximum < 0) maximum = 0;
                if (file_scroll_index < 0) file_scroll_index = 0;
                if (file_scroll_index > maximum) file_scroll_index = maximum;
                ui_dirty = 1;
            } else if (app == 1 && notes_open && !app_minimized[1]) {
                editor_move_vertical(wheel * 3);
                ui_dirty = 1;
            } else if (app == 2 && terminal_open && !app_minimized[2]) {
                terminal_output_scroll -= wheel * 3;
                if (terminal_output_scroll < 0) terminal_output_scroll = 0;
                if (terminal_output_scroll >= terminal_output_count) terminal_output_scroll = terminal_output_count ? terminal_output_count - 1 : 0;
                ui_dirty = 1;
            } else if (app == 3 && browser_open && !app_minimized[3]) {
                browser_scroll += wheel * 3;
                if (browser_scroll < 0) browser_scroll = 0;
                ui_dirty = 1;
            }
        }
    }
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_x > 312) cursor_x = 312;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_y > 190) cursor_y = 190;

    if ((mouse_buttons & 1) && dragging_window == 0) {
        file_x = cursor_x - drag_offset_x;
        file_y = cursor_y - drag_offset_y;
        if (file_x < 0) file_x = 0;
        if (file_x > 320 - file_w) file_x = 320 - file_w;
        if (file_y < 0) file_y = 0;
        if (file_y > 184 - file_h) file_y = 184 - file_h;
    } else if ((mouse_buttons & 1) && dragging_window == 1) {
        notes_x = cursor_x - drag_offset_x;
        notes_y = cursor_y - drag_offset_y;
        if (notes_x < 0) notes_x = 0;
        if (notes_x > 320 - notes_w) notes_x = 320 - notes_w;
        if (notes_y < 0) notes_y = 0;
        if (notes_y > 184 - notes_h) notes_y = 184 - notes_h;
    } else if ((mouse_buttons & 1) && dragging_window == 2) {
        terminal_x = cursor_x - drag_offset_x;
        terminal_y = cursor_y - drag_offset_y;
        if (terminal_x < 0) terminal_x = 0;
        if (terminal_x > 320 - terminal_w) terminal_x = 320 - terminal_w;
        if (terminal_y < 0) terminal_y = 0;
        if (terminal_y > 184 - terminal_h) terminal_y = 184 - terminal_h;
    } else if ((mouse_buttons & 1) && dragging_window == 3) {
        browser_x = cursor_x - drag_offset_x;
        browser_y = cursor_y - drag_offset_y;
        if (browser_x < 0) browser_x = 0;
        if (browser_x > 320 - browser_w) browser_x = 320 - browser_w;
        if (browser_y < 0) browser_y = 0;
        if (browser_y > 184 - browser_h) browser_y = 184 - browser_h;
    } else if ((mouse_buttons & 1) && dragging_window == 9) {
        settings_x = cursor_x - drag_offset_x;
        settings_y = cursor_y - drag_offset_y;
        if (settings_x < 0) settings_x = 0;
        if (settings_x > 320 - settings_w) settings_x = 320 - settings_w;
        if (settings_y < 0) settings_y = 0;
        if (settings_y > 184 - settings_h) settings_y = 184 - settings_h;
    } else if ((mouse_buttons & 1) && dragging_window >= 4 && dragging_window <= 8) {
        int width, height;
        int x = 0, y = 0, min_w = 200, min_h = 110;
        int *target_w = 0, *target_h = 0;
        if (dragging_window == 4) { x = file_x; y = file_y; min_w = 210; min_h = 120; target_w = &file_w; target_h = &file_h; }
        else if (dragging_window == 5) { x = notes_x; y = notes_y; min_w = 220; min_h = 120; target_w = &notes_w; target_h = &notes_h; }
        else if (dragging_window == 6) { x = terminal_x; y = terminal_y; min_w = 200; min_h = 110; target_w = &terminal_w; target_h = &terminal_h; }
        else if (dragging_window == 7) { x = browser_x; y = browser_y; min_w = 200; min_h = 120; target_w = &browser_w; target_h = &browser_h; }
        else { x = settings_x; y = settings_y; min_w = 220; min_h = 142; target_w = &settings_w; target_h = &settings_h; }
        width = cursor_x - x + 1;
        height = cursor_y - y + 1;
        if (width < min_w) width = min_w;
        if (height < min_h) height = min_h;
        if (width > 320 - x) width = 320 - x;
        if (height > 184 - y) height = 184 - y;
        *target_w = width;
        *target_h = height;
    }

    if (!(mouse_buttons & 1) && (old_buttons & 1)) dragging_window = -1;
    if ((mouse_buttons & 1) && !(old_buttons & 1)) mouse_click();
    if ((mouse_buttons & 6) && !(old_buttons & 6)) {
        context_open = 1;
        start_open = 0;
        context_x = cursor_x > 202 ? 202 : cursor_x;
        context_y = cursor_y > 140 ? 140 : cursor_y;
        ui_dirty = 1;
    }
    if (old_x != cursor_x || old_y != cursor_y || mouse_buttons != old_buttons)
        ui_dirty = 1;
    return 1;
}

static int input_poll(void) {
    u8 status = inb(0x64);
    if (!(status & 1)) return 0;
    u8 data = inb(0x60);
    if (status & 0x20) { mouse_byte(data); return 0; }
    return keyboard_event(data);
}

static void reboot(void) {
    for (volatile int i = 0; i < 100000; ++i) {}
    outb(0x64, 0xfe);
    for (;;) __asm__ volatile ("hlt");
}

void kernel_main(void) {
    for (u8 *p = &__bss_start; p < &__bss_end; ++p) *p = 0;
    framebuffer_force_redraw = 1;
    if (*(volatile u32 *)0x05f0 == 0x31454256) {
        volatile u8 *info = (volatile u8 *)0x0600;
        framebuffer_pitch = (u16)info[16] | ((u16)info[17] << 8);
        framebuffer_bpp = info[25];
        linear_framebuffer = (u32)info[40] | ((u32)info[41] << 8) |
                            ((u32)info[42] << 16) | ((u32)info[43] << 24);
    }
    mouse_init();
    net_ready = ne2k_init();
    if (fs_load()) settings_load();
    rtc_poll_count = 49999;
    clock_refresh();
    draw_desktop();
    for (;;) {
        clock_refresh();
        int key = input_poll();
        if (bsod_mode) {
            if (key == 10 || (key == 15 && typed_char == 'R')) reboot();
            else if (key == 11) { bsod_mode = 0; ui_dirty = 1; }
        } else if (key == 6) reboot();
        else if (key == 18) {
            start_open = 1;
            start_selected = 0;
            context_open = 0;
            ui_dirty = 1;
        } else if (key == 12) {
            start_open = 0;
            context_open = 0;
            switch_task();
        } else if (key == 17) {
            context_open = 1;
            start_open = 0;
            context_x = cursor_x > 202 ? 202 : cursor_x;
            context_y = cursor_y > 140 ? 140 : cursor_y;
            ui_dirty = 1;
        } else if (start_open) {
            if (key == 11) start_open = 0;
            else if (key == 3 && start_selected > 0) --start_selected;
            else if (key == 4 && start_selected < 5) ++start_selected;
            else if (key == 10) start_action(start_selected);
            ui_dirty = 1;
        } else if (key == 14 && notes_open && active_window == 1) {
            editor_saved = save_editor();
            ui_dirty = 1;
        } else if (key == 5) {
            open_files();
        } else if (key == 13) {
            open_notes();
        } else if (key == 11 && (context_open || start_open)) {
            context_open = 0;
            start_open = 0;
            ui_dirty = 1;
        } else if (notes_open && active_window == 1) {
            if (key == 11) close_editor();
            else if (editing_entry >= 0 && key == 1 && editor_cursor > 0) {
                --editor_cursor;
                ui_dirty = 1;
            } else if (editing_entry >= 0 && key == 2 && editor_cursor < editor_len) {
                ++editor_cursor;
                ui_dirty = 1;
            } else if (editing_entry >= 0 && key == 3) {
                editor_move_vertical(-1);
                ui_dirty = 1;
            } else if (editing_entry >= 0 && key == 4) {
                editor_move_vertical(1);
                ui_dirty = 1;
            } else if (editing_entry >= 0 && key == 19) {
                int row, col;
                editor_row_col(editor_cursor, &row, &col);
                editor_cursor = editor_index_at(row, 0);
                ui_dirty = 1;
            } else if (editing_entry >= 0 && key == 20) {
                int row, col;
                editor_row_col(editor_cursor, &row, &col);
                editor_cursor = editor_index_at(row, 34);
                ui_dirty = 1;
            } else if (editing_entry >= 0 && key == 21) {
                editor_delete();
                ui_dirty = 1;
            } else if (editing_entry >= 0 && key == 15) {
                editor_insert_char(typed_char);
                ui_dirty = 1;
            } else if (editing_entry >= 0 && key == 10) {
                editor_insert_char('\n');
                ui_dirty = 1;
            } else if (editing_entry >= 0 && key == 16) {
                editor_backspace();
                ui_dirty = 1;
            }
        } else if (settings_open && active_window == 4) {
            if (key == 11) close_active_window();
            else if (key == 1) wallpaper_set((wallpaper_id + 2) % 3);
            else if (key == 2) wallpaper_set((wallpaper_id + 1) % 3);
        } else if (terminal_open && active_window == 2) {
            if (key == 11) close_active_window();
            else if (key == 3) terminal_history_move(-1);
            else if (key == 4) terminal_history_move(1);
            else if (key == 19) { terminal_output_scroll = terminal_output_count; ui_dirty = 1; }
            else if (key == 20 || key == 24) {
                int page = (terminal_h - 58) / 9;
                if (page < 1) page = 1;
                terminal_output_scroll -= key == 20 ? terminal_output_count : page;
                if (terminal_output_scroll < 0) terminal_output_scroll = 0;
                ui_dirty = 1;
            } else if (key == 23) {
                int page = (terminal_h - 58) / 9;
                if (page < 1) page = 1;
                terminal_output_scroll += page;
                if (terminal_output_scroll > terminal_output_count) terminal_output_scroll = terminal_output_count;
                ui_dirty = 1;
            }
            else if (key == 15 && terminal_len < 34) {
                terminal_line[terminal_len++] = typed_char;
                terminal_line[terminal_len] = 0;
                terminal_history_pos = -1;
                ui_dirty = 1;
            } else if (key == 16 && terminal_len > 0) {
                terminal_line[--terminal_len] = 0;
                terminal_history_pos = -1;
                ui_dirty = 1;
            } else if (key == 10) terminal_run_command();
        } else if (browser_open && active_window == 3) {
            if (key == 11) close_active_window();
            else if (key == 22) {
                browser_url_len = 0;
                browser_url[0] = 0;
                browser_address_active = 1;
                ui_dirty = 1;
            }
            else if (key == 10) {
                browser_load_url();
                browser_address_active = 0;
                ui_dirty = 1;
            } else if (key == 15 && browser_url_len < (int)sizeof browser_url - 1) {
                browser_url[browser_url_len++] = typed_char;
                browser_url[browser_url_len] = 0;
                browser_address_active = 1;
                ui_dirty = 1;
            } else if (key == 16 && browser_url_len > 0) {
                browser_url[--browser_url_len] = 0;
                browser_address_active = 1;
                ui_dirty = 1;
            } else if (key == 3 && browser_scroll > 0) {
                --browser_scroll;
                ui_dirty = 1;
            } else if (key == 4) {
            int lines = 1, col = 0;
            int visible_cols = (browser_w - 20) / 6;
            int visible_lines = (browser_h - 64) / 9;
            if (visible_cols < 1) visible_cols = 1;
            if (visible_lines < 1) visible_lines = 1;
                for (int i = 0; i < browser_text_len; ++i) {
                if (browser_text[i] == '\n' || ++col >= visible_cols) { ++lines; col = 0; }
                }
            if (browser_scroll < lines - visible_lines) { ++browser_scroll; ui_dirty = 1; }
            }
        } else if (key && files_open && active_window == 0) {
            if (key == 11) {
                close_active_window();
            } else if (key == 3) {
                select_next(-1);
            } else if (key == 4) {
                select_next(1);
            } else if (key == 7 && !fs_error) {
                (void)fs_add_numbered();
            } else if (key == 9 && !fs_error) {
                fs_delete_selected();
                select_next(1);
            } else if (key == 10 && selected_entry >= 0 && entry_used(selected_entry)) {
                edit_entry(selected_entry, 1);
            }
            ui_dirty = 1;
        } else if (key == 1 || key == 2 || key == 3 || key == 4) {
            if (key == 1 && cursor_x > 2) cursor_x -= 3;
            if (key == 2 && cursor_x < 310) cursor_x += 3;
            if (key == 3 && cursor_y > 2) cursor_y -= 3;
            if (key == 4 && cursor_y < 188) cursor_y += 3;
            ui_dirty = 1;
        }
        if (ui_dirty) {
            draw_desktop();
            ui_dirty = 0;
        }
    }
}
