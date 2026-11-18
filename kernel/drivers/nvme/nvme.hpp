#ifndef NVME_HPP
#define NVME_HPP

#include <stdint.h>

namespace nvme {

constexpr uint32_t kPageSize    = 4096;
constexpr uint16_t kAdminQueueDepth = 8;
constexpr uint16_t kIoQueueDepth    = 128;
constexpr uint16_t kIoQueueId       = 1;

enum Reg : uint32_t {
    REG_CAP    = 0x00,
    REG_VS     = 0x08,
    REG_INTMS  = 0x0C,
    REG_INTMC  = 0x10,
    REG_CC     = 0x14,
    REG_CSTS   = 0x1C,
    REG_AQA    = 0x24,
    REG_ASQ    = 0x28,
    REG_ACQ    = 0x30,
};

enum CCBits : uint32_t {
    CC_EN         = 1u << 0,
    CC_CSS_NVM    = 0u << 4,
    CC_MPS_4K     = 0u << 7,
    CC_AMS_RR     = 0u << 11,
    CC_SHN_NONE   = 0u << 14,
    CC_IOSQES_64  = 6u << 16,
    CC_IOCQES_16  = 4u << 20,
};

enum CSTSBits : uint32_t {
    CSTS_RDY  = 1u << 0,
    CSTS_CFS  = 1u << 1,
};

enum AdminOp : uint8_t {
    ADMIN_DELETE_SQ = 0x00,
    ADMIN_CREATE_SQ = 0x01,
    ADMIN_GET_LOG   = 0x02,
    ADMIN_DELETE_CQ = 0x04,
    ADMIN_CREATE_CQ = 0x05,
    ADMIN_IDENTIFY  = 0x06,
};

enum IoOp : uint8_t {
    IO_FLUSH = 0x00,
    IO_WRITE = 0x01,
    IO_READ  = 0x02,
};

enum IdentifyCns : uint32_t {
    CNS_NS    = 0x00,
    CNS_CTRL  = 0x01,
};

struct Cmd {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t command_id;
    uint32_t nsid;
    uint32_t cdw2;
    uint32_t cdw3;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

struct Cqe {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} __attribute__((packed));

class Controller {
public:
    Controller() = default;
    ~Controller();

    int init();

    int read (uint64_t lba, uint32_t count, void* buf);
    int write(uint64_t lba, uint32_t count, const void* buf);
    int flush();

    uint32_t    sector_size()  const { return sector_size_; }
    uint64_t    sector_count() const { return sector_count_; }
    uint32_t    vendor_id()    const { return vendor_id_; }
    const char* vendor_name()  const { return vendor_name_; }

private:
    uint8_t  bus_ = 0, dev_ = 0, func_ = 0;
    uint8_t  irq_ = 0;
    uint32_t vendor_id_ = 0;
    const char* vendor_name_ = "unknown";
    uint64_t bar0_phys_ = 0;
    uint64_t bar0_size_ = 0;
    volatile uint8_t* mmio_ = nullptr;

    uint64_t admin_sq_phys_ = 0;
    uint64_t admin_cq_phys_ = 0;
    uint16_t admin_sq_head_ = 0, admin_sq_tail_ = 0;
    uint16_t admin_cq_head_ = 0;
    uint16_t admin_cq_phase_ = 1;

    uint64_t io_sq_phys_ = 0;
    uint64_t io_cq_phys_ = 0;
    uint16_t io_sq_head_ = 0, io_sq_tail_ = 0;
    uint16_t io_cq_head_ = 0;
    uint16_t io_cq_phase_ = 1;

    uint32_t sector_size_ = 0;
    uint64_t sector_count_ = 0;
    uint16_t queue_depth_ = 0;

    uint32_t rd32(uint32_t off);
    uint64_t rd64(uint32_t off);
    void     wr32(uint32_t off, uint32_t v);
    void     wr64(uint32_t off, uint64_t v);

    int  reset();
    int  setup_admin_queue();
    int  enable();
    int  identify(uint32_t nsid, uint32_t cns, uint64_t buf_phys);
    int  create_io_cq();
    int  create_io_sq();
    int  admin_cmd(Cmd& cmd);
    int  io_cmd(Cmd& cmd);

    void ring_sq_tail(uint16_t qid, uint16_t tail);
    void ring_cq_head(uint16_t qid, uint16_t head);
};

}
#endif