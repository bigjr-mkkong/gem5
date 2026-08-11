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
    uint64_t payload_word0;
    bool is_write;
    bool is_pim_query;
}PEsim_rs_MemReq;

typedef struct PESim_payload{
    uint64_t dword_payload[8];
    uint32_t payload_sz_bytes;
}PESim_payload;

typedef struct PESim_config {
    const char *config_file;
    const char *output_dir;
    uint32_t controller_id;
    uint64_t controller_base;
    uint64_t controller_size;
    uint64_t pim_size;
} PESim_config;

typedef struct PESim_body PESim_body;

PESim_body *pesim_new(const PESim_config *config);
void pesim_free(PESim_body *sim);

void pesim_print_stats(PESim_body *sim);
void pesim_reset_stats(PESim_body *sim);

bool pesim_canAccept(PESim_body *sim, uint64_t addr, bool is_write);
bool pesim_enqueue_with_data(PESim_body *sim, uint64_t addr, PESim_payload payload, bool is_write);
bool pesim_can_accept_pim_cmd(PESim_body *sim, uint64_t offset, PESim_payload payload, bool is_write);
bool pesim_enqueue_pim_cmd(PESim_body *sim, uint64_t offset, PESim_payload payload, bool is_write);

double pesim_clock_period(PESim_body *sim);
unsigned int pesim_queue_size(PESim_body *sim);
unsigned int pesim_burst_size(PESim_body *sim);

bool pesim_has_complete(PESim_body *sim);
PEsim_rs_MemReq pesim_get_complete(PESim_body *sim);

void pesim_tick(PESim_body *sim);


#ifdef __cplusplus
}
#endif
