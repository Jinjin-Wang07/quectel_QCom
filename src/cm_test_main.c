/*
    Copyright 2025 Quectel Wireless Solutions Co.,Ltd

    Quectel hereby grants customers of Quectel a license to use, modify,
    distribute and publish the Software in binary form provided that
    customers shall have no right to reverse engineer, reverse assemble,
    decompile or reduce to source code form any portion of the Software.
    Under no circumstances may customers modify, demonstrate, use, deliver
    or disclose any portion of the Software in source code form.
*/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_SIZE(items) (sizeof(items) / sizeof((items)[0]))
#define MAX_VALUE_LEN 256
#define MAX_COMMAND_LEN 512
#define MAX_RESPONSE_LEN 8192
#define MAX_ARGS 48

static int s_critical_only;
static char s_last_if_state[MAX_VALUE_LEN];
static char s_last_dns_servers[MAX_COMMAND_LEN];

typedef struct {
    char config_file[MAX_VALUE_LEN];
    char cm_path[MAX_VALUE_LEN];
    char log_dir[MAX_VALUE_LEN];
    char log_file[MAX_VALUE_LEN];
    char cm_log_file[MAX_VALUE_LEN];
    char at_device[MAX_VALUE_LEN];
    char at_map_file[MAX_VALUE_LEN];
    char rat[MAX_VALUE_LEN];
    char band[MAX_VALUE_LEN];
    char info_commands[MAX_COMMAND_LEN];
    char apn[MAX_VALUE_LEN];
    char user[MAX_VALUE_LEN];
    char password[MAX_VALUE_LEN];
    char auth[MAX_VALUE_LEN];
    char pin[MAX_VALUE_LEN];
    char proxy[MAX_VALUE_LEN];
    char interface[MAX_VALUE_LEN];
    char pdp[MAX_VALUE_LEN];
    char qmap_iface_idx[MAX_VALUE_LEN];
    char udhcpc_script[MAX_VALUE_LEN];
    char ping_address[MAX_VALUE_LEN];
    int enable_ipv4;
    int enable_ipv6;
    int no_dhcp;
    int bridge;
    int verbose;
    int connect_duration_sec;
    int connection_times;
    int sleep_between_sec;
    int connect_wait_sec;
    int status_interval_sec;
    int ping_enable;
    int ping_count;
    int ping_timeout_sec;
    int at_timeout_sec;
    int critical_log_only;
} CM_TEST_CONFIG;

static FILE *s_log_fp;
static volatile sig_atomic_t s_stop_requested;

static int contains_word_case_insensitive(const char *text, const char *word)
{
    size_t text_len;
    size_t word_len;
    size_t i;
    size_t j;

    if (!text || !word)
        return 0;

    text_len = strlen(text);
    word_len = strlen(word);
    if (!word_len || word_len > text_len)
        return 0;

    for (i = 0; i + word_len <= text_len; i++) {
        for (j = 0; j < word_len; j++) {
            if (tolower((unsigned char)text[i + j]) != tolower((unsigned char)word[j]))
                break;
        }
        if (j == word_len)
            return 1;
    }

    return 0;
}

static int should_keep_critical_log(const char *msg)
{
    static const char *critical_keywords[] = {
        "requestBaseBandVersion",
        "requestGetSIMStatus",
        "requestGetProfile",
        "requestRegistrationState",
        "requestQueryDataCall",
        "requestSetupDataCall",
        "QConnectManager_Linux",
        "Find /sys/bus/usb/devices/",
        "Auto find qmichannel",
        "Auto find usbnet_adapter",
        "ip link set dev",
        "ip addr flush dev",
        "env -i PATH=",
        "busybox udhcpc",
        "failed",
        "error",
        "timeout"
    };
    size_t i;

    if (!s_critical_only)
        return 1;

    for (i = 0; i < ARRAY_SIZE(critical_keywords); i++) {
        if (contains_word_case_insensitive(msg, critical_keywords[i]))
            return 1;
    }

    return 0;
}

static const char *now_string(void)
{
    static char time_buf[64];
    struct timeval tv;
    struct tm tm_value;
    time_t seconds;
    suseconds_t millisec;

    gettimeofday(&tv, NULL);
    seconds = tv.tv_sec;
    millisec = (tv.tv_usec + 500) / 1000;
    if (millisec == 1000) {
        seconds++;
        millisec = 0;
    }

    localtime_r(&seconds, &tm_value);
    snprintf(time_buf, sizeof(time_buf), "%02d-%02d_%02d:%02d:%02d:%03d",
        tm_value.tm_mon + 1, tm_value.tm_mday, tm_value.tm_hour,
        tm_value.tm_min, tm_value.tm_sec, (int)millisec);
    return time_buf;
}

