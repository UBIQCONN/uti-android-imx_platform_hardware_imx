/*
 * Copyright 2021 NXP.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <cutils/log.h>
#include <ion/ion.h>
#include <linux/dma-buf.h>
#include <linux/dma-buf-imx.h>
#include <linux/version.h>
#include "DmaHeapAllocator.h"

namespace fsl {

DmaHeapAllocator* DmaHeapAllocator::sInstance(0);
Mutex DmaHeapAllocator::sLock(Mutex::PRIVATE);

DmaHeapAllocator* DmaHeapAllocator::getInstance()
{
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new DmaHeapAllocator();

    return sInstance;
}

DmaHeapAllocator::DmaHeapAllocator()
{
    mBufferAllocator = CreateDmabufHeapBufferAllocator();
    if (!mBufferAllocator) {
        ALOGE("create CreateDmabufHeapBufferAllocator failed");
    }
}

DmaHeapAllocator::~DmaHeapAllocator()
{
    if (mBufferAllocator)
        FreeDmabufHeapBufferAllocator(mBufferAllocator);
}

int DmaHeapAllocator::allocMemory(int size, int align, int flags)
{
    int fd = -1;

    // VPU decoder needs 32k physical address alignment.
    // But align parameter can't take effect to ensure alignment.
    size = (size + (PAGE_SIZE << 3)) & (~((PAGE_SIZE << 3) - 1));
    // contiguous memory includes cacheable/non-cacheable.
    if (flags & MFLAGS_SECURE) {
        fd = DmabufHeapAlloc(mBufferAllocator, "secure", size, 0, 0);
        if (fd < 0)
             ALOGE("%s DmabufHeapAlloc MFLAGS_SECURE failed ", __func__);
    }
    else if (flags & MFLAGS_CONTIGUOUS) {
        if (flags & MFLAGS_CACHEABLE)
            fd = DmabufHeapAlloc(mBufferAllocator, "reserved", size, 0, 0);
        else
            fd = DmabufHeapAlloc(mBufferAllocator, "reserved-uncached", size, 0, 0);

        if (fd < 0)
            ALOGE("%s DmabufHeapAlloc MFLAGS_CONTIGUOUS failed ", __func__);
    }
    // cacheable memory includes non-contiguous.
    // it will not go into this logic currently, it always allocate continue memory
    else if (flags & MFLAGS_CACHEABLE) {
        fd = DmabufHeapAlloc(mBufferAllocator, "system", size, 0, 0);
        if (fd < 0)
             ALOGE("%s DmabufHeapAlloc MFLAGS_CACHEABLE failed ", __func__);
    }
    else {
        ALOGE("%s invalid flags:0x%x", __func__, flags);
        return fd;
    }

    return fd;
}

int DmaHeapAllocator::getPhys(int fd, int size, uint64_t& addr)
{
    uint64_t phyAddr = -1;

    if (fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    struct dma_buf_phys dma_phys;
    if (ioctl(fd, DMA_BUF_IOCTL_PHYS, &dma_phys) < 0) {
        ALOGV("%s DMA_BUF_IOCTL_PHYS failed",__func__);
        struct dmabuf_imx_phys_data data;
        int fd_;
        fd_ = open("/dev/dmabuf_imx", O_RDONLY | O_CLOEXEC);
        if (fd_ < 0) {
            ALOGE("open /dev/dmabuf_imx failed: %s", strerror(errno));
            return -EINVAL;
        }
        data.dmafd = fd;
        if (ioctl(fd_, DMABUF_GET_PHYS, &data) < 0) {
            ALOGE("%s DMABUF_GET_PHYS  failed",__func__);
            close(fd_);
            return -EINVAL;
        } else
            phyAddr = data.phys;
        close(fd_);
    } else {
        phyAddr = dma_phys.phys;
    }

    addr = phyAddr;
    return 0;
}

int DmaHeapAllocator::getVaddrs(int fd, int size, uint64_t& addr)
{
    if (fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    void* vaddr = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (vaddr == MAP_FAILED) {
        ALOGE("Could not mmap %s", strerror(errno));
        return -EINVAL;
    }

    addr = (uintptr_t)vaddr;
    return 0;
}

int DmaHeapAllocator::flushCache(int fd)
{
    if (fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    struct dma_buf_sync dma_sync;
    dma_sync.flags = DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END;
    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &dma_sync) < 0) {
        ALOGE("%s DMA_BUF_IOCTL_SYNC failed",__func__);
        return -EINVAL;
    }

    return 0;

}
}
