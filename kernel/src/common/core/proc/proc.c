#include <common/core/memory/heap.h>
#include <common/core/memory/virt.h>
#include <common/core/proc/proc.h>
#include <common/core/proc/proclayout.h>
#include <common/lib/kmsg.h>
#include <hal/memory/phys.h>
#include <hal/memory/virt.h>
#include <hal/proc/intlock.h>
#include <hal/proc/isrhandler.h>
#include <hal/proc/stack.h>
#include <hal/proc/state.h>
#include <hal/proc/timer.h>

#define PROC_MOD_NAME "Process Manager & Scheduler"

static UINT64 Proc_InstanceCountsByID[PROC_MAX_PROCESS_COUNT];
static struct Proc_Process *Proc_ProcessesByID[PROC_MAX_PROCESS_COUNT];
static struct Proc_Process *Proc_CurrentProcess;
static struct Proc_Process *Proc_DeallocQueueHead;
static bool Proc_Initialized = false;

#define PROC_SCHEDULER_STACK_SIZE 65536
static char Proc_SchedulerStack[PROC_SCHEDULER_STACK_SIZE];

bool Proc_IsInitialized() {
	return Proc_Initialized;
}

struct Proc_Process *Proc_GetProcessData(struct Proc_ProcessID id) {
	USIZE array_index = id.id;
	if (array_index >= PROC_MAX_PROCESS_COUNT) {
		return NULL;
	}
	HAL_InterruptLock_Lock();
	struct Proc_Process *data = Proc_ProcessesByID[array_index];
	if (data == NULL) {
		HAL_InterruptLock_Unlock();
		return NULL;
	}
	if (Proc_InstanceCountsByID[array_index] != id.instance_number) {
		HAL_InterruptLock_Unlock();
		return NULL;
	}
	HAL_InterruptLock_Unlock();
	return data;
}

static struct Proc_ProcessID Proc_AllocateProcessID(struct Proc_Process *process) {
	struct Proc_ProcessID result;
	for (USIZE i = 0; i < PROC_MAX_PROCESS_COUNT; ++i) {
		HAL_InterruptLock_Lock();
		if (Proc_ProcessesByID[i] == NULL) {
			Proc_ProcessesByID[i] = process;
			result.id = i;
			result.instance_number = Proc_InstanceCountsByID[i];
			HAL_InterruptLock_Unlock();
			return result;
		}
		HAL_InterruptLock_Unlock();
	}
	result.id = PROC_MAX_PROCESS_COUNT;
	result.instance_number = 0;
	return result;
}

struct Proc_ProcessID Proc_MakeNewProcess(struct Proc_ProcessID parent) {
	struct Proc_Process *process = ALLOC_OBJ(struct Proc_Process);
	if (process == NULL) {
		goto fail;
	}
	UINTN stack = (UINTN)Heap_AllocateMemory(PROC_KERNEL_STACK_SIZE);
	if (stack == 0) {
		goto free_process_obj;
	}
	char *process_state = (char *)(Heap_AllocateMemory(HAL_PROCESS_STATE_SIZE));
	if (process_state == NULL) {
		goto free_stack;
	}
	struct Proc_ProcessID new_id = Proc_AllocateProcessID(process);
	if (!proc_is_valid_Proc_ProcessID(new_id)) {
		goto free_process_state;
	}
	memset(process_state, 0, HAL_PROCESS_STATE_SIZE);
	process->next = process->prev = process->waitQueueHead = process->waitQueueTail = process->nextInQueue = NULL;
	process->ppid = parent;
	process->pid = new_id;
	process->processState = process_state;
	process->kernelStack = stack;
	process->returnCode = 0;
	process->state = SLEEPING;
	process->address_space = NULL;
	return new_id;
free_process_state:
	Heap_FreeMemory(process_state, HAL_PROCESS_STATE_SIZE);
free_stack:
	Heap_FreeMemory((void *)stack, PROC_KERNEL_STACK_SIZE);
free_process_obj:
	FREE_OBJ(process);
fail:;
	struct Proc_ProcessID failed_id;
	failed_id.instance_number = 0;
	failed_id.id = PROC_MAX_PROCESS_COUNT;
	return failed_id;
}

void Proc_Resume(struct Proc_ProcessID id) {
	struct Proc_Process *process = Proc_GetProcessData(id);
	if (process == NULL) {
		return;
	}
	HAL_InterruptLock_Lock();
	process->state = RUNNING;
	struct Proc_Process *next, *prev;
	next = Proc_CurrentProcess;
	prev = Proc_CurrentProcess->prev;
	next->prev = process;
	prev->next = process;
	process->next = next;
	process->prev = prev;
	HAL_InterruptLock_Unlock();
}

static void Proc_CutFromActiveList(struct Proc_Process *process) {
	struct Proc_Process *prev = process->prev;
	struct Proc_Process *next = process->next;
	prev->next = next;
	next->prev = prev;
}

void Proc_Suspend(struct Proc_ProcessID id, bool overrideState) {
	struct Proc_Process *process = Proc_GetProcessData(id);
	if (process == NULL) {
		return;
	}
	HAL_InterruptLock_Lock();
	if (overrideState) {
		process->state = SLEEPING;
	}
	Proc_CutFromActiveList(process);
	if (process == Proc_CurrentProcess) {
		Proc_Yield();
	}
	HAL_InterruptLock_Unlock();
}

struct Proc_ProcessID Proc_GetProcessID() {
	struct Proc_Process *current = Proc_CurrentProcess;
	struct Proc_ProcessID id = current->pid;
	return id;
}

void Proc_SuspendSelf(bool overrideState) {
	Proc_Suspend(Proc_GetProcessID(), overrideState);
}