static void test_log(const char *fmt, ...)
{
    va_list args;
    char message[MAX_RESPONSE_LEN];

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (!should_keep_critical_log(message))
        return;

    fprintf(stdout, "[%s] ", now_string());
    fprintf(stdout, "%s", message);
    fprintf(stdout, "\n");
    fflush(stdout);

    if (s_log_fp) {
        fprintf(s_log_fp, "[%s] ", now_string());
        fprintf(s_log_fp, "%s", message);
        fprintf(s_log_fp, "\n");
        fflush(s_log_fp);
    }
}

static char *trim(char *value)
{
    char *end;

    while (isspace((unsigned char)*value))
        value++;

    if (*value == '\0')
        return value;

    end = value + strlen(value) - 1;
    while (end > value && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return value;
}

static int parse_bool(const char *value, int default_value)
{
    if (!value || !value[0])
        return default_value;
    if (!strcasecmp(value, "1") || !strcasecmp(value, "yes") ||
        !strcasecmp(value, "true") || !strcasecmp(value, "on"))
        return 1;
    if (!strcasecmp(value, "0") || !strcasecmp(value, "no") ||
        !strcasecmp(value, "false") || !strcasecmp(value, "off"))
        return 0;
    return default_value;
}

static int parse_positive_int(const char *value, int default_value)
{
    char *end = NULL;
    long parsed;

    if (!value || !value[0])
        return default_value;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno || end == value || *trim(end) != '\0' || parsed < 0 || parsed > 86400)
        return default_value;

    return (int)parsed;
}

static void copy_value(char *dst, size_t dst_size, const char *value)
{
    if (!dst_size)
        return;
    snprintf(dst, dst_size, "%s", value ? value : "");
}

static int mkdir_p(const char *path)
{
    char tmp[MAX_VALUE_LEN];
    char *p;

    if (!path || !path[0])
        return 0;

    copy_value(tmp, sizeof(tmp), path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) && errno != EEXIST)
        return -1;

    return 0;
}

static int ensure_parent_dir(const char *path)
{
    char dir[MAX_VALUE_LEN];
    char *slash;

    copy_value(dir, sizeof(dir), path);
    slash = strrchr(dir, '/');
    if (!slash || slash == dir)
        return 0;

    *slash = '\0';
    return mkdir_p(dir);
}

static int log_path_is_explicit(const char *path)
{
    return path[0] == '/' || strchr(path, '/') != NULL;
}

static int path_is_absolute(const char *path)
{
    return path && path[0] == '/';
}

static int path_exists(const char *path)
{
    struct stat st;

    return path && path[0] && !stat(path, &st);
}

static void dirname_of(const char *path, char *out, size_t out_size)
{
    char *slash;

    copy_value(out, out_size, path && path[0] ? path : ".");
    slash = strrchr(out, '/');
    if (!slash) {
        copy_value(out, out_size, ".");
        return;
    }
    if (slash == out) {
        slash[1] = '\0';
        return;
    }
    *slash = '\0';
}

static void basename_of(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');

    copy_value(out, out_size, slash ? slash + 1 : path);
}

static void get_config_root_dir(const char *config_file, char *out, size_t out_size)
{
    char config_dir[MAX_VALUE_LEN];
    char config_dir_base[MAX_VALUE_LEN];

    dirname_of(config_file, config_dir, sizeof(config_dir));
    basename_of(config_dir, config_dir_base, sizeof(config_dir_base));
    if (!strcmp(config_dir_base, "configs"))
        dirname_of(config_dir, out, out_size);
    else
        copy_value(out, out_size, config_dir);
}

static int resolve_relative_path(const char *base_dir, char *path, size_t path_size, int must_exist)
{
    char resolved[MAX_VALUE_LEN];

    if (!path[0] || path_is_absolute(path))
        return 0;
    if (must_exist && path_exists(path))
        return 0;

    snprintf(resolved, sizeof(resolved), "%s/%s", base_dir, path);
    if (strlen(resolved) >= path_size)
        return -1;
    if (must_exist && !path_exists(resolved))
        return 0;
    copy_value(path, path_size, resolved);
    return 0;
}

static int resolve_config_paths(CM_TEST_CONFIG *config)
{
    char base_dir[MAX_VALUE_LEN];

    get_config_root_dir(config->config_file, base_dir, sizeof(base_dir));
    return resolve_relative_path(base_dir, config->cm_path, sizeof(config->cm_path), 1)
        || resolve_relative_path(base_dir, config->log_dir, sizeof(config->log_dir), 0)
        || resolve_relative_path(base_dir, config->at_map_file, sizeof(config->at_map_file), 1)
        || resolve_relative_path(base_dir, config->udhcpc_script, sizeof(config->udhcpc_script), 1);
}

