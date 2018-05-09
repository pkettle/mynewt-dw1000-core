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
#include <dw1000/dw1000_ccp.h>

static void time_postprocess(struct os_event * ev);
static void time_ev_cb(struct os_event * ev);
static struct os_callout time_callout_postprocess;
static uint64_t usec_exp = 0;

/*! 
 * @fn dw1000_time_init(dw1000_dev_instance_t * inst)
 *
 * @brief Init function for time module. 
 * 
 * input parameters
 * @param inst - dw1000_dev_instance_t * 
 * @param slot_id - Slot number of the device
 *
 * output parameters
 *
 * returns none
 */
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
    inst->time->ccp_interval = 0xFFFFFFFF;
    dw1000_time_set_postprocess(inst, &time_postprocess);
    dw1000_ccp_set_postprocess(inst, &time_ev_cb);
    dw1000_time_set_callbacks(inst, NULL);
    inst->time->status.initialized = 1;
    return inst->time;
}

/*! 
 * @fn dw1000_time_set_callbacks(dw1000_dev_instance_t * inst, dw1000_dev_cb_t time_cb)
 *
 * @brief Set callbacks for time module 
 * 
 * input parameters
 * @param inst - dw1000_dev_instance_t * 
 * @param time_cb - dw1000_dev_cb_t
 *
 * output parameters
 *
 * returns none
 */
void dw1000_time_set_callbacks(dw1000_dev_instance_t * inst, dw1000_dev_cb_t time_cb)
{
    inst->time_cb = time_cb;
}

/*! 
 * @fn dw1000_time_set_postprocess(dw1000_dev_instance_t * inst, os_event_fn * time_postprocess)
 *
 * @brief Overrides the default post-processing behaviors, replacing the simple interpolation logic
 * with more advanced algorithm 
 *
 * input parameters
 * @param inst - dw1000_dev_instance_t * 
 * @param time_postprocess - os_event_fn*
 *
 * returns none
 */
void
dw1000_time_set_postprocess(dw1000_dev_instance_t * inst, os_event_fn * time_postprocess){
    os_callout_init(&time_callout_postprocess, os_eventq_dflt_get(), time_postprocess, (void *) inst);
    dw1000_time_instance_t * time = inst->time;
    time->config.postprocess = true;
}

/*!
 * @fn time_postprocess(struct os_event * ev)
 *
 * @brief this is the default implementation of times post process
 *
 * input parameters
 * @param ev - os_event*
 *
 * output parameters
 *
 * returns relative time value in dwt units
 */
static void
time_postprocess(struct os_event * ev){
    assert(ev != NULL);
    assert(ev->ev_arg != NULL);
    dw1000_dev_instance_t * inst = (dw1000_dev_instance_t *)ev->ev_arg;
    dw1000_time_instance_t * time = inst->time;
    
    uint32_t delay = time->slot_id * MYNEWT_VAL(TDMA_DELTA)/MYNEWT_VAL(TDMA_NSLOTS);
    //Calculate the transmission timestamp using the CCP reception timestamp
    time->transmission_timestamp = time_absolute(inst, time->ccp_reception_timestamp, delay);
    time->status.ccp_packet_received = true;
}

/*!
 * @fn time_ev_cb(struct os_event *ev)
 *
 * @brief This function overrides the default ccp_postprocess method. This function calculates
 * ccp interval using the current and previous timestamps and also the transmission timestamp
 *  and then calls cout the time_postprocess
 *
 * input parameters
 * @param ev - os_event*
 *
 * output parameters
 *
 * returns relative time value in dwt units
 */
void
time_ev_cb(struct os_event *ev){
    assert(ev != NULL);
    assert(ev->ev_arg != NULL);
    dw1000_dev_instance_t *inst = (dw1000_dev_instance_t *)ev->ev_arg;
    dw1000_time_instance_t *time = inst->time;
    dw1000_ccp_instance_t *ccp = inst->ccp;
    
    ccp_frame_t * previous_frame = ccp->frames[(ccp->idx-1)%ccp->nframes];
    ccp_frame_t * frame = ccp->frames[ccp->idx%ccp->nframes];
    
    time->correction_factor = frame->correction_factor;
    time->ccp_reception_timestamp = frame->reception_timestamp;

    //Current rate of ccp packets in microseconds
    if(previous_frame->reception_timestamp > 0){
        uint64_t temp = 0;
        if(frame->reception_timestamp > previous_frame->reception_timestamp)
            temp = (uint64_t)((frame->reception_timestamp - previous_frame->reception_timestamp)*DWT_TIME_UNITS*1e6);
        else //When wrap around happens
            temp = (uint64_t)(((0xFFFFFFFFFF - previous_frame->reception_timestamp) + frame->reception_timestamp)*DWT_TIME_UNITS*1e6);
        time->ccp_interval = temp>0?temp:time->ccp_interval;
    }
    if (time->config.postprocess)
        os_eventq_put(os_eventq_dflt_get(), &time_callout_postprocess.c_ev);
}

