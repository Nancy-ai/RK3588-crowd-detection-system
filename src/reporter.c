#define _POSIX_C_SOURCE 200809L

#include "reporter.h"
#include "event_analysis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define REPORT_JSON_SIZE 32768
#define REPORT_HTTP_SIZE 40960

static char g_target_ip[64] = "127.0.0.1";
static int g_target_port = 8000;
static int g_reporter_ready = 0;

static long long get_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int append_json(char* buf, int cap, int* offset, const char* fmt, ...)
{
    if (!buf || !offset || *offset >= cap) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + *offset, cap - *offset, fmt, args);
    va_end(args);

    if (n < 0 || *offset + n >= cap) {
        return -1;
    }

    *offset += n;
    return 0;
}

static void json_escape(const char* src, char* dst, int dst_size)
{
    if (!dst || dst_size <= 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    int j = 0;
    for (int i = 0; src[i] != '\0' && j < dst_size - 1; i++) {
        char c = src[i];

        if (c == '"' || c == '\\') {
            if (j + 2 >= dst_size) {
                break;
            }
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            dst[j++] = ' ';
        } else {
            dst[j++] = c;
        }
    }

    dst[j] = '\0';
}

static const TrackRuntimeInfo* find_runtime_info(const TrackRuntimeInfoGroup* runtime_info,
                                                 int track_id)
{
    if (!runtime_info) {
        return NULL;
    }

    for (int i = 0; i < runtime_info->count; i++) {
        if (runtime_info->infos[i].track_id == track_id) {
            return &runtime_info->infos[i];
        }
    }

    return NULL;
}

static int connect_with_timeout(int sockfd, struct sockaddr_in* addr, int timeout_ms)
{
    int old_flags = fcntl(sockfd, F_GETFL, 0);
    if (old_flags < 0) {
        return -1;
    }

    if (fcntl(sockfd, F_SETFL, old_flags | O_NONBLOCK) < 0) {
        return -1;
    }

    int ret = connect(sockfd, (struct sockaddr*)addr, sizeof(*addr));
    if (ret == 0) {
        fcntl(sockfd, F_SETFL, old_flags);
        return 0;
    }

    if (errno != EINPROGRESS) {
        fcntl(sockfd, F_SETFL, old_flags);
        return -1;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sockfd, &wfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(sockfd + 1, NULL, &wfds, NULL, &tv);
    if (ret <= 0) {
        fcntl(sockfd, F_SETFL, old_flags);
        return -1;
    }

    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        fcntl(sockfd, F_SETFL, old_flags);
        return -1;
    }

    if (error != 0) {
        errno = error;
        fcntl(sockfd, F_SETFL, old_flags);
        return -1;
    }

    fcntl(sockfd, F_SETFL, old_flags);
    return 0;
}

static int send_all(int sockfd, const char* data, int len)
{
    int sent = 0;

    while (sent < len) {
        int n = send(sockfd, data + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }

        sent += n;
    }

    return 0;
}

int reporter_init(const char* target_ip, int port)
{
    if (!target_ip || port <= 0) {
        return -1;
    }

    snprintf(g_target_ip, sizeof(g_target_ip), "%s", target_ip);
    g_target_port = port;
    g_reporter_ready = 1;

    printf("[Reporter] HTTP target: http://%s:%d/api/report\n",
           g_target_ip, g_target_port);

    return 0;
}

int reporter_send(const DetectResultGroup* tracks,
                  const TrackRuntimeInfoGroup* runtime_info,
                  const EventResultGroup* events,
                  const SystemStats* stats)
{
    if (!g_reporter_ready || !tracks || !runtime_info || !events || !stats) {
        return -1;
    }

    char json[REPORT_JSON_SIZE];
    char http[REPORT_HTTP_SIZE];
    char npu_text[256];

    int offset = 0;
    long long device_send_ms = get_now_ms();

    int roi_count = 0;
    int long_stay_count = 0;

    for (int i = 0; i < runtime_info->count; i++) {
        if (runtime_info->infos[i].in_roi) {
            roi_count++;
        }

        if (runtime_info->infos[i].alarmed) {
            long_stay_count++;
        }
    }

    if (events->count > long_stay_count) {
        long_stay_count = events->count;
    }

    json_escape(stats->npu_load, npu_text, sizeof(npu_text));

    if (append_json(json, sizeof(json), &offset, "{") < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"device_id\":\"rk3588_elf2\",") < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"device_send_ms\":%lld,", device_send_ms) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"fps\":%.2f,", stats->fps) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"temperature\":%.2f,", stats->temp) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"cpu_usage\":%.2f,", stats->cpu_usage) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"gpu_usage\":%d,", stats->gpu_load) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"npu_load\":\"%s\",", npu_text) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"person_count\":%d,", tracks->count) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"track_count\":%d,", tracks->count) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"roi_count\":%d,", roi_count) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"long_stay_count\":%d,", long_stay_count) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"event_count\":%d,", events->count) < 0) return -1;

    if (append_json(json, sizeof(json), &offset,
                    "\"events\":[") < 0) return -1;

    for (int i = 0; i < events->count && i < EVENT_MAX_RESULTS; i++) {
        const EventResult* e = &events->results[i];

        if (i > 0) {
            if (append_json(json, sizeof(json), &offset, ",") < 0) return -1;
        }

        char roi_name[64];
        json_escape(e->roi_name, roi_name, sizeof(roi_name));

        const char* event_type = "UNKNOWN";
        if (e->type == EVENT_TYPE_LONG_STAY) {
            event_type = "LONG_STAY";
        }

        if (append_json(json, sizeof(json), &offset,
                        "{"
                        "\"type\":\"%s\","
                        "\"track_id\":%d,"
                        "\"duration\":%.2f,"
                        "\"roi\":\"%s\","
                        "\"box\":{\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d}"
                        "}",
                        event_type,
                        e->track_id,
                        e->duration,
                        roi_name,
                        e->x1,
                        e->y1,
                        e->x2,
                        e->y2) < 0) {
            return -1;
        }
    }

    if (append_json(json, sizeof(json), &offset,
                    "],\"tracks\":[") < 0) return -1;

    for (int i = 0; i < tracks->count && i < 128; i++) {
        const DetectResult* r = &tracks->results[i];

        if (i > 0) {
            if (append_json(json, sizeof(json), &offset, ",") < 0) return -1;
        }

        const TrackRuntimeInfo* info = find_runtime_info(runtime_info, r->track_id);

        int in_roi = info ? info->in_roi : 0;
        int alarmed = info ? info->alarmed : 0;
        float dwell_time = info ? info->dwell_time : 0.0f;

        if (append_json(json, sizeof(json), &offset,
                        "{"
                        "\"track_id\":%d,"
                        "\"class_id\":%d,"
                        "\"score\":%.4f,"
                        "\"in_roi\":%s,"
                        "\"alarmed\":%s,"
                        "\"dwell_time\":%.2f,"
                        "\"box\":{\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d}"
                        "}",
                        r->track_id,
                        r->id,
                        r->prob,
                        in_roi ? "true" : "false",
                        alarmed ? "true" : "false",
                        dwell_time,
                        r->x1,
                        r->y1,
                        r->x2,
                        r->y2) < 0) {
            return -1;
        }
    }

    if (append_json(json, sizeof(json), &offset, "]}") < 0) {
        return -1;
    }

    int json_len = offset;

    int http_len = snprintf(
        http,
        sizeof(http),
        "POST /api/report HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        g_target_ip,
        g_target_port,
        json_len,
        json
    );

    if (http_len <= 0 || http_len >= (int)sizeof(http)) {
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_target_port);

    if (inet_pton(AF_INET, g_target_ip, &server_addr.sin_addr) <= 0) {
        close(sockfd);
        return -1;
    }

    if (connect_with_timeout(sockfd, &server_addr, 300) < 0) {
        close(sockfd);
        return -1;
    }

    if (send_all(sockfd, http, http_len) < 0) {
        close(sockfd);
        return -1;
    }

    char response[128];
    recv(sockfd, response, sizeof(response) - 1, 0);

    close(sockfd);
    return 0;
}

void reporter_close(void)
{
    g_reporter_ready = 0;
}