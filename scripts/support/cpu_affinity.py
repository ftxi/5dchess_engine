"""Small Linux/Windows CPU-affinity helpers and a POSIX exec launcher."""

import ctypes
import os
import sys


def _kernel32():
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.GetCurrentProcess.restype = ctypes.c_void_p
    api.OpenProcess.argtypes = (ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong)
    api.OpenProcess.restype = ctypes.c_void_p
    api.CloseHandle.argtypes = (ctypes.c_void_p,)
    api.GetProcessAffinityMask.argtypes = (
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t),
    )
    api.SetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
    return api


def available_cpus() -> set[int] | None:
    if sys.platform == "win32":
        api = _kernel32()
        process_mask, system_mask = ctypes.c_size_t(), ctypes.c_size_t()
        if not api.GetProcessAffinityMask(
            api.GetCurrentProcess(), ctypes.byref(process_mask), ctypes.byref(system_mask)
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        return {i for i in range(ctypes.sizeof(process_mask) * 8)
                if process_mask.value & (1 << i)}
    if hasattr(os, "sched_getaffinity"):
        return set(os.sched_getaffinity(0))
    return None


def pin_process(pid: int, cpus: set[int]) -> None:
    if not cpus:
        raise ValueError("CPU affinity cannot be empty")
    if sys.platform == "win32":
        api = _kernel32()
        handle = api.OpenProcess(0x0200 | 0x1000, False, pid)
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            if not api.SetProcessAffinityMask(handle, sum(1 << cpu for cpu in cpus)):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            api.CloseHandle(handle)
    elif hasattr(os, "sched_setaffinity"):
        os.sched_setaffinity(pid, cpus)
    else:
        raise OSError("CPU affinity is unavailable on this platform")


if __name__ == "__main__":
    # Pin before exec so every engine thread inherits the same mask. This
    # avoids preexec_fn, which is unsafe in a threaded asyncio controller.
    pin_process(os.getpid(), {int(sys.argv[1])})
    os.execvp(sys.argv[2], sys.argv[2:])
