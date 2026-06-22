#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef struct PEsim_rs_MemReq
{
    uint64_t addr;
    uint64_t issue_time;
    bool is_write;
}PEsim_rs_MemReq;

typedef struct PESim_cacheline{
    uint64_t dword_payload[8];
}PESim_cacheline;

typedef struct PESim_body PESim_body;

PESim_body *pesim_new(void);
void pesim_free(PESim_body *sim);

void pesim_print_stats(PESim_body *sim);
void pesim_reset_stats(PESim_body *sim);

bool pesim_canAccept(PESim_body *sim, uint64_t addr, bool is_write);
bool pesim_enqueue_with_data(PESim_body *sim, uint64_t addr, PESim_cacheline payload, bool is_write);

double pesim_clock_period(PESim_body *sim);
unsigned int pesim_queue_size(PESim_body *sim);
unsigned int pesim_burst_size(PESim_body *sim);

bool pesim_has_complete(PESim_body *sim);
PEsim_rs_MemReq pesim_get_complete(PESim_body *sim);

void pesim_tick(PESim_body *sim);


#ifdef __cplusplus
}
#endif