static int resolve_log_path(const char *log_dir, char *path, size_t path_size)
{
    char resolved[MAX_VALUE_LEN];

    if (!path[0])
        return 0;

    if (log_dir && log_dir[0] && !log_path_is_explicit(path)) {
        snprintf(resolved, sizeof(resolved), "%s/%s", log_dir, path);
        if (strlen(resolved) >= path_size)
            return -1;
        copy_value(path, path_size, resolved);
    }

    return ensure_parent_dir(path);
}

static void make_default_log_file(const char *config_file, char *out, size_t out_size)
{
    const char *base = strrchr(config_file, '/');
    char name[MAX_VALUE_LEN];
    char *dot;
    size_t name_len;

    base = base ? base + 1 : config_file;
    copy_value(name, sizeof(name), base[0] ? base : "quectel-cm-test");
    dot = strrchr(name, '.');
    if (dot)
        *dot = '\0';

    if (out_size <= strlen(".log"))
        return;

    name_len = strlen(name);
    if (name_len > out_size - strlen(".log") - 1)
        name_len = out_size - strlen(".log") - 1;

    memcpy(out, name, name_len);
    memcpy(out + name_len, ".log", strlen(".log") + 1);
}

static void set_default_config(CM_TEST_CONFIG *config)
{
    memset(config, 0, sizeof(*config));
    copy_value(config->cm_path, sizeof(config->cm_path), "./out/quectel-CM");
    copy_value(config->log_dir, sizeof(config->log_dir), "log");
    copy_value(config->at_map_file, sizeof(config->at_map_file), "configs/qcm-at-map.conf");
    copy_value(config->info_commands, sizeof(config->info_commands),
        "AT+QNWINFO|AT+QCSQ|AT+COPS?|AT+CGREG?|AT+CEREG?|AT+C5GREG?");
    copy_value(config->ping_address, sizeof(config->ping_address), "8.8.8.8");
    config->enable_ipv4 = 1;
    config->connect_duration_sec = 60;
    config->connection_times = 1;
    config->sleep_between_sec = 0;
    config->connect_wait_sec = 30;
    config->status_interval_sec = 30;
    config->ping_count = 1;
    config->ping_timeout_sec = 5;
    config->at_timeout_sec = 5;
    config->critical_log_only = 0;
}

static int apply_config_key(CM_TEST_CONFIG *config, const char *key, const char *value)
{
    if (!strcasecmp(key, "cm_path"))
        copy_value(config->cm_path, sizeof(config->cm_path), value);
    else if (!strcasecmp(key, "log_dir"))
        copy_value(config->log_dir, sizeof(config->log_dir), value);
    else if (!strcasecmp(key, "log_file"))
        copy_value(config->log_file, sizeof(config->log_file), value);
    else if (!strcasecmp(key, "cm_log_file"))
        copy_value(config->cm_log_file, sizeof(config->cm_log_file), value);
    else if (!strcasecmp(key, "at_device"))
        copy_value(config->at_device, sizeof(config->at_device), value);
    else if (!strcasecmp(key, "at_map_file"))
        copy_value(config->at_map_file, sizeof(config->at_map_file), value);
    else if (!strcasecmp(key, "rat"))
        copy_value(config->rat, sizeof(config->rat), value);
    else if (!strcasecmp(key, "band"))
        copy_value(config->band, sizeof(config->band), value);
    else if (!strcasecmp(key, "info_commands"))
        copy_value(config->info_commands, sizeof(config->info_commands), value);
    else if (!strcasecmp(key, "apn"))
        copy_value(config->apn, sizeof(config->apn), value);
    else if (!strcasecmp(key, "user"))
        copy_value(config->user, sizeof(config->user), value);
    else if (!strcasecmp(key, "password"))
        copy_value(config->password, sizeof(config->password), value);
    else if (!strcasecmp(key, "auth"))
        copy_value(config->auth, sizeof(config->auth), value);
    else if (!strcasecmp(key, "pin"))
        copy_value(config->pin, sizeof(config->pin), value);
    else if (!strcasecmp(key, "proxy"))
        copy_value(config->proxy, sizeof(config->proxy), value);
    else if (!strcasecmp(key, "interface"))
        copy_value(config->interface, sizeof(config->interface), value);
    else if (!strcasecmp(key, "pdp"))
        copy_value(config->pdp, sizeof(config->pdp), value);
    else if (!strcasecmp(key, "qmap_iface_idx"))
        copy_value(config->qmap_iface_idx, sizeof(config->qmap_iface_idx), value);
    else if (!strcasecmp(key, "udhcpc_script"))
        copy_value(config->udhcpc_script, sizeof(config->udhcpc_script), value);
    else if (!strcasecmp(key, "ping_address"))
        copy_value(config->ping_address, sizeof(config->ping_address), value);
    else if (!strcasecmp(key, "ipv4"))
        config->enable_ipv4 = parse_bool(value, config->enable_ipv4);
    else if (!strcasecmp(key, "ipv6"))
        config->enable_ipv6 = parse_bool(value, config->enable_ipv6);
    else if (!strcasecmp(key, "no_dhcp"))
        config->no_dhcp = parse_bool(value, config->no_dhcp);
    else if (!strcasecmp(key, "bridge"))
        config->bridge = parse_bool(value, config->bridge);
    else if (!strcasecmp(key, "verbose"))
        config->verbose = parse_bool(value, config->verbose);
    else if (!strcasecmp(key, "connect_duration_sec"))
        config->connect_duration_sec = parse_positive_int(value, config->connect_duration_sec);
    else if (!strcasecmp(key, "connection_times"))
        config->connection_times = parse_positive_int(value, config->connection_times);
    else if (!strcasecmp(key, "sleep_between_sec"))
        config->sleep_between_sec = parse_positive_int(value, config->sleep_between_sec);
    else if (!strcasecmp(key, "connect_wait_sec"))
        config->connect_wait_sec = parse_positive_int(value, config->connect_wait_sec);
    else if (!strcasecmp(key, "status_interval_sec"))
        config->status_interval_sec = parse_positive_int(value, config->status_interval_sec);
    else if (!strcasecmp(key, "ping_enable"))
        config->ping_enable = parse_bool(value, config->ping_enable);
    else if (!strcasecmp(key, "ping_count"))
        config->ping_count = parse_positive_int(value, config->ping_count);
    else if (!strcasecmp(key, "ping_timeout_sec"))
        config->ping_timeout_sec = parse_positive_int(value, config->ping_timeout_sec);
    else if (!strcasecmp(key, "at_timeout_sec"))
        config->at_timeout_sec = parse_positive_int(value, config->at_timeout_sec);
    else if (!strcasecmp(key, "critical_log_only") || !strcasecmp(key, "less_log") ||
        !strcasecmp(key, "quiet"))
        config->critical_log_only = parse_bool(value, config->critical_log_only);
    else {
        test_log("unknown config key '%s'", key);
        return -1;
    }

    return 0;
}

