#include <common/core/memory/msecurity.h>
#include <hal/memory/virt.h>

bool MemorySecurity_VerifyMemoryRangePermissions(uintptr_t start, uintptr_t end, int flags) {
	if (start + end < start) {
		return false;
	}
	uintptr_t root = HAL_VirtualMM_GetCurrentAddressSpace();
	uintptr_t alignedStart = ALIGN_DOWN(start, HAL_VirtualMM_PageSize);
	uintptr_t alignedEnd = ALIGN_UP(end, HAL_VirtualMM_PageSize);
	int result = HAL_VIRT_FLAGS_EXECUTABLE | HAL_VIRT_FLAGS_READABLE | HAL_VIRT_FLAGS_DISABLE_CACHE |
				 HAL_VIRT_FLAGS_USER_ACCESSIBLE | HAL_VIRT_FLAGS_WRITABLE;
	for (uintptr_t addr = alignedStart; addr < alignedEnd; addr += HAL_VirtualMM_PageSize) {
		result &= HAL_VirtualMM_GetPagePermissions(root, addr);
		if ((result | flags) != result) {
			return false;
		}
	}
	return true;
}

int MemorySecurity_VerifyCString(uintptr_t start, int maxLength, int flags) {
	uintptr_t root = HAL_VirtualMM_GetCurrentAddressSpace();
	int stringFlags =
		HAL_VIRT_FLAGS_EXECUTABLE | HAL_VIRT_FLAGS_READABLE | HAL_VIRT_FLAGS_USER_ACCESSIBLE | HAL_VIRT_FLAGS_WRITABLE;
	for (uintptr_t addr = start; addr < start + (uintptr_t)maxLength; ++addr) {
		if (addr < start) {
			return false;
		}
		if (addr == start) {
			stringFlags = HAL_VirtualMM_GetPagePermissions(root, ALIGN_DOWN(addr, HAL_VirtualMM_PageSize));
			if ((stringFlags | flags) != stringFlags) {
				return -1;
			}
		} else if (addr % HAL_VirtualMM_PageSize == 0) {
			stringFlags &= HAL_VirtualMM_GetPagePermissions(root, addr);
			if ((stringFlags | flags) != stringFlags) {
				return -1;
			}
		}
		char *loc = (char *)addr;
		if (*loc == '\0') {
			return addr - start;
		}
	}
	return -1;
}

int MemorySecurity_VerifyNullTerminatedPointerList(uintptr_t start, int maxPointerCount, int flags) {
	if (start % sizeof(uintptr_t) != 0) {
		return -1;
	}
	uintptr_t root = HAL_VirtualMM_GetCurrentAddressSpace();
	int pointerFlags =
		HAL_VIRT_FLAGS_EXECUTABLE | HAL_VIRT_FLAGS_READABLE | HAL_VIRT_FLAGS_USER_ACCESSIBLE | HAL_VIRT_FLAGS_WRITABLE;
	for (uintptr_t addr = start; addr < start + sizeof(uintptr_t) * (uintptr_t)maxPointerCount;
		 addr += sizeof(uintptr_t)) {
		if (addr < start) {
			return false;
		}
		if (addr == start) {
			pointerFlags = HAL_VirtualMM_GetPagePermissions(root, ALIGN_DOWN(addr, HAL_VirtualMM_PageSize));
			if ((pointerFlags | flags) != pointerFlags) {
				return -1;
			}
		} else if (addr % HAL_VirtualMM_PageSize == 0) {
			pointerFlags &= HAL_VirtualMM_GetPagePermissions(root, addr);
			if ((pointerFlags | flags) != pointerFlags) {
				return -1;
			}
		}
		uintptr_t *loc = (uintptr_t *)addr;
		if (*loc == 0) {
			return (addr - start) / sizeof(uintptr_t);
		}
	}
	return -1;
}