void Proc_Dispose(struct Proc_Process *process) {
	HAL_InterruptLock_Lock();
	process->nextInQueue = Proc_DeallocQueueHead;
	Proc_DeallocQueueHead = process;
	HAL_InterruptLock_Unlock();
}

void Proc_Exit(int exitCode) {
	HAL_InterruptLock_Lock();
	struct Proc_Process *process = Proc_CurrentProcess;
	process->returnCode = exitCode;
	Proc_InstanceCountsByID[process->pid.id]++;
	Proc_ProcessesByID[process->pid.id] = NULL;
	process->state = ZOMBIE;
	Proc_CutFromActiveList(process);
	struct Proc_ProcessID parentID = process->ppid;
	struct Proc_Process *parentProcess = Proc_GetProcessData(parentID);
	if (parentProcess == NULL) {
		Proc_Dispose(process);
	} else {
		if (parentProcess->waitQueueHead == NULL) {
			parentProcess->waitQueueHead = parentProcess->waitQueueTail = process;
		} else {
			parentProcess->waitQueueTail->nextInQueue = process;
		}
		process->nextInQueue = NULL;
		if (parentProcess->state == WAITING_FOR_CHILD_TERM) {
			Proc_Resume(parentID);
		}
	}
	Proc_Yield();
}

struct Proc_Process *Proc_GetWaitingQueueHead(struct Proc_Process *process) {
	struct Proc_Process *result = process->waitQueueHead;
	if (process->waitQueueTail == result) {
		process->waitQueueHead = process->waitQueueTail = NULL;
	} else {
		process->waitQueueHead = process->waitQueueHead->nextInQueue;
	}
	return result;
}

struct Proc_Process *Proc_WaitForChildTermination() {
	HAL_InterruptLock_Lock();
	struct Proc_Process *process = Proc_CurrentProcess;
	if (process->waitQueueHead != NULL) {
		struct Proc_Process *result = Proc_GetWaitingQueueHead(process);
		HAL_InterruptLock_Unlock();
		return result;
	}
	process->state = WAITING_FOR_CHILD_TERM;
	Proc_SuspendSelf(false);
	HAL_InterruptLock_Lock();
	struct Proc_Process *result = Proc_GetWaitingQueueHead(process);
	HAL_InterruptLock_Unlock();
	return result;
}

void Proc_Yield() {
	HAL_InterruptLock_Flush();
	HAL_Timer_TriggerInterrupt();
}

void Proc_PreemptCallback(UNUSED void *ctx, char *frame) {
	memcpy(Proc_CurrentProcess->processState, frame, HAL_PROCESS_STATE_SIZE);
	Proc_CurrentProcess = Proc_CurrentProcess->next;
	memcpy(frame, Proc_CurrentProcess->processState, HAL_PROCESS_STATE_SIZE);
	VirtualMM_SwitchToAddressSpace(Proc_CurrentProcess->address_space);
	HAL_ISRStacks_SetSyscallsStack(Proc_CurrentProcess->kernelStack + PROC_KERNEL_STACK_SIZE);
}

void Proc_Initialize() {
	for (USIZE i = 0; i < PROC_MAX_PROCESS_COUNT; ++i) {
		Proc_ProcessesByID[i] = NULL;
		Proc_InstanceCountsByID[i] = 0;
	}
	struct Proc_ProcessID kernelProcID = Proc_MakeNewProcess(PROC_INVALID_PROC_ID);
	if (!proc_is_valid_Proc_ProcessID(kernelProcID)) {
		KernelLog_ErrorMsg(PROC_MOD_NAME, "Failed to allocate kernel process");
	}
	struct Proc_Process *kernelProcessData = Proc_GetProcessData(kernelProcID);
	if (kernelProcessData == NULL) {
		KernelLog_ErrorMsg(PROC_MOD_NAME, "Failed to access data of the kernel process");
	}
	kernelProcessData->state = RUNNING;
	Proc_CurrentProcess = kernelProcessData;
	kernelProcessData->next = kernelProcessData;
	kernelProcessData->prev = kernelProcessData;
	kernelProcessData->processState = Heap_AllocateMemory(HAL_PROCESS_STATE_SIZE);
	if (kernelProcessData->processState == NULL) {
		KernelLog_ErrorMsg(PROC_MOD_NAME, "Failed to allocate kernel process state");
	}
	kernelProcessData->address_space = VirtualMM_MakeAddressSpaceFromRoot(HAL_VirtualMM_GetCurrentAddressSpace());
	if (kernelProcessData->address_space == NULL) {
		KernelLog_ErrorMsg(PROC_MOD_NAME, "Failed to allocate process address space object");
	}
	Proc_DeallocQueueHead = NULL;
	HAL_ISRStacks_SetISRStack((UINTN)(Proc_SchedulerStack) + PROC_SCHEDULER_STACK_SIZE);
	if (!HAL_Timer_SetCallback((HAL_ISR_Handler)Proc_PreemptCallback)) {
		KernelLog_ErrorMsg(PROC_MOD_NAME, "Failed to set timer callback");
	}
	Proc_Initialized = true;
}

bool Proc_PollDisposeQueue() {
	HAL_InterruptLock_Lock();
	struct Proc_Process *process = Proc_DeallocQueueHead;
	if (process == NULL) {
		HAL_InterruptLock_Unlock();
		return false;
	}
	KernelLog_InfoMsg("User Request Monitor", "Disposing process...");
	Proc_DeallocQueueHead = process->nextInQueue;
	HAL_InterruptLock_Unlock();
	if (process->address_space != 0) {
		VirtualMM_DropAddressSpace(process->address_space);
	}
	if (process->kernelStack != 0) {
		Heap_FreeMemory((void *)(process->kernelStack), PROC_KERNEL_STACK_SIZE);
	}
	FREE_OBJ(process);
	return true;
}