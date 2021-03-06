#ifndef __VIRT_H_INCLUDED__
#define __VIRT_H_i686_Ports_ReadDoubleWordCUDED__

#include <common/core/proc/mutex.h>
#include <common/lib/rbtree.h>
#include <common/misc/utils.h>

struct VirtualMM_MemoryRegionBase {
	struct RedBlackTree_Node base;
	uintptr_t start;
	uintptr_t end;
	size_t size;
};

struct VirtualMM_MemoryHoleNode {
	struct VirtualMM_MemoryRegionBase base;
	size_t maxSize;
	size_t minSize;
	struct VirtualMM_MemoryRegionNode *correspondingRegion;
};

struct VirtualMM_MemoryRegionNode {
	struct VirtualMM_MemoryRegionBase base;
	struct VirtualMM_MemoryHoleNode *correspondingHole;
	int flags;
	bool isUsed;
};

struct VirtualMM_RegionTrees {
	struct RedBlackTree_Tree holesTreeRoot;
	struct RedBlackTree_Tree regionsTreeRoot;
};

struct VirtualMM_AddressSpace {
	uintptr_t root;
	size_t refCount;
	struct Mutex mutex;
	struct VirtualMM_RegionTrees trees;
};

struct VirtualMM_AddressSpace *VirtualMM_GetCurrentAddressSpace();
struct VirtualMM_MemoryRegionNode *VirtualMM_MemoryMap(struct VirtualMM_AddressSpace *space, uintptr_t addr,
													   size_t size, int flags, bool lock);
int VirtualMM_MemoryUnmap(struct VirtualMM_AddressSpace *space, uintptr_t addr, size_t size, bool lock);
void VirtualMM_MemoryRetype(struct VirtualMM_AddressSpace *space, struct VirtualMM_MemoryRegionNode *region, int flags);
struct VirtualMM_AddressSpace *VirtualMM_MakeAddressSpaceFromRoot(uintptr_t root);
void VirtualMM_DropAddressSpace(struct VirtualMM_AddressSpace *space);
struct VirtualMM_AddressSpace *VirtualMM_ReferenceAddressSpace(struct VirtualMM_AddressSpace *space);
struct VirtualMM_AddressSpace *VirtualMM_MakeNewAddressSpace();
void VirtualMM_SwitchToAddressSpace(struct VirtualMM_AddressSpace *space);
void VirtualMM_PreemptToAddressSpace(struct VirtualMM_AddressSpace *space);
struct VirtualMM_AddressSpace *VirtualMM_CopyCurrentAddressSpace();

#endif