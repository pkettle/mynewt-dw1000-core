/* Copyright 2018, Decawave Limited, All Rights Reserved
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

#ifndef _DW1000_TIME_H_
#define _DW1000_TIME_H_

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <hal/hal_spi.h>
#include <dw1000/dw1000_regs.h>
#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_ftypes.h>

typedef struct _dw1000_time_status_t{
    uint16_t selfmalloc:1;
    uint16_t initialized:1;
    uint16_t ccp_packet_received:1;
}dw1000_time_status_t;

typedef struct _dw1000_time_config_t{
        uint16_t postprocess:1;
}dw1000_time_config_t;

typedef struct _dw1000_time_instance_t{
    struct _dw1000_dev_instance_t * parent;
    dw1000_time_status_t status;
    dw1000_time_config_t config;
    struct os_callout callout_timer;
    uint16_t slot_id;
    float correction_factor;
    uint64_t ccp_reception_timestamp;
    uint64_t transmission_timestamp;
    uint64_t ccp_interval;
}dw1000_time_instance_t;

dw1000_time_instance_t * dw1000_time_init(dw1000_dev_instance_t * inst, uint16_t slot_id);
void dw1000_time_start(dw1000_dev_instance_t* inst);
void dw1000_time_stop(dw1000_dev_instance_t* inst);
void dw1000_time_free(dw1000_time_instance_t * inst);
void dw1000_time_set_callbacks(dw1000_dev_instance_t * inst, dw1000_dev_cb_t time_ccp_rx_complete_cb);
void dw1000_time_set_postprocess(dw1000_dev_instance_t * inst, os_event_fn * time_postprocess);

#ifdef __cplusplus
}
#endif
#endif /* _DW1001_time_H_ */