static int read_config_file(const char *path, CM_TEST_CONFIG *config)
{
    FILE *config_fp;
    char line[1024];
    unsigned int line_no = 0;
    int errors = 0;

    config_fp = fopen(path, "r");
    if (!config_fp) {
        test_log("failed to open config %s: %s", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), config_fp)) {
        char *key;
        char *value;
        char *separator;

        line_no++;
        key = trim(line);
        if (!key[0] || key[0] == '#' || key[0] == ';')
            continue;

        separator = strchr(key, '=');
        if (!separator) {
            test_log("invalid config line %u: missing '='", line_no);
            errors++;
            continue;
        }

        *separator = '\0';
        value = trim(separator + 1);
        key = trim(key);
        if (!key[0]) {
            test_log("invalid config line %u: empty key", line_no);
            errors++;
            continue;
        }

        if (apply_config_key(config, key, value))
            errors++;
    }

    fclose(config_fp);

    if (!config->enable_ipv4 && !config->enable_ipv6)
        config->enable_ipv4 = 1;
    if (config->connection_times <= 0)
        config->connection_times = 1;
    if (config->status_interval_sec <= 0)
        config->status_interval_sec = 30;
    if (!config->cm_log_file[0] && config->log_file[0])
        copy_value(config->cm_log_file, sizeof(config->cm_log_file), config->log_file);

    return errors ? -1 : 0;
}

static void signal_handler(int signo)
{
    (void)signo;
    s_stop_requested = 1;
}

static int open_at_device(const char *device)
{
    int fd;

    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        test_log("failed to open AT device %s: %s", device, strerror(errno));
        return -1;
    }

    if (!strncmp(device, "/dev/tty", strlen("/dev/tty"))) {
        struct termios termios_value;

        memset(&termios_value, 0, sizeof(termios_value));
        if (!tcgetattr(fd, &termios_value)) {
            cfmakeraw(&termios_value);
            cfsetispeed(&termios_value, B115200);
            cfsetospeed(&termios_value, B115200);
            tcsetattr(fd, TCSANOW, &termios_value);
            tcflush(fd, TCIOFLUSH);
        }
    }

    return fd;
}

static int response_is_complete(const char *response)
{
    return strstr(response, "\r\nOK\r\n") || strstr(response, "\nOK\n") ||
        strstr(response, "\r\nERROR\r\n") || strstr(response, "\nERROR\n") ||
        strstr(response, "+CME ERROR:") || strstr(response, "+CMS ERROR:");
}

