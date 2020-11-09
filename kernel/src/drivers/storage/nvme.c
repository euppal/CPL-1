#include <drivers/pci.h>
#include <drivers/storage/nvme.h>
#include <kmsg.h>
#include <memory/heap.h>
#include <memory/virt.h>
#include <proc/mutex.h>
#include <utils.h>

#define NVME_SUBMISSION_QUEUE_SIZE 2
#define NVME_COMPLETITION_QUEUE_SIZE 2
#define NVME_MAX_MDTS 8
#define NVME_QUEUE_PHYS_CONTIG (1 << 0)

struct nvme_cap_register {
	uint32_t mqes : 16;
	uint32_t cqr : 1;
	uint32_t ams_is_weighted_rr_supported : 1;
	uint32_t ams_vendor_specific : 1;
	uint32_t : 5;
	uint32_t to : 8;
	uint32_t dstrd : 4;
	uint32_t nssrs : 1;
	uint32_t css_is_nvm_supported : 1;
	uint32_t : 6;
	uint32_t css_is_only_admin_supported : 1;
	uint32_t bps : 1;
	uint32_t : 2;
	uint32_t mpsmin : 4;
	uint32_t mpsmax : 4;
	uint32_t pmrs : 1;
	uint32_t cmbs : 1;
	uint32_t : 6;
} packed_align(8);

struct nvme_cc_register {
	uint32_t en : 1;
	uint32_t : 3;
	uint32_t css : 3;
	uint32_t mps : 4;
	uint32_t ams : 3;
	uint32_t shn : 2;
	uint32_t iosqes : 4;
	uint32_t iocqes : 4;
	uint32_t : 8;
} packed_align(4);

struct nvme_csts_register {
	uint32_t rdy : 1;
	uint32_t cfs : 1;
	uint32_t shst : 2;
	uint32_t nssro : 1;
	uint32_t pp : 1;
	uint32_t : 26;
} packed_align(4);

struct nvme_aqa_register {
	uint32_t asqs : 12;
	uint32_t : 4;
	uint32_t acqs : 12;
	uint32_t : 4;
} packed_align(4);

struct nvme_asq_register {
	uint64_t : 12;
	uint64_t page : 52;
} packed_align(8);

struct nvme_acq_register {
	uint64_t : 12;
	uint64_t page : 52;
} packed_align(8);

static struct nvme_cap_register
nvme_read_cap_register(volatile uint32_t *bar0) {
	struct nvme_cap_register result = {0};
	uint64_t *as_pointer = (uint64_t *)&result;
	*as_pointer = ((uint64_t)bar0[1] << 32ULL) | (uint64_t)bar0[0];
	return result;
}

static struct nvme_cc_register nvme_read_cc_register(volatile uint32_t *bar0) {
	struct nvme_cc_register result = {0};
	uint32_t *as_pointer = (uint32_t *)&result;
	*as_pointer = bar0[5];
	return result;
}

static struct nvme_csts_register
nvme_read_csts_register(volatile uint32_t *bar0) {
	struct nvme_csts_register result = {0};
	uint32_t *as_pointer = (uint32_t *)&result;
	*as_pointer = bar0[7];
	return result;
}

static struct nvme_aqa_register
nvme_read_aqa_register(volatile uint32_t *bar0) {
	struct nvme_aqa_register result = {0};
	uint32_t *as_pointer = (uint32_t *)&result;
	*as_pointer = bar0[9];
	return result;
}

static struct nvme_asq_register
nvme_read_asq_register(volatile uint32_t *bar0) {
	struct nvme_asq_register result = {0};
	uint64_t *as_pointer = (uint64_t *)&result;
	*as_pointer = ((uint64_t)bar0[11] << 32ULL) | (uint64_t)bar0[10];
	return result;
}

static struct nvme_acq_register
nvme_read_acq_register(volatile uint32_t *bar0) {
	struct nvme_acq_register result = {0};
	uint64_t *as_pointer = (uint64_t *)&result;
	*as_pointer = ((uint64_t)bar0[13] << 32ULL) | (uint64_t)bar0[12];
	return result;
}

static void nvme_write_cc_register(volatile uint32_t *bar0,
                                   struct nvme_cc_register cc) {
	uint32_t *as_pointer = (uint32_t *)&cc;
	bar0[5] = *as_pointer;
}

