#include "KTransferMemory.h"
#include <nce.h>
#include <os.h>
#include <asm/unistd.h>

namespace skyline::kernel::type {
    KTransferMemory::KTransferMemory(const DeviceState &state, pid_t pid, u64 address, size_t size, const memory::Permission permission) : owner(pid), cSize(size), permission(permission), KObject(state, KType::KTransferMemory) {
        if (pid) {
            Registers fregs{};
            fregs.x0 = address;
            fregs.x1 = size;
            fregs.x2 = static_cast<u64 >(permission.Get());
            fregs.x3 = static_cast<u64>(MAP_ANONYMOUS | MAP_PRIVATE | ((address) ? MAP_FIXED : 0));
            fregs.x4 = static_cast<u64>(-1);
            fregs.x8 = __NR_mmap;
            state.nce->ExecuteFunction(ThreadCall::Syscall, fregs, pid);
            if (fregs.x0 < 0)
                throw exception("An error occurred while mapping shared region in child process");
            cAddress = fregs.x0;
        } else {
            address = reinterpret_cast<u64>(mmap(reinterpret_cast<void *>(address), size, permission.Get(), MAP_ANONYMOUS | MAP_PRIVATE | ((address) ? MAP_FIXED : 0), -1, 0));
            if (reinterpret_cast<void *>(address) == MAP_FAILED)
                throw exception("An error occurred while mapping transfer memory in kernel");
            cAddress = address;
        }
    }

    u64 KTransferMemory::Transfer(pid_t process, u64 address, u64 size) {
        if (process) {
            Registers fregs{};
            fregs.x0 = address;
            fregs.x1 = size;
            fregs.x2 = static_cast<u64 >(permission.Get());
            fregs.x3 = static_cast<u64>(MAP_ANONYMOUS | MAP_PRIVATE | ((address) ? MAP_FIXED : 0));
            fregs.x4 = static_cast<u64>(-1);
            fregs.x8 = __NR_mmap;
            state.nce->ExecuteFunction(ThreadCall::Syscall, fregs, process);
            if (fregs.x0 < 0)
                throw exception("An error occurred while mapping transfer memory in child process");
            address = fregs.x0;
        } else {
            address = reinterpret_cast<u64>(mmap(reinterpret_cast<void *>(address), size, permission.Get(), MAP_ANONYMOUS | MAP_PRIVATE | ((address) ? MAP_FIXED : 0), -1, 0));
            if (reinterpret_cast<void *>(address) == MAP_FAILED)
                throw exception("An error occurred while mapping transfer memory in kernel");
        }
        size_t copySz = std::min(size, cSize);
        if (process && !owner) {
            state.process->WriteMemory(reinterpret_cast<void *>(cAddress), address, copySz);
        } else if (!process && owner) {
            state.process->ReadMemory(reinterpret_cast<void *>(address), cAddress, copySz);
        } else
            throw exception("Transferring from kernel to kernel is not supported");
        if (owner) {
            Registers fregs{};
            fregs.x0 = address;
            fregs.x1 = size;
            fregs.x8 = __NR_munmap;
            state.nce->ExecuteFunction(ThreadCall::Syscall, fregs, owner);
            if (fregs.x0 < 0)
                throw exception("An error occurred while unmapping transfer memory in child process");
        } else {
            if (reinterpret_cast<void *>(munmap(reinterpret_cast<void *>(address), size)) == MAP_FAILED)
                throw exception("An error occurred while unmapping transfer memory in kernel");
        }
        owner = process;
        cAddress = address;
        cSize = size;
        return address;
    }

    memory::MemoryInfo KTransferMemory::GetInfo() {
        memory::MemoryInfo info{};
        info.baseAddress = cAddress;
        info.size = cSize;
        info.type = static_cast<u64>(memory::Type::TransferMemory);
        info.memoryAttribute.isIpcLocked = (info.ipcRefCount > 0);
        info.memoryAttribute.isDeviceShared = (info.deviceRefCount > 0);
        info.r = permission.r;
        info.w = permission.w;
        info.x = permission.x;
        info.ipcRefCount = ipcRefCount;
        info.deviceRefCount = deviceRefCount;
        return info;
    }

    KTransferMemory::~KTransferMemory() {
        if (owner) {
            try {
                if (state.process) {
                    Registers fregs{};
                    fregs.x0 = cAddress;
                    fregs.x1 = cSize;
                    fregs.x8 = __NR_munmap;
                    state.nce->ExecuteFunction(ThreadCall::Syscall, fregs, state.process->pid);
                }
            } catch (const std::exception &) {
            }
        } else
            munmap(reinterpret_cast<void *>(cAddress), cSize);
    }
};