static void log_at_response(const char *label, const char *command, const char *response)
{
    char response_copy[MAX_RESPONSE_LEN];
    char *line;
    char *saveptr = NULL;

    copy_value(response_copy, sizeof(response_copy), response);
    test_log("%s command: %s", label, command);
    line = strtok_r(response_copy, "\r\n", &saveptr);
    while (line) {
        char *trimmed = trim(line);
        if (trimmed[0])
            test_log("%s response: %s", label, trimmed);
        line = strtok_r(NULL, "\r\n", &saveptr);
    }
}

static int send_at_command(int fd, const char *label, const char *command, int timeout_sec)
{
    char request[MAX_COMMAND_LEN];
    char response[MAX_RESPONSE_LEN];
    size_t response_len = 0;
    long long deadline_ms;
    struct timeval tv;

    snprintf(request, sizeof(request), "%s\r", command);
    response[0] = '\0';
    tcflush(fd, TCIFLUSH);
    if (write(fd, request, strlen(request)) < 0) {
        test_log("%s command write failed for %s: %s", label, command, strerror(errno));
        return -1;
    }

    gettimeofday(&tv, NULL);
    deadline_ms = ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000) + ((long long)timeout_sec * 1000);

    while (!s_stop_requested) {
        long long now_ms;
        int timeout_ms;
        struct pollfd pollfd_value;

        gettimeofday(&tv, NULL);
        now_ms = ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
        if (now_ms >= deadline_ms)
            break;
        timeout_ms = (int)(deadline_ms - now_ms);
        if (timeout_ms > 500)
            timeout_ms = 500;

        memset(&pollfd_value, 0, sizeof(pollfd_value));
        pollfd_value.fd = fd;
        pollfd_value.events = POLLIN;

        if (poll(&pollfd_value, 1, timeout_ms) < 0) {
            if (errno == EINTR)
                continue;
            test_log("%s poll failed for %s: %s", label, command, strerror(errno));
            return -1;
        }

        if (pollfd_value.revents & POLLIN) {
            ssize_t read_len = read(fd, response + response_len, sizeof(response) - response_len - 1);
            if (read_len > 0) {
                response_len += (size_t)read_len;
                response[response_len] = '\0';
                if (response_is_complete(response)) {
                    log_at_response(label, command, response);
                    return strstr(response, "ERROR") ? -1 : 0;
                }
                if (response_len >= sizeof(response) - 1)
                    break;
            }
        }
    }

    log_at_response(label, command, response_len ? response : "<timeout/no response>");
    return -1;
}

static int run_at_command_list(const char *device, const char *label, const char *commands, int timeout_sec)
{
    char commands_copy[MAX_COMMAND_LEN];
    char *command;
    char *saveptr = NULL;
    int fd;
    int ret = 0;

    if (!device[0] || !commands[0])
        return 0;

    fd = open_at_device(device);
    if (fd < 0)
        return -1;

    copy_value(commands_copy, sizeof(commands_copy), commands);
    command = strtok_r(commands_copy, "|", &saveptr);
    while (command && !s_stop_requested) {
        char *trimmed = trim(command);
        if (trimmed[0] && send_at_command(fd, label, trimmed, timeout_sec))
            ret = -1;
        command = strtok_r(NULL, "|", &saveptr);
    }

    close(fd);
    return ret;
}

static int lookup_map_command(const char *map_file, const char *key, char *out_value, size_t out_size)
{
    FILE *map_fp;
    char line[1024];

    out_value[0] = '\0';
    if (!map_file[0])
        return -1;

    map_fp = fopen(map_file, "r");
    if (!map_fp) {
        test_log("failed to open AT map %s: %s", map_file, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), map_fp)) {
        char *map_key;
        char *map_value;
        char *separator;

        map_key = trim(line);
        if (!map_key[0] || map_key[0] == '#' || map_key[0] == ';')
            continue;

        separator = strchr(map_key, '=');
        if (!separator)
            continue;

        *separator = '\0';
        map_value = trim(separator + 1);
        map_key = trim(map_key);
        if (!strcasecmp(map_key, key)) {
            copy_value(out_value, out_size, map_value);
            fclose(map_fp);
            return 0;
        }
    }

    fclose(map_fp);
    return -1;
}

static int apply_mapped_setting(const CM_TEST_CONFIG *config, const char *prefix, const char *name)
{
    char key[MAX_VALUE_LEN];
    char commands[MAX_COMMAND_LEN];

    if (!name[0])
        return 0;
    if (!config->at_device[0]) {
        test_log("skip %s=%s because at_device is not configured", prefix, name);
        return 0;
    }

    snprintf(key, sizeof(key), "%s.%s", prefix, name);
    if (lookup_map_command(config->at_map_file, key, commands, sizeof(commands))) {
        test_log("no AT map entry for %s in %s", key, config->at_map_file);
        return -1;
    }

    test_log("apply %s=%s using map key %s", prefix, name, key);
    return run_at_command_list(config->at_device, key, commands, config->at_timeout_sec);
}

