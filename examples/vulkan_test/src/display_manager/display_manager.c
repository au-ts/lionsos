#include <microkit.h>
#include <stdint.h>
#include <sddf/util/printf.h>
#include <sddf/util/util.h>
#include <sddf/virtio/virtio.h>
#include <sddf/virtio/virtio_queue.h>
#include <sddf/gpu/queue.h>
#include <gpu_config.h>
#include <gpu.h>
#include <sddf/util/ialloc.h>


/* Uncomment this to enable debug logging */
// #define DEBUG_GPU_VIRTIO_DRIVER

#if defined(DEBUG_GPU_VIRTIO_DRIVER)
#define LOG_GPU_VIRTIO_DRIVER(...) do{ sddf_dprintf("GPU_VIRTIO_DRIVER|INFO: "); sddf_dprintf(__VA_ARGS__); }while(0)
#else
#define LOG_GPU_VIRTIO_DRIVER(...) do{}while(0)
#endif
#define LOG_GPU_VIRTIO_DRIVER_ERR(...) do{ sddf_dprintf("GPU_VIRTIO_DRIVER|ERROR: "); sddf_dprintf(__VA_ARGS__); }while(0)

#define IRQ_CH 0
#define VIRT_CH 1
#define VMM_CHANNEL 1

//TODO(LEO): maybe dont alloate specific address and just let QEMU default?
#define VIRTIO_MMIO_GPU_OFFSET              0xa001000

/*
 * This offset is the default for QEMU, but can change depending on
 * the configuration of QEMU and what other virtIO devices are being
 * used.
 */
#ifndef VIRTIO_MMIO_GPU_OFFSET
#define VIRTIO_MMIO_GPU_OFFSET (0xe00)
#endif

#define VIRTQ_QUEUE_SIZE GPU_QUEUE_CAPACITY_DRV

/* Size of data contained in a single descriptor */
#define VIRTIO_DATA_ENTRY_SIZE 2048
_Static_assert((uint64_t)VIRTIO_DATA_ENTRY_SIZE *(uint64_t)VIRTQ_QUEUE_SIZE <= GPU_VIRTIO_DATA_REGION_SIZE,
               "VIRTIO_DATA_ENTRY_SIZE * VIRTQ_QUEUE_SIZE must be less than or "
               "equal to GPU_VIRTIO_DATA_REGION_SIZE");

#define VIRTIO_DATA(idx) ((idx) * VIRTIO_DATA_ENTRY_SIZE + virtio_data)
#define VIRTIO_DATA_PADDR(idx) ((idx) * VIRTIO_DATA_ENTRY_SIZE + virtio_data_paddr)

#define VIRTIO_DATA_PADDR_TO_VADDR(paddr) ((uintptr_t)(paddr) + virtio_data - virtio_data_paddr)

#define VIRTIO_MAX_DESC_PER_REQ 3

#define BIT_LOW(n)  (1ul<<(n))
#define BIT_HIGH(n) (1ul<<(n - 32 ))

/* Microkit patched variables */
uintptr_t virtio_metadata;
uintptr_t virtio_metadata_paddr;
uintptr_t virtio_data;
uintptr_t virtio_data_paddr;

uintptr_t gpu_driver_data;


static volatile virtio_mmio_regs_t *regs;
static volatile struct virtio_gpu_config
    *virtio_config; /* gpu device configuration, populated by device during initialisation. */
static struct virtq virtq;
// static gpu_queue_handle_t gpu_queue_h;


/* Store information between a request/response pair */
typedef struct reqbk {
    /* Store virtio request code so that when device responds, we can check whether
     * it has written into our virtIO header descriptor, which is marked as read-only.
     */
    enum virtio_gpu_ctrl_type virtio_code;
    /* Offset into sddf data memory region. Currently only DISPLAY_INFO requests
     * imply data written in the data region, which would need to be stored.
     */
    uint64_t mem_offset;
} reqbk_t;
static reqbk_t reqsbk[GPU_QUEUE_CAPACITY_DRV];