unused static void nvme_write_csts_register(volatile uint32_t *bar0,
                                            struct nvme_csts_register csts) {
	uint32_t *as_pointer = (uint32_t *)&csts;
	bar0[7] = *as_pointer;
}
static void nvme_write_aqa_register(volatile uint32_t *bar0,
                                    struct nvme_aqa_register aqa) {
	uint32_t *as_pointer = (uint32_t *)&aqa;
	bar0[9] = *as_pointer;
}

static void nvme_write_asq_register(volatile uint32_t *bar0,
                                    struct nvme_asq_register cap) {
	uint64_t *as_pointer = (uint64_t *)&cap;
	bar0[10] = (uint32_t)(*as_pointer & 0xFFFFFFFF);
	bar0[11] = (uint32_t)((*as_pointer >> 32ULL) & 0xFFFFFFFF);
}

static void nvme_write_acq_register(volatile uint32_t *bar0,
                                    struct nvme_acq_register cap) {
	uint64_t *as_pointer = (uint64_t *)&cap;
	bar0[12] = (uint32_t)(*as_pointer & 0xFFFFFFFF);
	bar0[13] = (uint32_t)((*as_pointer >> 32ULL) & 0xFFFFFFFF);
}

enum nvme_admin_opcode {
	NVME_CREATE_SUBMISSION_QUEUE = 0x01,
	NVME_CREATE_COMPLETITION_QUEUE = 0x05,
	NVME_IDENTIFY = 0x06,
	NVME_SET_FEATURES = 0x09,
	NVME_GET_FEATURES = 0x0a
};

enum nvme_io_opcode { NVME_CMD_WRITE = 0x01, NVME_CMD_READ = 0x02 };

union nvme_sq_entry {
	struct {
		uint8_t opcode;
		uint8_t flags;
		uint16_t command_id;
		uint32_t nsid;
		uint64_t : 64;
		uint64_t : 64;
		uint64_t prp1;
		uint64_t prp2;
		uint32_t cns;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
	} packed identify;
	struct {
		uint8_t opcode;
		uint8_t flags;
		uint16_t command_id;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint64_t prp1;
		uint64_t : 64;
		uint16_t sqid;
		uint16_t qsize;
		uint16_t sq_flags;
		uint16_t cqid;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
	} packed create_sq;
	struct {
		uint8_t opcode;
		uint8_t flags;
		uint16_t command_id;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint64_t prp1;
		uint64_t : 64;
		uint16_t cqid;
		uint16_t qsize;
		uint16_t cq_flags;
		uint16_t irq_vector;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
		uint32_t : 32;
	} packed create_cq;
	struct {
		uint8_t opcode;
		uint8_t flags;
		uint16_t command_id;
		uint32_t nsid;
		uint64_t rsvd2;
		uint64_t metadata;
		uint64_t prp1;
		uint64_t prp2;
		uint64_t slba;
		uint16_t length;
		uint16_t control;
		uint32_t dsmgmt;
		uint32_t reftag;
		uint16_t apptag;
		uint16_t appmask;
	} packed rw;
} packed;

struct nvme_cq_entry {
	uint32_t command_specific;
	uint32_t reserved;
	uint16_t submission_queue_head_pointer;
	uint16_t submission_queue_identifier;
	uint16_t command_identifier;
	uint16_t phase_bit : 1;
	uint16_t status : 15;
} packed;

struct nvme_id_power_state {
	uint16_t max_power;
	uint8_t rsvd2;
	uint8_t flags;
	uint32_t entry_lat;
	uint32_t exit_lat;
	uint8_t read_tput;
	uint8_t read_lat;
	uint8_t write_tput;
	uint8_t write_lat;
	uint16_t idle_power;
	uint8_t idle_scale;
	uint8_t rsvd19;
	uint16_t active_power;
	uint8_t active_work_scale;
	uint8_t rsvd23[9];
} packed;