static void add_arg(char **argv, int *argc_value, const char *value)
{
    if (*argc_value < MAX_ARGS - 1)
        argv[(*argc_value)++] = (char *)value;
}

static pid_t start_quectel_cm(const CM_TEST_CONFIG *config)
{
    char *argv[MAX_ARGS];
    int argc_value = 0;
    pid_t child_pid;

    memset(argv, 0, sizeof(argv));
    add_arg(argv, &argc_value, config->cm_path);

    if (config->apn[0]) {
        add_arg(argv, &argc_value, "-s");
        add_arg(argv, &argc_value, config->apn);
        if (config->user[0])
            add_arg(argv, &argc_value, config->user);
        if (config->password[0])
            add_arg(argv, &argc_value, config->password);
        if (config->auth[0])
            add_arg(argv, &argc_value, config->auth);
    }
    if (config->pin[0]) {
        add_arg(argv, &argc_value, "-p");
        add_arg(argv, &argc_value, config->pin);
    }
    if (config->proxy[0]) {
        add_arg(argv, &argc_value, "-p");
        add_arg(argv, &argc_value, config->proxy);
    }
    if (config->cm_log_file[0]) {
        add_arg(argv, &argc_value, "-f");
        add_arg(argv, &argc_value, config->cm_log_file);
    }
    if (config->interface[0]) {
        add_arg(argv, &argc_value, "-i");
        add_arg(argv, &argc_value, config->interface);
    }
    if (config->pdp[0]) {
        add_arg(argv, &argc_value, "-n");
        add_arg(argv, &argc_value, config->pdp);
    }
    if (config->qmap_iface_idx[0]) {
        add_arg(argv, &argc_value, "-m");
        add_arg(argv, &argc_value, config->qmap_iface_idx);
    }
    if (config->udhcpc_script[0]) {
        add_arg(argv, &argc_value, "-S");
        add_arg(argv, &argc_value, config->udhcpc_script);
    }
    if (config->enable_ipv4)
        add_arg(argv, &argc_value, "-4");
    if (config->enable_ipv6)
        add_arg(argv, &argc_value, "-6");
    if (config->no_dhcp)
        add_arg(argv, &argc_value, "-d");
    if (config->bridge)
        add_arg(argv, &argc_value, "-b");
    if (config->verbose)
        add_arg(argv, &argc_value, "-v");

    argv[argc_value] = NULL;
    child_pid = fork();
    if (child_pid < 0) {
        test_log("fork failed: %s", strerror(errno));
        return -1;
    }

    if (child_pid == 0) {
        execv(config->cm_path, argv);
        fprintf(stderr, "execv(%s) failed: %s\n", config->cm_path, strerror(errno));
        _exit(127);
    }

    test_log("started quectel-CM pid=%d", (int)child_pid);
    return child_pid;
}

static int wait_child_exit(pid_t child_pid, int timeout_sec)
{
    int status = 0;
    int elapsed_sec;

    for (elapsed_sec = 0; elapsed_sec < timeout_sec; elapsed_sec++) {
        pid_t wait_ret = waitpid(child_pid, &status, WNOHANG);
        if (wait_ret == child_pid) {
            if (WIFEXITED(status))
                test_log("quectel-CM exited with status %d", WEXITSTATUS(status));
            else if (WIFSIGNALED(status))
                test_log("quectel-CM exited by signal %d", WTERMSIG(status));
            return status;
        }
        if (wait_ret < 0 && errno != EINTR) {
            test_log("waitpid failed: %s", strerror(errno));
            return -1;
        }
        sleep(1);
    }

    return -2;
}

static void stop_quectel_cm(pid_t child_pid)
{
    if (child_pid <= 0)
        return;

    test_log("stopping quectel-CM pid=%d", (int)child_pid);
    kill(child_pid, SIGINT);
    if (wait_child_exit(child_pid, 30) == -2) {
        test_log("quectel-CM did not stop after SIGINT, sending SIGTERM");
        kill(child_pid, SIGTERM);
        if (wait_child_exit(child_pid, 10) == -2) {
            test_log("quectel-CM did not stop after SIGTERM, sending SIGKILL");
            kill(child_pid, SIGKILL);
            wait_child_exit(child_pid, 5);
        }
    }
}

