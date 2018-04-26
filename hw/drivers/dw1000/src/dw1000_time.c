/**
 * Copyright 2018, Decawave Limited, All Rights Reserved
 * 
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <os/os.h>
#include <hal/hal_spi.h>
#include <hal/hal_gpio.h>
#include "bsp/bsp.h"


#include <dw1000/dw1000_regs.h>
#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_hal.h>
#include <dw1000/dw1000_mac.h>
#include <dw1000/dw1000_phy.h>
#include <dw1000/dw1000_ftypes.h>
#if MYNEWT_VAL(DW1000_TIME)
#include <dw1000/dw1000_time.h>

static void time_postprocess(struct os_event * ev);
static struct os_callout time_callout_postprocess;

void time_rx_ccp_complete_cb(dw1000_dev_instance_t * inst)
{
    dw1000_time_instance_t * time =  inst->time;
    if (time->config.postprocess)
        os_eventq_put(os_eventq_dflt_get(), &time_callout_postprocess.c_ev);

}

dw1000_time_instance_t * dw1000_time_init(dw1000_dev_instance_t * inst, uint16_t slot_id)
{
    if (inst->time == NULL ) {
        inst->time = (dw1000_time_instance_t *) malloc(sizeof(dw1000_time_instance_t));
        assert(inst->time);
        memset(inst->time, 0, sizeof(dw1000_time_instance_t));
        inst->time->status.selfmalloc = 1;
    }

    inst->time->parent = inst;
    inst->time->slot_id = slot_id;
    dw1000_time_set_postprocess(inst, &time_postprocess);
    dw1000_time_set_callbacks(inst, time_rx_ccp_complete_cb);
    inst->time->status.initialized = 1;
    return inst->time;
}

void dw1000_time_set_callbacks(dw1000_dev_instance_t * inst, dw1000_dev_cb_t time_ccp_rx_complete_cb)
{
    inst->time_ccp_rx_complete_cb = time_ccp_rx_complete_cb;
}

void
dw1000_time_set_postprocess(dw1000_dev_instance_t * inst, os_event_fn * time_postprocess){
    os_callout_init(&time_callout_postprocess, os_eventq_dflt_get(), time_postprocess, (void *) inst);
    dw1000_time_instance_t * time = inst->time;
    time->config.postprocess = true;
}

static void time_postprocess(struct os_event * ev){
    assert(ev != NULL);
    assert(ev->ev_arg != NULL);
    dw1000_dev_instance_t * inst = (dw1000_dev_instance_t *)ev->ev_arg;
    dw1000_time_instance_t * time = inst->time;

    uint64_t delta = MYNEWT_VAL(TDMA_DELTA) * 1000000 ;
    uint32_t  nslots = MYNEWT_VAL(TDMA_NSLOTS);
    printf("ccp_reception_timestamp == %llu\n",time->ccp_reception_timestamp);
    time->transmission_timestamp = time->ccp_reception_timestamp + time->slot_id * delta / nslots;
    printf("transmission_timestamp == %llu\n",time->transmission_timestamp);

}

#endif // DW1000_TIME