struct nvme_identify_info {
	uint16_t vid;
	uint16_t ssvid;
	char sn[20];
	char mn[40];
	char fr[8];
	uint8_t rab;
	uint8_t ieee[3];
	uint8_t mic;
	uint8_t mdts;
	uint16_t cntlid;
	uint32_t ver;
	uint8_t rsvd84[172];
	uint16_t oacs;
	uint8_t acl;
	uint8_t aerl;
	uint8_t frmw;
	uint8_t lpa;
	uint8_t elpe;
	uint8_t npss;
	uint8_t avscc;
	uint8_t apsta;
	uint16_t wctemp;
	uint16_t cctemp;
	uint8_t rsvd270[242];
	uint8_t sqes;
	uint8_t cqes;
	uint8_t rsvd514[2];
	uint32_t nn;
	uint16_t oncs;
	uint16_t fuses;
	uint8_t fna;
	uint8_t vwc;
	uint16_t awun;
	uint16_t awupf;
	uint8_t nvscc;
	uint8_t rsvd531;
	uint16_t acwu;
	uint8_t rsvd534[2];
	uint32_t sgls;
	uint8_t rsvd540[1508];
	struct nvme_id_power_state psd[32];
	uint8_t vs[1024];
} packed;

struct nvme_drive {
	volatile uint32_t *bar0;
	union nvme_sq_entry *admin_submission_queue;
	volatile struct nvme_cq_entry *admin_completition_queue;
	union nvme_sq_entry *submission_queue;
	volatile struct nvme_cq_entry *completition_queue;
	volatile uint16_t *admin_submission_doorbell;
	volatile uint16_t *admin_completition_doorbell;
	volatile uint16_t *submission_doorbell;
	volatile uint16_t *completition_doorbell;
	struct nvme_identify_info *identify_info;
	size_t admin_submission_queue_head;
	size_t admin_completition_queue_head;
	size_t submission_queue_head;
	size_t completition_queue_head;
	struct nvme_drive_namespace *namespaces;
	uint64_t *prps;
	struct mutex mutex;
	bool admin_phase_bit;
	bool phase_bit;
};

enum {
	NVME_NS_FEAT_THIN = 1 << 0,
	NVME_NS_FLBAS_LBA_MASK = 0xf,
	NVME_NS_FLBAS_META_EXT = 0x10,
	NVME_LBAF_RP_BEST = 0,
	NVME_LBAF_RP_BETTER = 1,
	NVME_LBAF_RP_GOOD = 2,
	NVME_LBAF_RP_DEGRADED = 3,
	NVME_NS_DPC_PI_LAST = 1 << 4,
	NVME_NS_DPC_PI_FIRST = 1 << 3,
	NVME_NS_DPC_PI_TYPE3 = 1 << 2,
	NVME_NS_DPC_PI_TYPE2 = 1 << 1,
	NVME_NS_DPC_PI_TYPE1 = 1 << 0,
	NVME_NS_DPS_PI_FIRST = 1 << 3,
	NVME_NS_DPS_PI_MASK = 0x7,
	NVME_NS_DPS_PI_TYPE1 = 1,
	NVME_NS_DPS_PI_TYPE2 = 2,
	NVME_NS_DPS_PI_TYPE3 = 3,
};

struct nvme_lbaf {
	uint16_t ms;
	uint8_t ds;
	uint8_t rp;
};

struct nvme_namespace_info {
	uint64_t nsze;
	uint64_t ncap;
	uint64_t nuse;
	uint8_t nsfeat;
	uint8_t nlbaf;
	uint8_t flbas;
	uint8_t mc;
	uint8_t dpc;
	uint8_t dps;
	uint8_t nmic;
	uint8_t rescap;
	uint8_t fpi;
	uint8_t rsvd33;
	uint16_t nawun;
	uint16_t nawupf;
	uint16_t nacwu;
	uint16_t nabsn;
	uint16_t nabo;
	uint16_t nabspf;
	uint16_t rsvd46;
	uint64_t nvmcap[2];
	uint8_t rsvd64[40];
	uint8_t nguid[16];
	uint8_t eui64[8];
	struct nvme_lbaf lbaf[16];
	uint8_t rsvd192[192];
	uint8_t vs[3712];
} packed;

struct nvme_drive_namespace {
	struct nvme_drive *drive;
	struct nvme_namespace_info *info;
	size_t namespace_id;
	uint64_t block_size;
	uint64_t transfer_blocks_max;
	uint64_t blocks_count;
};