static int child_is_running(pid_t child_pid)
{
    int status = 0;
    pid_t wait_ret;

    wait_ret = waitpid(child_pid, &status, WNOHANG);
    if (wait_ret == 0)
        return 1;
    if (wait_ret == child_pid) {
        if (WIFEXITED(status))
            test_log("quectel-CM exited early with status %d", WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            test_log("quectel-CM exited early by signal %d", WTERMSIG(status));
        return 0;
    }
    if (errno != EINTR)
        test_log("waitpid status check failed: %s", strerror(errno));
    return 0;
}

static void log_file_first_line(const char *path, const char *label)
{
    FILE *file_fp;
    char line[256];

    file_fp = fopen(path, "r");
    if (!file_fp)
        return;
    if (fgets(line, sizeof(line), file_fp)) {
        char *trimmed = trim(line);
        if (trimmed[0])
            test_log("%s: %s", label, trimmed);
    }
    fclose(file_fp);
}

static void log_interface_status(const CM_TEST_CONFIG *config)
{
    char path[MAX_VALUE_LEN + 64];
    char command[MAX_COMMAND_LEN];
    FILE *command_fp;
    char line[512];
    char operstate[MAX_VALUE_LEN] = {0};
    int command_status;

    if (!config->interface[0]) {
        test_log("interface check skipped: interface is not configured");
        return;
    }

    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", config->interface);
    {
        FILE *state_fp = fopen(path, "r");
        if (state_fp) {
            if (fgets(operstate, sizeof(operstate), state_fp)) {
                char *trimmed = trim(operstate);
                if (trimmed[0]) {
                    if (!s_last_if_state[0] || strcmp(s_last_if_state, trimmed)) {
                        test_log("interface state: %s %s", config->interface, trimmed);
                        copy_value(s_last_if_state, sizeof(s_last_if_state), trimmed);
                    }
                }
            }
            fclose(state_fp);
        }
    }

    if (s_critical_only)
        return;

    log_file_first_line(path, "interface operstate");

    snprintf(command, sizeof(command), "ip -brief address show dev '%s' 2>&1", config->interface);
    command_fp = popen(command, "r");
    if (!command_fp) {
        test_log("failed to run interface address check: %s", strerror(errno));
        return;
    }
    while (fgets(line, sizeof(line), command_fp)) {
        char *trimmed = trim(line);
        if (trimmed[0])
            test_log("interface address: %s", trimmed);
    }
    command_status = pclose(command_fp);
    test_log("interface address command status=%d", command_status);
}

static void log_dns_servers(void)
{
    FILE *resolv_fp;
    char line[512];
    char dns_servers[MAX_COMMAND_LEN] = {0};
    size_t used = 0;

    resolv_fp = fopen("/etc/resolv.conf", "r");
    if (!resolv_fp)
        return;

    while (fgets(line, sizeof(line), resolv_fp)) {
        char *entry = trim(line);
        if (!strncmp(entry, "nameserver", strlen("nameserver"))) {
            char *value = entry + strlen("nameserver");

            value = trim(value);
            if (value[0]) {
                int appended = snprintf(dns_servers + used, sizeof(dns_servers) - used,
                    "%s%s", used ? "," : "", value);
                if (appended < 0 || (size_t)appended >= sizeof(dns_servers) - used) {
                    used = sizeof(dns_servers) - 1;
                    break;
                }
                used += (size_t)appended;
            }
        }
    }

    fclose(resolv_fp);
    if (dns_servers[0] && strcmp(dns_servers, s_last_dns_servers)) {
        copy_value(s_last_dns_servers, sizeof(s_last_dns_servers), dns_servers);
        test_log("dns servers: %s", s_last_dns_servers);
    }
}

static int run_ping_check(const CM_TEST_CONFIG *config)
{
    char command[MAX_COMMAND_LEN];
    FILE *command_fp;
    char line[512];
    int command_status;

    if (!config->ping_enable)
        return 0;

    snprintf(command, sizeof(command), "ping -c %d -W %d '%s' 2>&1",
        config->ping_count, config->ping_timeout_sec, config->ping_address);
    test_log("ping command: %s", command);
    command_fp = popen(command, "r");
    if (!command_fp) {
        test_log("failed to run ping: %s", strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), command_fp)) {
        char *trimmed = trim(line);
        if (trimmed[0])
            test_log("ping response: %s", trimmed);
    }

    command_status = pclose(command_fp);
    test_log("ping status=%d (log-only)", command_status);
    return command_status;
}

static void log_status_snapshot(const CM_TEST_CONFIG *config, const char *phase)
{
    if (s_critical_only)
        return;

    test_log("status snapshot: %s", phase);
    if (config->at_device[0])
        run_at_command_list(config->at_device, "modem-info", config->info_commands, config->at_timeout_sec);
    else
        test_log("modem AT info skipped: at_device is not configured");
    log_interface_status(config);
    log_dns_servers();
    run_ping_check(config);
}

static void sleep_with_stop(int seconds)
{
    int elapsed_sec;

    for (elapsed_sec = 0; elapsed_sec < seconds && !s_stop_requested; elapsed_sec++)
        sleep(1);
}