static ialloc_t ialloc_desc;

/* Mapping from virtIO descriptor idx to sDDF id */
static uint32_t virtio_desc_to_id[VIRTQ_QUEUE_SIZE];

static uint16_t last_handled_used_idx = 0;

static void virtio_gpu_init();
static void handle_irq();
static void handle_request();

void init(void) {
    regs = (volatile virtio_mmio_regs_t *) VIRTIO_MMIO_GPU_OFFSET;
    virtio_config = (volatile struct virtio_gpu_config *)regs->Config;
    sddf_printf("----------------------------------------\n");
    sddf_printf("DISPLAY MANAGER: Booted successfully!\n");
    virtio_gpu_init();
}

void notified(microkit_channel ch) {
    if (ch == VMM_CHANNEL) {
        sddf_printf("DISPLAY MANAGER: Received a ping from the VMM!\n");
    }
}

static void virtio_gpu_init(void)
{
    /* Do MMIO device init (section 4.2.3.1) */
    if (!virtio_mmio_check_magic(regs)) {
        LOG_GPU_VIRTIO_DRIVER_ERR("Invalid virtIO magic value!\n");
        assert(false);
    }

    if (virtio_mmio_version(regs) != VIRTIO_VERSION) {
        LOG_GPU_VIRTIO_DRIVER_ERR("Incorrect virtIO version!\n");
        assert(false);
    }

    if (!virtio_mmio_check_device_id(regs, VIRTIO_DEVICE_ID_GPU)) {
        LOG_GPU_VIRTIO_DRIVER_ERR("Not a virtIO gpu device!\n");
        assert(false);
    }

    if (virtio_mmio_version(regs) != VIRTIO_GPU_DRIVER_VERSION) {
        LOG_GPU_VIRTIO_DRIVER_ERR("Driver does not support given virtIO version: 0x%x\n", virtio_mmio_version(regs));
        LOG_GPU_VIRTIO_DRIVER_ERR("Driver supports virtIO version: 0x%x\n", VIRTIO_GPU_DRIVER_VERSION);
        assert(false);
    }

    /* First reset the device */
    regs->Status = 0;
    /* Set the ACKNOWLEDGE bit to say we have noticed the device */
    regs->Status = VIRTIO_DEVICE_STATUS_ACKNOWLEDGE;
    /* Set the DRIVER bit to say we know how to drive the device */
    regs->Status = VIRTIO_DEVICE_STATUS_DRIVER;

    /* Now we can read configuration space to validate its fields */
    if (virtio_config->num_scanouts == 0) {
        LOG_GPU_VIRTIO_DRIVER_ERR("No scanouts available!\n");
        assert(false);
    }

    uint32_t dev_features_low = regs->DeviceFeatures;
    (void)(dev_features_low); /* Silence unused warnings */
    regs->DeviceFeaturesSel = 1;
    uint32_t dev_features_high = regs->DeviceFeatures;

#ifdef DEBUG_GPU_VIRTIO_DRIVER
    LOG_GPU_VIRTIO_DRIVER("Device is offering the following features:\n");
    uint64_t dev_features = dev_features_low | ((uint64_t)dev_features_high << 32);
    virtio_print_reserved_feature_bits(dev_features);
    virtio_gpu_print_features(dev_features);
#endif

    /* Select features we want from the device.
     * We require blob resources for zero copy if enabled, and virtIO version 1
     * as we are following the non-legacy virtIO 1.2 specification.
     */
#ifdef GPU_BLOB_SUPPORT
    if (!(dev_features_low & BIT_LOW(VIRTIO_GPU_F_RESOURCE_BLOB))) {
        LOG_GPU_VIRTIO_DRIVER_ERR("Device does not support blob resources!\n");
        assert(false);
    }
#endif
    if (!(dev_features_high & BIT_HIGH(VIRTIO_F_VERSION_1))) {
        LOG_GPU_VIRTIO_DRIVER_ERR("Device does not support virtIO version 1!\n");
        assert(false);
    }
#ifdef GPU_BLOB_SUPPORT
    uint32_t drv_features_low = BIT_LOW(VIRTIO_GPU_F_RESOURCE_BLOB);
#else
    uint32_t drv_features_low = 0;
#endif
    uint32_t drv_features_high = BIT_HIGH(VIRTIO_F_VERSION_1);
    regs->DriverFeatures = drv_features_low;
    regs->DriverFeaturesSel = 1;
    regs->DriverFeatures = drv_features_high;

#ifdef DEBUG_GPU_VIRTIO_DRIVER
    uint64_t drv_features = drv_features_low | ((uint64_t)drv_features_high << 32);
    LOG_GPU_VIRTIO_DRIVER("Driver is selecting the following features:\n");
    virtio_print_reserved_feature_bits(drv_features);
    virtio_gpu_print_features(drv_features);
#endif

    regs->Status |= VIRTIO_DEVICE_STATUS_FEATURES_OK;
    if (!(regs->Status & VIRTIO_DEVICE_STATUS_FEATURES_OK)) {
        LOG_GPU_VIRTIO_DRIVER_ERR("Device status features is not OK!\n");
        assert(false);
    }

    /* Add virtqueues */
    size_t desc_off = 0;
    size_t avail_off = ALIGN(desc_off + (16 * VIRTQ_QUEUE_SIZE), 2);
    size_t used_off = ALIGN(avail_off + (6 + 2 * VIRTQ_QUEUE_SIZE), 4);
    size_t size = used_off + (6 + 8 * VIRTQ_QUEUE_SIZE);
    assert(size <= GPU_VIRTIO_METADATA_REGION_SIZE);

    virtq.num = VIRTQ_QUEUE_SIZE;
    virtq.desc = (struct virtq_desc *)(virtio_metadata + desc_off);
    virtq.avail = (struct virtq_avail *)(virtio_metadata + avail_off);
    virtq.used = (struct virtq_used *)(virtio_metadata + used_off);

    assert(regs->QueueNumMax >= VIRTQ_QUEUE_SIZE);
    regs->QueueSel = VIRTIO_GPU_CONTROL_QUEUE;
    regs->QueueNum = VIRTQ_QUEUE_SIZE;
    regs->QueueDescLow = (virtio_metadata_paddr + desc_off) & 0xFFFFFFFFUL;
    regs->QueueDescHigh = (virtio_metadata_paddr + desc_off) >> 32;
    regs->QueueDriverLow = (virtio_metadata_paddr + avail_off) & 0xFFFFFFFFUL;
    regs->QueueDriverHigh = (virtio_metadata_paddr + avail_off) >> 32;
    regs->QueueDeviceLow = (virtio_metadata_paddr + used_off) & 0xFFFFFFFFUL;
    regs->QueueDeviceHigh = (virtio_metadata_paddr + used_off) >> 32;
    regs->QueueReady = 1;

    /* Finish initialisation */
    regs->Status |= VIRTIO_DEVICE_STATUS_DRIVER_OK;
}