uint16_t nvme_execute_admin_cmd(struct nvme_drive *drive,
                                union nvme_sq_entry command) {
	drive->admin_submission_queue[drive->admin_submission_queue_head] = command;
	drive->admin_submission_queue_head++;
	if (drive->admin_submission_queue_head == NVME_SUBMISSION_QUEUE_SIZE) {
		drive->admin_submission_queue_head = 0;
	}
	volatile struct nvme_cq_entry *completition_entry =
	    drive->admin_completition_queue + drive->admin_completition_queue_head;
	completition_entry->phase_bit = drive->admin_phase_bit;

	*(drive->admin_submission_doorbell) = drive->admin_submission_queue_head;

	while (completition_entry->phase_bit == drive->admin_phase_bit) {
		struct nvme_csts_register csts;
		csts = nvme_read_csts_register(drive->bar0);
		if (csts.cfs == 1) {
			kmsg_err("NVME Driver",
			         "Fatal error occured while executing admin command\n");
		}
		asm volatile("pause");
	}

	drive->admin_completition_queue_head++;
	if (drive->admin_completition_queue_head == NVME_COMPLETITION_QUEUE_SIZE) {
		drive->admin_completition_queue_head = 0;
		drive->admin_phase_bit = !(drive->admin_phase_bit);
	}

	*(drive->admin_completition_doorbell) =
	    drive->admin_completition_queue_head;
	return drive->admin_completition_queue->status;
}

uint16_t nvme_execute_io_cmd(struct nvme_drive *drive,
                             union nvme_sq_entry command) {
	drive->submission_queue[drive->submission_queue_head] = command;
	drive->submission_queue_head++;
	if (drive->submission_queue_head == NVME_SUBMISSION_QUEUE_SIZE) {
		drive->submission_queue_head = 0;
	}
	volatile struct nvme_cq_entry *completition_entry =
	    drive->completition_queue + drive->completition_queue_head;
	completition_entry->phase_bit = drive->phase_bit;

	*(drive->submission_doorbell) = drive->submission_queue_head;

	while (completition_entry->phase_bit == drive->phase_bit) {
		struct nvme_csts_register csts;
		csts = nvme_read_csts_register(drive->bar0);
		if (csts.cfs == 1) {
			kmsg_err("NVME Driver",
			         "Fatal error occured while executing admin command\n");
		}
		asm volatile("pause");
	}

	drive->completition_queue_head++;
	if (drive->completition_queue_head == NVME_COMPLETITION_QUEUE_SIZE) {
		drive->completition_queue_head = 0;
		drive->phase_bit = !(drive->phase_bit);
	}

	*(drive->completition_doorbell) = drive->completition_queue_head;
	return drive->completition_queue->status;
}

bool nvme_rw_lba(struct nvme_drive *drive, size_t ns, void *buf, uint64_t lba,
                 size_t count, bool write) {
	mutex_lock(&(drive->mutex));
	struct nvme_drive_namespace *namespace = drive->namespaces + (ns - 1);
	size_t block_size = namespace->block_size;
	uint32_t page_offset = (uint32_t)buf & (PAGE_SIZE - 1);

	unused union nvme_sq_entry rw_command;
	rw_command.rw.opcode = write ? NVME_CMD_WRITE : NVME_CMD_READ;
	rw_command.rw.flags = 0;
	rw_command.rw.control = 0;
	rw_command.rw.dsmgmt = 0;
	rw_command.rw.reftag = 0;
	rw_command.rw.apptag = 0;
	rw_command.rw.appmask = 0;
	rw_command.rw.metadata = 0;
	rw_command.rw.slba = lba;
	rw_command.rw.nsid = ns;
	rw_command.rw.length = count - 1;
	uint32_t aligned_down = ALIGN_DOWN((uint32_t)buf, PAGE_SIZE);
	uint32_t aligned_up =
	    ALIGN_UP((uint32_t)(buf + count * block_size), PAGE_SIZE);
	size_t prp_entries_count = ((aligned_up - aligned_down) / PAGE_SIZE) - 1;

	if (prp_entries_count == 0) {
		rw_command.rw.prp1 = (uint64_t)((uint32_t)buf - KERNEL_MAPPING_BASE);
	} else if (prp_entries_count == 1) {
		rw_command.rw.prp1 = (uint64_t)((uint32_t)buf - KERNEL_MAPPING_BASE);
		rw_command.rw.prp2 = (uint64_t)((uint32_t)buf - KERNEL_MAPPING_BASE -
		                                page_offset + PAGE_SIZE);
	} else {
		rw_command.rw.prp1 = (uint64_t)((uint32_t)buf - KERNEL_MAPPING_BASE);
		rw_command.rw.prp2 =
		    (uint64_t)((uint32_t)(drive->prps) - KERNEL_MAPPING_BASE);
		for (size_t i = 0; i < prp_entries_count; ++i) {
			drive->prps[i] =
			    (uint64_t)((uint32_t)buf - KERNEL_MAPPING_BASE - page_offset +
			               PAGE_SIZE + i * PAGE_SIZE);
			printf("%p\n", drive->prps[i]);
		}
	}

	uint16_t status = nvme_execute_io_cmd(drive, rw_command);
	mutex_unlock(&(drive->mutex));
	return status == 0;
}

