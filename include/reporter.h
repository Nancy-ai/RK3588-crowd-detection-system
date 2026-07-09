#ifndef REPORTER_H
#define REPORTER_H

#include "post_processor.h"
#include "sys_monitor.h"
#include "event_analysis.h"

#ifdef __cplusplus
extern "C" {
#endif

int reporter_init(const char* target_ip, int port);

int reporter_send(const DetectResultGroup* tracks,
                  const TrackRuntimeInfoGroup* runtime_info,
                  const EventResultGroup* events,
                  const SystemStats* stats);

void reporter_close(void);

#ifdef __cplusplus
}
#endif

#endif