/*! 
 * @fn time_relative(dw1000_dev_instance_t* inst, uint32_t delay)
 *
 * @brief This function calculates a time delayed by delay microseconds 
 * wrt to the current system time
 * 
 * input parameters
 * @param inst - dw1000_dev_instance_t *
 * @param delay - delay value in microseconds by which the relative time needs to be calculated
 *
 * output parameters
 *
 * returns relative time value in dwt units
 */
uint64_t
time_relative(dw1000_dev_instance_t* inst, uint32_t delay){
   return time_absolute(inst, dw1000_read_systime(inst), delay);
}

/*! 
 * @fn time_absolute(dw1000_dev_instance_t* inst, uint64_t epoch, uint32_t delay)
 *
 * @brief This function calculates a time delayed by delay microseconds 
 * wrt to the epoch time
 * 
 * input parameters
 * @param inst - dw1000_dev_instance_t *
 * @param epoch - timestamp in dwt units from which the time needs to be calculated
 * @param delay - delay value in microseconds by which the absolute time needs to be calculated
 *
 * output parameters
 *
 * returns absolute time value in dwt units
 */
uint64_t
time_absolute(dw1000_dev_instance_t* inst, uint64_t epoch, uint32_t delay){
    //Shrink it to 40 bits
    return ((uint64_t)((epoch + ((delay*1e-6)/DWT_TIME_UNITS)))&0xFFFFFFFFFF);
}

/*! 
 * @fn time_now(dw1000_dev_instance_t* inst)
 *
 * @brief This function calculates the drift between the transmitter's dw1000 clock
 * and receiver's dw1000 clock using the ppm value derived from tracking offset & trackking interval
 * register values
 * 
 * input parameters
 * @param inst - dw1000_dev_instance_t *
 *
 * output parameters
 *
 * returns a timestamp in dwt units relative to the transmitter's system clock
 */
uint64_t
time_now(dw1000_dev_instance_t* inst){
    int32_t tracking_interval = (int32_t) dw1000_read_reg(inst, RX_TTCKI_ID, 0, sizeof(int32_t));
    int32_t tracking_offset = (int32_t) dw1000_read_reg(inst, RX_TTCKO_ID, 0, sizeof(int32_t)) & RX_TTCKO_RXTOFS_MASK;
    //The tracking offset is a signed integer value with a length of 19bits
    //So extend the signed bits to fetch it as signed decimal if its 19th bit is 1
    if((tracking_offset & 0x40000))
        tracking_offset = tracking_offset | 0xfff80000;
    //Calculate the ppm(Refer DW1000 User Manual for the formula)
    int32_t ppm = tracking_interval/tracking_offset;
    //Calculate the drift in usec
    //Formula drift in Hz = ppm * clock_frequency(Hz) * 1e-6
    //Drift in usec = 1/(drift in Hz) * 1e6
    int32_t drift = 1/(ppm * DWT_SYS_CLK_FRQ);
    return time_relative(inst,drift);
}


bool check_time(dw1000_dev_instance_t* inst){
    bool status = false;
    dw1000_time_instance_t* time = inst->time;
    uint64_t delta = MYNEWT_VAL(TDMA_DELTA);
    uint32_t  nslots = MYNEWT_VAL(TDMA_NSLOTS);

    if(time->status.ccp_packet_received == true){
        usec_exp = os_cputime_ticks_to_usecs(os_cputime_get32());
        time->status.ccp_packet_received = false;
        status = true;
    }else if(time->ccp_interval < ((os_cputime_ticks_to_usecs(os_cputime_get32()))-usec_exp)){
        usec_exp = os_cputime_ticks_to_usecs(os_cputime_get32());
        uint32_t delay = time->slot_id * delta / nslots;
        time->transmission_timestamp = time_relative(inst, delay);
        status = true;
    }
    return status;
}
#endif // DW1000_TIME
