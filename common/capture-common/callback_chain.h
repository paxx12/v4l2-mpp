#ifndef CALLBACK_CHAIN_H
#define CALLBACK_CHAIN_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void (*cb)(const void *data, size_t size, void *arg);
    void *arg;
    bool run;
} callback_chain_t;

static inline bool callback_chain_active(callback_chain_t *chain)
{
    while (chain->cb) {
        if (chain->run) {
            return true;
        }
        chain++;
    }
    return false;
}

static inline void callback_chain_write_cb(const void *data, size_t size, void *arg)
{
    callback_chain_t *chain = arg;

    while (chain->cb) {
        if (chain->run) {
            chain->cb(data, size, chain->arg);
        }
        chain++;
    }
}

#endif