static int run_cycle(const CM_TEST_CONFIG *config, int cycle_no)
{
    pid_t child_pid;
    time_t end_time;
    time_t next_status_time;
    int keep_connected = (config->connect_duration_sec == 0);
    int child_running = 1;
    int ret = 0;

    test_log("cycle %d start", cycle_no);
    apply_mapped_setting(config, "rat", config->rat);
    apply_mapped_setting(config, "band", config->band);

    child_pid = start_quectel_cm(config);
    if (child_pid <= 0)
        return -1;

    sleep_with_stop(config->connect_wait_sec);
    if (!child_is_running(child_pid))
        return -1;

    log_status_snapshot(config, "after connect wait");
    end_time = keep_connected ? 0 : time(NULL) + config->connect_duration_sec;
    next_status_time = time(NULL) + config->status_interval_sec;

    if (keep_connected)
        test_log("cycle %d keep connected until SIGINT or SIGTERM", cycle_no);

    while (!s_stop_requested && (keep_connected || time(NULL) < end_time)) {
        if (!child_is_running(child_pid)) {
            child_running = 0;
            ret = -1;
            break;
        }
        if (time(NULL) >= next_status_time) {
            log_status_snapshot(config, "during connection");
            next_status_time = time(NULL) + config->status_interval_sec;
        }
        sleep_with_stop(1);
    }

    log_status_snapshot(config, "before disconnect");
    if (child_running)
        stop_quectel_cm(child_pid);
    log_status_snapshot(config, "after disconnect");
    test_log("cycle %d end result=%s", cycle_no, ret ? "failed" : "completed");
    return ret;
}

static void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s -c config_file [-f log_file] [-v] [-q]\n", progname);
}

int main(int argc, char **argv)
{
    CM_TEST_CONFIG config;
    char cli_log_file[MAX_VALUE_LEN] = {0};
    int cli_verbose = 0;
    int option;
    int cycle_no;
    int ret = 0;

    set_default_config(&config);

    while ((option = getopt(argc, argv, "c:f:vqh")) != -1) {
        switch (option) {
        case 'c':
            copy_value(config.config_file, sizeof(config.config_file), optarg);
            break;
        case 'f':
            copy_value(cli_log_file, sizeof(cli_log_file), optarg);
            break;
        case 'v':
            cli_verbose = 1;
            break;
        case 'q':
            config.critical_log_only = 1;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return option == 'h' ? 0 : 1;
        }
    }

    if (!config.config_file[0]) {
        usage(argv[0]);
        return 1;
    }

    if (read_config_file(config.config_file, &config))
        return 1;

    if (cli_log_file[0])
        copy_value(config.log_file, sizeof(config.log_file), cli_log_file);
    if (cli_verbose)
        config.verbose = 1;
    s_critical_only = config.critical_log_only;
    if (resolve_config_paths(&config)) {
        fprintf(stderr, "failed to resolve paths relative to config %s\n", config.config_file);
        return 1;
    }
    if (!config.log_file[0])
        make_default_log_file(config.config_file, config.log_file, sizeof(config.log_file));
    if (!config.cm_log_file[0] && config.log_file[0])
        copy_value(config.cm_log_file, sizeof(config.cm_log_file), config.log_file);
    if (resolve_log_path(config.log_dir, config.log_file, sizeof(config.log_file))
        || resolve_log_path(config.log_dir, config.cm_log_file, sizeof(config.cm_log_file))) {
        fprintf(stderr, "failed to prepare log path under %s: %s\n", config.log_dir, strerror(errno));
        return 1;
    }

    if (config.log_file[0]) {
        s_log_fp = fopen(config.log_file, "a+");
        if (!s_log_fp) {
            fprintf(stderr, "failed to open log file %s: %s\n", config.log_file, strerror(errno));
            return 1;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    test_log("quectel-cm-test start config=%s", config.config_file);
    test_log("cm_path=%s connection_times=%d connect_duration_sec=%d sleep_between_sec=%d ping_enable=%d ping_address=%s",
        config.cm_path, config.connection_times, config.connect_duration_sec,
        config.sleep_between_sec, config.ping_enable, config.ping_address);

    for (cycle_no = 1; cycle_no <= config.connection_times && !s_stop_requested; cycle_no++) {
        if (run_cycle(&config, cycle_no))
            ret = 1;
        if (cycle_no < config.connection_times && !s_stop_requested) {
            test_log("sleep between cycles: %d seconds", config.sleep_between_sec);
            sleep_with_stop(config.sleep_between_sec);
        }
    }

    test_log("quectel-cm-test exit result=%s", ret ? "failed" : "completed");
    if (s_log_fp)
        fclose(s_log_fp);
    return ret;
}