void nvme_init(struct pci_address addr) {
	struct nvme_drive *nvme_drive_info = ALLOC_OBJ(struct nvme_drive);
	if (nvme_drive_info == NULL) {
		kmsg_err("NVME Driver", "Failed to allocate NVME drive object");
	}
	nvme_drive_info->admin_completition_queue_head = 0;
	nvme_drive_info->admin_submission_queue_head = 0;
	nvme_drive_info->completition_queue_head = 0;
	nvme_drive_info->submission_queue_head = 0;
	mutex_init(&(nvme_drive_info->mutex));

	struct pci_bar bar;
	if (!pci_read_bar(addr, 0, &bar)) {
		kmsg_err("NVME Driver", "Failed to read BAR0");
	}
	if (bar.is_mmio == false) {
		kmsg_err("NVME Driver", "BAR0 doesn't support MMIO");
	}
	printf("         NVME BAR0 MMIO base at 0x%p\n", bar.address);
	pci_enable_bus_mastering(addr);

	uint32_t mapping_paddr = ALIGN_DOWN(bar.address, PAGE_SIZE);
	uint32_t mapping_size =
	    ALIGN_UP(bar.size + (bar.address - mapping_paddr), PAGE_SIZE);
	uint32_t mapping =
	    virt_get_io_mapping(mapping_paddr, mapping_size, bar.disable_cache);
	if (mapping == 0) {
		kmsg_err("NVME Driver", "Failed to map BAR0");
	}
	volatile uint32_t *bar0 =
	    (volatile uint32_t *)(mapping + (bar.address - mapping_paddr));
	nvme_drive_info->bar0 = bar0;

	struct nvme_cc_register cc;
	struct nvme_csts_register csts;
	struct nvme_cap_register cap;
	struct nvme_aqa_register aqa;
	struct nvme_asq_register asq;
	struct nvme_acq_register acq;

	cc = nvme_read_cc_register(bar0);
	cc.en = 0;
	nvme_write_cc_register(bar0, cc);
	csts = nvme_read_csts_register(bar0);
	while (csts.rdy != 0) {
		asm volatile("pause");
		csts = nvme_read_csts_register(bar0);
	}
	kmsg_log("NVME Driver", "NVME Controller disabled");

	cap = nvme_read_cap_register(bar0);
	if (!cap.css_is_nvm_supported) {
		kmsg_err("NVME Driver",
		         "NVME controller doesn't support NVM command set");
	}
	if (cap.mpsmin > 0) {
		kmsg_err("NVME Driver",
		         "NVME controller doesn't support host page size");
	}

	size_t submission_queue_size = ALIGN_UP(
	    sizeof(union nvme_sq_entry) * NVME_SUBMISSION_QUEUE_SIZE, PAGE_SIZE);
	union nvme_sq_entry *admin_submission_queue =
	    (union nvme_sq_entry *)heap_alloc(submission_queue_size);
	union nvme_sq_entry *submission_queue =
	    (union nvme_sq_entry *)heap_alloc(submission_queue_size);

	size_t completition_queue_size = ALIGN_UP(
	    sizeof(struct nvme_cq_entry) * NVME_COMPLETITION_QUEUE_SIZE, PAGE_SIZE);
	struct nvme_cq_entry *admin_completition_queue =
	    (struct nvme_cq_entry *)heap_alloc(completition_queue_size);
	struct nvme_cq_entry *completition_queue =
	    (struct nvme_cq_entry *)heap_alloc(completition_queue_size);

	if (admin_submission_queue == NULL || admin_completition_queue == NULL ||
	    submission_queue == NULL || completition_queue == NULL) {
		kmsg_err("NVME Driver",
		         "Failed to allocate queues for NVME controller");
	}
	nvme_drive_info->admin_submission_queue = admin_submission_queue;
	nvme_drive_info->admin_completition_queue = admin_completition_queue;
	nvme_drive_info->submission_queue = submission_queue;
	nvme_drive_info->completition_queue = completition_queue;
	nvme_drive_info->admin_phase_bit = false;
	nvme_drive_info->phase_bit = false;
	memset(admin_submission_queue, 0, submission_queue_size);
	memset(admin_completition_queue, 0, completition_queue_size);
	memset(submission_queue, 0, submission_queue_size);
	memset(completition_queue, 0, completition_queue_size);
	kmsg_log("NVME Driver", "NVME controller admin queues allocated");

	uint32_t admin_submission_queue_physical =
	    (uint32_t)admin_submission_queue - KERNEL_MAPPING_BASE;
	uint32_t admin_completition_queue_physical =
	    (uint32_t)admin_completition_queue - KERNEL_MAPPING_BASE;
	unused uint32_t submission_queue_physical =
	    (uint32_t)submission_queue - KERNEL_MAPPING_BASE;
	uint32_t completition_queue_physical =
	    (uint32_t)completition_queue - KERNEL_MAPPING_BASE;

	asq = nvme_read_asq_register(bar0);
	acq = nvme_read_acq_register(bar0);
	asq.page = admin_submission_queue_physical / PAGE_SIZE;
	acq.page = admin_completition_queue_physical / PAGE_SIZE;
	nvme_write_asq_register(bar0, asq);
	nvme_write_acq_register(bar0, acq);

	aqa = nvme_read_aqa_register(bar0);
	aqa.acqs = NVME_COMPLETITION_QUEUE_SIZE - 1;
	aqa.asqs = NVME_SUBMISSION_QUEUE_SIZE - 1;
	nvme_write_aqa_register(bar0, aqa);

	cc = nvme_read_cc_register(bar0);
	cc.ams = 0;
	cc.mps = 0;
	cc.css = 0;
	cc.shn = 0;
	cc.iocqes = 4;
	cc.iosqes = 6;
	nvme_write_cc_register(bar0, cc);

	cc = nvme_read_cc_register(bar0);
	cc.en = 1;
	nvme_write_cc_register(bar0, cc);

	csts = nvme_read_csts_register(bar0);
	while (csts.rdy != 1) {
		if (csts.cfs != 0) {
			kmsg_err("NVME Driver", "Error while enabling controller");
		}
		asm volatile("pause");
		csts = nvme_read_csts_register(bar0);
	}

	kmsg_log("NVME Driver", "NVME Controller reenabled");
	uint32_t doorbell_size = 1 << (2 + cap.dstrd);
	uint32_t doorbells_base = (uint32_t)bar0 + 0x1000;
	nvme_drive_info->admin_submission_doorbell =
	    (volatile uint16_t *)(doorbells_base + doorbell_size * 0);
	nvme_drive_info->admin_completition_doorbell =
	    (volatile uint16_t *)(doorbells_base + doorbell_size * 1);
	nvme_drive_info->submission_doorbell =
	    (volatile uint16_t *)(doorbells_base + doorbell_size * 2);
	nvme_drive_info->completition_doorbell =
	    (volatile uint16_t *)(doorbells_base + doorbell_size * 3);

	union nvme_sq_entry identify_command = {0};
	identify_command.identify.opcode = NVME_IDENTIFY;
	identify_command.identify.nsid = 0;
	identify_command.identify.cns = 1;
	struct nvme_identify_info *identify_info =
	    ALLOC_OBJ(struct nvme_identify_info);
	if (identify_info == NULL) {
		kmsg_err("NVME Driver", "Failed to allocate identify info object");
	}
	identify_command.identify.prp1 =
	    ((uint32_t)identify_info - KERNEL_MAPPING_BASE);
	identify_command.identify.prp2 = 0;
	if (nvme_execute_admin_cmd(nvme_drive_info, identify_command) != 0) {
		kmsg_err("NVME Driver", "Failed to execute identify command");
	}
	nvme_drive_info->identify_info = (struct nvme_identify_info *)identify_info;
	kmsg_log("NVME Driver", "Identify command executed successfully");

	union nvme_sq_entry create_queue_command = {0};

	create_queue_command.create_cq.opcode = NVME_CREATE_COMPLETITION_QUEUE;
	create_queue_command.create_cq.prp1 = completition_queue_physical;
	create_queue_command.create_cq.cqid = 1;
	create_queue_command.create_cq.qsize = NVME_COMPLETITION_QUEUE_SIZE - 1;
	create_queue_command.create_cq.cq_flags = NVME_QUEUE_PHYS_CONTIG;
	create_queue_command.create_cq.irq_vector = 0;
	if (nvme_execute_admin_cmd(nvme_drive_info, create_queue_command) != 0) {
		kmsg_err("NVME Driver", "Failed to create completition queue");
	}
	kmsg_log("NVME Driver", "Created completition queue");

	create_queue_command.create_sq.opcode = NVME_CREATE_SUBMISSION_QUEUE;
	create_queue_command.create_sq.prp1 = submission_queue_physical;
	create_queue_command.create_sq.cqid = 1;
	create_queue_command.create_sq.sqid = 1;
	create_queue_command.create_sq.qsize = NVME_SUBMISSION_QUEUE_SIZE - 1;
	create_queue_command.create_sq.sq_flags = NVME_QUEUE_PHYS_CONTIG;
	if (nvme_execute_admin_cmd(nvme_drive_info, create_queue_command) != 0) {
		kmsg_err("NVME Driver", "Failed to create submission queue");
	}
	kmsg_log("NVME Driver", "Created submission queue");

	uint32_t namespaces_count = identify_info->nn;
	printf("         NVME Drive namespaces count: %u\n", identify_info->nn);
	struct nvme_drive_namespace *namespaces =
	    (struct nvme_drive_namespace *)heap_alloc(
	        sizeof(struct nvme_drive_namespace) * namespaces_count);
	if (namespaces == NULL) {
		kmsg_log("NVME Driver", "Failed to allocate namespaces objects");
	}
	nvme_drive_info->namespaces = namespaces;

	size_t max_size = 1 << (12 + nvme_drive_info->identify_info->mdts);
	if (nvme_drive_info->identify_info->mdts > NVME_MAX_MDTS ||
	    max_size == 4096) {
		max_size = 1 << (12 + NVME_MAX_MDTS);
	}
	printf("         Max transfer size: %u\n", max_size);

	for (size_t i = 0; i < namespaces_count; ++i) {
		struct nvme_drive_namespace *namespace = namespaces + i;
		namespace->namespace_id = i + 1;
		namespace->drive = nvme_drive_info;
		namespace->info = ALLOC_OBJ(struct nvme_namespace_info);
		if (namespace->info == NULL) {
			kmsg_log("NVME Driver", "Failed to allocate namespace info object");
		}

		union nvme_sq_entry get_namespace_info_command = {0};
		get_namespace_info_command.identify.opcode = NVME_IDENTIFY;
		get_namespace_info_command.identify.nsid = i + 1;
		get_namespace_info_command.identify.cns = 0;
		get_namespace_info_command.identify.prp1 =
		    (uint32_t) namespace->info - KERNEL_MAPPING_BASE;
		if (nvme_execute_admin_cmd(nvme_drive_info,
		                           get_namespace_info_command) != 0) {
			kmsg_err("NVME Driver", "Failed to read namespace info");
		}
		kmsg_log("NVME Driver", "Successfully read NVME namespace info");

		uint8_t format_used = namespace->info->flbas & 0b11;
		size_t block_size = 1 << namespace->info->lbaf[format_used].ds;
		size_t transfer_blocks_max = max_size / block_size;
		namespace->block_size = block_size;
		namespace->transfer_blocks_max = transfer_blocks_max;
		namespace->blocks_count = namespace->info->nsze;
		printf("         Drive namespace %u initialized. Namespace size: %u "
		       "blocks\n",
		       i + 1, namespace->info->nsze);
	}

	uint64_t *prps = heap_alloc(max_size / PAGE_SIZE * 8);
	if (prps == NULL) {
		kmsg_err("NVME Driver", "Failed to allocate PRP list");
	}
	nvme_drive_info->prps = prps;

	char *buf = heap_alloc(59 * 512);
	memset(buf, 0, 30000);
	printf("%p\n", buf);
	nvme_rw_lba(nvme_drive_info, 1, buf, 0, 59, false);
	printf("%s\n", buf);
	size_t len = strlen(buf);
	for (size_t i = 0; i < len; ++i) {
		buf[i] = 255 - buf[i];
	}
	nvme_rw_lba(nvme_drive_info, 1, buf, 0, 59, true);
}