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

#ifndef _DW1000_TDMA_H_
#define _DW1000_TDMA_H_

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_phy.h>

#if MYNEWT_VAL(TDMA_ENABLED)

typedef struct _tdma_status_t{
    uint16_t selfmalloc:1;
    uint16_t initialized:1;
}tdma_status_t;

typedef struct _tdma_instance_t{
    struct _dw1000_dev_instance_t * parent;
    tdma_status_t status;
    struct os_mutex mutex;
    uint16_t idx;
    uint16_t nslots;
    uint32_t period;
    struct os_callout superframe_timer_cb;
    struct os_callout * timer_cb[];
}tdma_instance_t; 

struct _tdma_instance_t * tdma_init(struct _dw1000_dev_instance_t * inst, uint32_t period, uint16_t nslots);
void tdma_free(struct _tdma_instance_t * inst);
void tdma_assign_slot(struct _tdma_instance_t * inst, void (* callout )(struct os_event *), uint16_t slot, void * arg);
void tdma_release_slot(struct _tdma_instance_t * inst, uint16_t slot);
void tdma_superframe_event_cb(struct os_event * ev);

#ifdef __cplusplus
}
#endif

#endif //MYNEWT_VAL(DW1000_TDMA_ENABLED)
#endif //_DW1000_TDMA_H_