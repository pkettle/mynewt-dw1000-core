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
static struct os_callout tdma_callout;
static void ccp_miss_cb(struct os_event *ev);
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
    //CCP miss watchdog
    os_callout_init(&tdma_callout, os_eventq_dflt_get(), ccp_miss_cb, inst);
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
    printf("Default implementation : Replace with a transmission timestamp calculation algo \n");
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
    //You have received a new CCP packet. So stop it while processing it
    os_callout_stop(&tdma_callout);
    ccp_frame_t * previous_frame = ccp->frames[(ccp->idx-1)%ccp->nframes];
    ccp_frame_t * frame = ccp->frames[ccp->idx%ccp->nframes];
    
    time->correction_factor = frame->correction_factor;
    time->ccp_reception_timestamp = frame->reception_timestamp;
    uint64_t temp = 0;

    //Current rate of ccp packets in microseconds
    if(frame->seq_num == (uint8_t)(previous_frame->seq_num+1)){
        if(frame->reception_timestamp > previous_frame->reception_timestamp)
            temp = (uint64_t)((frame->reception_timestamp - previous_frame->reception_timestamp)*DWT_TIME_UNITS*1e6);
        else //When wrap around happens
            temp = (uint64_t)(((0xFFFFFFFFFF - previous_frame->reception_timestamp) + frame->reception_timestamp)*DWT_TIME_UNITS*1e6);
        time->ccp_interval = temp>0?temp:time->ccp_interval;
    }
    if (time->config.postprocess && temp > 0)
        os_eventq_put(os_eventq_dflt_get(), &time_callout_postprocess.c_ev);
    else
        dw1000_restart_rx(inst, inst->control);
    //Your processing is complete. So start the timer again so that it will catch next CCP miss
    //Wait an extra slot duration to make sure that the CCP packet is actually missed.
    //This will be common across all the tags & nodes
    //This is done becoz the timer task is not accurate and the granularity is pretty small. So it tends to kick in before the actual CCP packet arives everytime
    os_callout_reset(&tdma_callout, OS_TICKS_PER_SEC*(inst->time->ccp_interval + MYNEWT_VAL(TDMA_DELTA)/MYNEWT_VAL(TDMA_NSLOTS))*1e-6);
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
   return time_absolute(inst, time_now(inst), delay);
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
    assert(inst!=NULL);
    uint32_t ccp_period_actual = MYNEWT_VAL(CCP_PERIOD);
    dw1000_time_instance_t *time = inst->time;
    //Drift is expected timeperiod - actual timeperiod
    if(ccp_period_actual < time->ccp_interval){
        uint64_t guard_time = (inst->slot_id*(time->ccp_interval - ccp_period_actual))/MYNEWT_VAL(TDMA_NSLOTS);
        return ((uint64_t)((dw1000_read_systime(inst) - (uint64_t)(guard_time/DWT_TIME_UNITS/1e6)))&0xFFFFFFFFFF);
    }
    else{
        uint64_t guard_time = (inst->slot_id*(ccp_period_actual - time->ccp_interval))/MYNEWT_VAL(TDMA_NSLOTS);
        return ((uint64_t)((dw1000_read_systime(inst) + (uint64_t)(guard_time/DWT_TIME_UNITS/1e6)))&0xFFFFFFFFFF);
    }
}

static void
ccp_miss_cb(struct os_event *ev){
    assert(ev != NULL);
    assert(ev->ev_arg != NULL);
    printf("Packet mssed \n");
    dw1000_dev_instance_t * inst = (dw1000_dev_instance_t *)ev->ev_arg;
    //The receiver was turned on waiting for the CCP packet to come
    //So first switch it off and then send the range request
    dw1000_phy_forcetrxoff(inst);
    //Wait an extra slot duration to make sure that the CCP packet is actually missed.
    //This will be common across all the tags & nodes
    //This is done becoz the timer task is not accurate and the granularity is pretty small. So it tends to kick in before the actual CCP packet arives everytime
    os_callout_reset(&tdma_callout, OS_TICKS_PER_SEC*(inst->time->ccp_interval + MYNEWT_VAL(TDMA_DELTA)/MYNEWT_VAL(TDMA_NSLOTS))*1e-6);
    //calculate an imaginary reception timestamp which could be like previous good reception timestamp + n*the os_callout wait time, where n is the no of times packets has been missed
    inst->time->ccp_reception_timestamp = time_absolute(inst,inst->time->ccp_reception_timestamp,(inst->time->ccp_interval+ MYNEWT_VAL(TDMA_DELTA)/MYNEWT_VAL(TDMA_NSLOTS)));
    os_eventq_put(os_eventq_dflt_get(), &time_callout_postprocess.c_ev);
}
#endif // DW1000_TIME
