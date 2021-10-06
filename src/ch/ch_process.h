/*
 * Copyright Intel Corp. 2020
 *
 * ch_driver.h: header file for Cloud-Hypervisor driver functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "internal.h"

typedef enum {
    VIR_CH_PROCESS_START_PAUSED = 1 << 0,
} chProcessStartFlags;

int virCHProcessStart(virCHDriverPtr  driver,
                      virDomainObjPtr vm,
                      virDomainRunningReason reason,
                      unsigned int flags);
int virCHProcessStop(virCHDriverPtr driver,
                     virDomainObjPtr vm,
                     virDomainShutoffReason reason);

int virCHProcessSetupThreads(virDomainObjPtr vm);

int virCHProcessSetupVcpu(virDomainObjPtr vm,
                          unsigned int vcpuid);

int virCHProcessSetupIOThread(virDomainObjPtr vm,
                             virDomainIOThreadInfoPtr iothread);

int
virCHProcessSetupEmulatorThread(virDomainObjPtr vm, pid_t tid);

void
chProcessReconnectAll(virCHDriverPtr driver);

int virCHProcessFinishStartup(virCHDriverPtr driver,
                              virDomainObjPtr vm,
                              bool startCPUs,
                              virDomainRunningReason reason,
                              virDomainPausedReason pausedReason);
