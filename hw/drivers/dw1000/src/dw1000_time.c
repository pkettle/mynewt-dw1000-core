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

//#if MYNEWT_VAL(DW1000_CLOCK_CALIBRATION)
//#include <dw1000/dw1000_ccp.h>
#include <dw1000/dw1000_time.h>

void time_rx_ccp_complete_cb(dw1000_dev_instance_t * inst)
{
    dw1000_time_instance_t * time =  inst->time;
    printf("correction factor == %lu\n",(uint32_t)(time->correction_factor * 1000000000));
    printf("reception_timestamp == %llu\n",time->reception_timestamp);

}

dw1000_time_instance_t * dw1000_timer_init(dw1000_dev_instance_t * inst, uint16_t slot_id)
{
    if (inst->time == NULL ) {
        inst->time = (dw1000_time_instance_t *) malloc(sizeof(dw1000_time_instance_t));
    assert(inst->time);
    memset(inst->time, 0, sizeof(dw1000_time_instance_t));
    inst->time->status.selfmalloc = 1;
    }

    inst->time->parent = inst;
    inst->time->slot_id = slot_id;
    dw1000_timer_set_callbacks(inst, time_rx_ccp_complete_cb);
    return inst->time;
}

void dw1000_timer_set_callbacks(dw1000_dev_instance_t * inst, time_ccp_rx_complete_cb time_ccp_rx_complete_cb_t)
{
    inst->time_ccp_rx_complete_cb = time_ccp_rx_complete_cb_t;
}