static gpu_resp_status_t virtio_gpu_to_sddf_resp_status(enum virtio_gpu_ctrl_type type)
{
    switch (type) {
    case VIRTIO_GPU_RESP_OK_DISPLAY_INFO:
    /* FALLTHROUGH */
    case VIRTIO_GPU_RESP_OK_NODATA:
        return GPU_RESP_OK;
    case VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID:
        return GPU_RESP_ERR_INVALID_RESOURCE_ID;
    case VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID:
        return GPU_RESP_ERR_INVALID_SCANOUT_ID;
    case VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER:
        return GPU_RESP_ERR_INVALID_PARAMETER;
    case VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY:
    /* FALLTHROUGH */
    case VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID:
    /* FALLTHROUGH */
    case VIRTIO_GPU_RESP_ERR_UNSPEC:
        return GPU_RESP_ERR_UNSPEC;
    default:
        LOG_GPU_VIRTIO_DRIVER_ERR("Unexpected virtIO response type received from device: 0x%x\n", type);
        assert(false);
        return GPU_RESP_ERR_UNSPEC;
    }
}

static bool handle_response()
{
    int err = 0;
    bool sddf_notify = false;
    uint16_t i = last_handled_used_idx;
    uint16_t curr_idx = virtq.used->idx;
    while (i != curr_idx) {
        struct virtq_used_elem used = virtq.used->ring[i % virtq.num];
        assert(used.id < VIRTQ_QUEUE_SIZE);

        LOG_GPU_VIRTIO_DRIVER("Handling response %d\n", virtio_desc_to_id[used.id]);

        struct virtq_desc desc_head = virtq.desc[used.id];
        assert(desc_head.len >= sizeof(struct virtio_gpu_ctrl_hdr));
        assert(desc_head.flags & VIRTQ_DESC_F_NEXT);
        assert(desc_head.next < VIRTQ_QUEUE_SIZE);

        gpu_resp_t resp = { 0 };
        resp.id = virtio_desc_to_id[used.id];
        resp.status = GPU_RESP_ERR_UNSPEC;

        struct virtio_gpu_ctrl_hdr *req_hdr = (struct virtio_gpu_ctrl_hdr *)VIRTIO_DATA_PADDR_TO_VADDR(desc_head.addr);
        assert(req_hdr->type == reqsbk[resp.id].virtio_code);
        switch (req_hdr->type) {
        case VIRTIO_GPU_CMD_GET_DISPLAY_INFO: {
            struct virtq_desc desc_footer = virtq.desc[desc_head.next];
            assert(desc_footer.len >= sizeof(struct virtio_gpu_resp_display_info));
            assert(desc_footer.flags & VIRTQ_DESC_F_WRITE);
            struct virtio_gpu_resp_display_info *resp_display_info =
                (struct virtio_gpu_resp_display_info *)VIRTIO_DATA_PADDR_TO_VADDR(desc_footer.addr);
            assert(resp_display_info->hdr.fence_id == req_hdr->fence_id);
            resp.status = virtio_gpu_to_sddf_resp_status(resp_display_info->hdr.type);

            struct gpu_resp_get_display_info *resp_display_info_sddf =
                (struct gpu_resp_get_display_info *)(reqsbk[resp.id].mem_offset + gpu_driver_data);
            int num_scanouts = (virtio_config->num_scanouts < GPU_MAX_SCANOUTS) ? virtio_config->num_scanouts
                                                                                : GPU_MAX_SCANOUTS;
            for (int i = 0; i < num_scanouts; i++) {
                resp_display_info_sddf->scanouts[i].rect.x = resp_display_info->pmodes[i].r.x;
                resp_display_info_sddf->scanouts[i].rect.y = resp_display_info->pmodes[i].r.y;
                resp_display_info_sddf->scanouts[i].rect.width = resp_display_info->pmodes[i].r.width;
                resp_display_info_sddf->scanouts[i].rect.height = resp_display_info->pmodes[i].r.height;
                resp_display_info_sddf->scanouts[i].enabled = resp_display_info->pmodes[i].enabled;
            }
            resp_display_info_sddf->num_scanouts = num_scanouts;
            break;
        }
#ifdef GPU_BLOB_SUPPORT
        case VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB: {
            struct virtq_desc desc_body = virtq.desc[desc_head.next];
            assert(desc_body.flags & VIRTQ_DESC_F_NEXT);
            assert(desc_body.len >= sizeof(struct virtio_gpu_mem_entry));

            struct virtq_desc desc_footer = virtq.desc[desc_body.next];
            assert(desc_footer.len >= sizeof(struct virtio_gpu_ctrl_hdr));
            assert(desc_footer.flags & VIRTQ_DESC_F_WRITE);

            struct virtio_gpu_ctrl_hdr *resp_hdr = (struct virtio_gpu_ctrl_hdr *)VIRTIO_DATA_PADDR_TO_VADDR(
                desc_footer.addr);
            assert(resp_hdr->fence_id == req_hdr->fence_id);
            resp.status = virtio_gpu_to_sddf_resp_status(resp_hdr->type);
            assert(desc_body.flags & VIRTQ_DESC_F_NEXT);
            assert(desc_body.len >= sizeof(struct virtio_gpu_mem_entry));

            err = ialloc_free(&ialloc_desc, desc_body.next);
            assert(!err);
            break;
        }
#endif
        case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING: {
            struct virtq_desc desc_body = virtq.desc[desc_head.next];
            assert(desc_body.flags & VIRTQ_DESC_F_NEXT);
            assert(desc_body.len >= sizeof(struct virtio_gpu_mem_entry));

            struct virtq_desc desc_footer = virtq.desc[desc_body.next];
            assert(desc_footer.len >= sizeof(struct virtio_gpu_ctrl_hdr));
            assert(desc_footer.flags & VIRTQ_DESC_F_WRITE);

            struct virtio_gpu_ctrl_hdr *resp_hdr = (struct virtio_gpu_ctrl_hdr *)VIRTIO_DATA_PADDR_TO_VADDR(
                desc_footer.addr);
            assert(resp_hdr->fence_id == req_hdr->fence_id);
            resp.status = virtio_gpu_to_sddf_resp_status(resp_hdr->type);
            assert(desc_body.flags & VIRTQ_DESC_F_NEXT);
            assert(desc_body.len >= sizeof(struct virtio_gpu_mem_entry));

            err = ialloc_free(&ialloc_desc, desc_body.next);
            assert(!err);
            break;
        }
        case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
        /* FALLTHROUGH */
#ifdef GPU_BLOB_SUPPORT
        case VIRTIO_GPU_CMD_SET_SCANOUT_BLOB:
        /* FALLTHROUGH */
#endif
        case VIRTIO_GPU_CMD_RESOURCE_UNREF:
        /* FALLTHROUGH */
        case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
        /* FALLTHROUGH */
        case VIRTIO_GPU_CMD_SET_SCANOUT:
        /* FALLTHROUGH */
        case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
        /* FALLTHROUGH */
        case VIRTIO_GPU_CMD_RESOURCE_FLUSH: {
            struct virtq_desc desc_footer = virtq.desc[desc_head.next];
            assert(desc_footer.len >= sizeof(struct virtio_gpu_ctrl_hdr));
            assert(desc_footer.flags & VIRTQ_DESC_F_WRITE);
            struct virtio_gpu_ctrl_hdr *resp_hdr = (struct virtio_gpu_ctrl_hdr *)VIRTIO_DATA_PADDR_TO_VADDR(
                desc_footer.addr);
            assert(resp_hdr->fence_id == req_hdr->fence_id);
            resp.status = virtio_gpu_to_sddf_resp_status(resp_hdr->type);
            break;
        }
        default:
            /* This should never happen as we have already checked for a valid request
             * code when the request was made, and also whether the device has tampered with it.
             */
            LOG_GPU_VIRTIO_DRIVER_ERR("Unrecognised (but already sanitised) bookkept request code "
                                      "when processing response\n");
            assert(false);
            break;
        }

        err = ialloc_free(&ialloc_desc, used.id);
        assert(!err);
        err = ialloc_free(&ialloc_desc, desc_head.next);
        assert(!err);

        // if (gpu_queue_full_resp(&gpu_queue_h)) {
        //     LOG_GPU_VIRTIO_DRIVER_ERR("Response queue is full, dropping response\n");
        //     continue;
        // }

        // err = gpu_enqueue_resp(&gpu_queue_h, resp);
        // assert(!err);
        // sddf_notify = true;

        i++;
    }

    last_handled_used_idx = i;

    return sddf_notify;
}
