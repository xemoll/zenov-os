#include "tpm2_protocol.inc"

namespace paging {
constexpr uint32_t tpm_tis_base = 0xFED40000U;
constexpr uint32_t tpm_tis_pages = 5U;
constexpr uint32_t write_through = 0x008U;
constexpr uint32_t cache_disable = 0x010U;
alignas(4096) uint32_t tpm_tis_table[1024];
bool tpm_tis_mapping_ready = false;

bool map_tpm_tis() {
    constexpr uint32_t directory_index = tpm_tis_base / table_span;
    constexpr uint32_t first_page = (tpm_tis_base % table_span) / page_size;
    static_assert(directory_index < 1024U && first_page + tpm_tis_pages <= 1024U);
    if (page_directory[directory_index] != 0U &&
        (page_directory[directory_index] & ~(page_size - 1U)) !=
            reinterpret_cast<uintptr_t>(tpm_tis_table)) {
        return false;
    }
    memset(tpm_tis_table, 0, sizeof(tpm_tis_table));
    for (uint32_t page = 0U; page < tpm_tis_pages; ++page) {
        const uint32_t address = tpm_tis_base + page * page_size;
        tpm_tis_table[first_page + page] = address | present | writable | write_through | cache_disable;
    }
    page_directory[directory_index] = reinterpret_cast<uintptr_t>(tpm_tis_table) | present | writable;
    if (enabled) reload();
    tpm_tis_mapping_ready = true;
    return true;
}
} // namespace paging

namespace tpm2 {

constexpr uintptr_t tis_base = paging::tpm_tis_base;
constexpr uint32_t register_access = 0x0000U;
constexpr uint32_t register_status = 0x0018U;
constexpr uint32_t register_data_fifo = 0x0024U;
constexpr uint32_t register_did_vid = 0x0F00U;
constexpr uint32_t register_revision = 0x0F04U;
constexpr uint8_t access_valid = 0x80U;
constexpr uint8_t access_active_locality = 0x20U;
constexpr uint8_t access_request_use = 0x02U;
constexpr uint8_t status_valid = 0x80U;
constexpr uint8_t status_command_ready = 0x40U;
constexpr uint8_t status_go = 0x20U;
constexpr uint8_t status_data_available = 0x10U;
constexpr uint8_t status_expect = 0x08U;
constexpr uint32_t timeout_ticks = 300U;
constexpr uint32_t command_capacity = tpm2_protocol::max_command_bytes;
constexpr uint32_t response_capacity = tpm2_protocol::max_response_bytes;

alignas(4) uint8_t command_buffer[command_capacity];
alignas(4) uint8_t response_buffer[response_capacity];
bool transport_ready = false;
bool counter_provisioned = false;
bool counter_public_valid = false;
uint64_t counter_generation = 0U;
uint32_t did_vid = 0U;
uint8_t revision = 0U;
uint32_t last_response_code = 0U;

volatile uint8_t& mmio8(uint32_t offset) {
    return *reinterpret_cast<volatile uint8_t*>(tis_base + offset);
}
volatile uint32_t& mmio32(uint32_t offset) {
    return *reinterpret_cast<volatile uint32_t*>(tis_base + offset);
}
uint8_t read8(uint32_t offset) { return mmio8(offset); }
uint32_t read32(uint32_t offset) { return mmio32(offset); }
void write8(uint32_t offset, uint8_t value) {
    mmio8(offset) = value;
    asm volatile("" : : : "memory");
}

void serial_u32(uint32_t value) {
    char text[11];
    uint32_t count = 0U;
    do {
        text[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(text));
    while (count) serial::put(text[--count]);
}
void serial_hex32(uint32_t value) {
    constexpr char digits[] = "0123456789abcdef";
    serial::write("0x");
    for (uint32_t shift = 28U;; shift -= 4U) {
        serial::put(digits[(value >> shift) & 0xFU]);
        if (shift == 0U) break;
    }
}
void serial_u64(uint64_t value) {
    const uint32_t high = static_cast<uint32_t>(value >> 32U);
    const uint32_t low = static_cast<uint32_t>(value);
    if (high == 0U) {
        serial_u32(low);
        return;
    }
    serial_hex32(high);
    serial::put('_');
    constexpr char digits[] = "0123456789abcdef";
    for (uint32_t shift = 28U;; shift -= 4U) {
        serial::put(digits[(low >> shift) & 0xFU]);
        if (shift == 0U) break;
    }
}
void console_u64(uint64_t value) {
    const uint32_t high = static_cast<uint32_t>(value >> 32U);
    const uint32_t low = static_cast<uint32_t>(value);
    if (high == 0U) {
        console::unsigned_dec(low);
        return;
    }
    console::hex(high);
    console::put('_');
    constexpr char digits[] = "0123456789abcdef";
    for (uint32_t shift = 28U;; shift -= 4U) {
        console::put(digits[(low >> shift) & 0xFU]);
        if (shift == 0U) break;
    }
}

bool elapsed(uint32_t start, uint32_t limit) {
    return static_cast<uint32_t>(timer_ticks - start) >= limit;
}
bool wait_access(uint8_t required, uint8_t forbidden = 0U) {
    const uint32_t start = timer_ticks;
    for (;;) {
        const uint8_t value = read8(register_access);
        if ((value & required) == required && (value & forbidden) == 0U) return true;
        if (elapsed(start, timeout_ticks)) return false;
        asm volatile("pause");
    }
}
bool wait_status(uint8_t required, uint8_t forbidden = 0U) {
    const uint32_t start = timer_ticks;
    for (;;) {
        const uint8_t value = static_cast<uint8_t>(read32(register_status));
        if ((value & required) == required && (value & forbidden) == 0U) return true;
        if (elapsed(start, timeout_ticks)) return false;
        asm volatile("pause");
    }
}
uint16_t burst_count() {
    return static_cast<uint16_t>((read32(register_status) >> 8U) & 0xFFFFU);
}
bool wait_burst(uint16_t& burst) {
    const uint32_t start = timer_ticks;
    for (;;) {
        burst = burst_count();
        if (burst != 0U) return true;
        if (elapsed(start, timeout_ticks)) return false;
        asm volatile("pause");
    }
}

bool request_locality() {
    const uint8_t access = read8(register_access);
    if ((access & (access_valid | access_active_locality)) ==
        (access_valid | access_active_locality)) {
        return true;
    }
    write8(register_access, access_request_use);
    return wait_access(static_cast<uint8_t>(access_valid | access_active_locality));
}

bool command_ready_state() {
    write8(register_status, status_command_ready);
    return wait_status(status_command_ready);
}

bool write_fifo(const uint8_t* command, uint32_t command_size) {
    if (!command || command_size < tpm2_protocol::command_header_bytes || command_size > command_capacity) return false;
    uint32_t written = 0U;
    while (written < command_size) {
        uint16_t burst = 0U;
        if (!wait_burst(burst)) return false;
        uint32_t chunk = command_size - written;
        if (chunk > burst) chunk = burst;
        for (uint32_t i = 0U; i < chunk; ++i) write8(register_data_fifo, command[written++]);
        if (written < command_size && !wait_status(status_expect)) return false;
    }
    return wait_status(status_valid, status_expect);
}

bool read_fifo_bytes(uint8_t* output, uint32_t count) {
    if (!output && count) return false;
    uint32_t read = 0U;
    while (read < count) {
        uint16_t burst = 0U;
        if (!wait_burst(burst)) return false;
        uint32_t chunk = count - read;
        if (chunk > burst) chunk = burst;
        for (uint32_t i = 0U; i < chunk; ++i) output[read++] = read8(register_data_fifo);
    }
    return true;
}

bool transmit(const uint8_t* command, uint32_t command_size,
              uint8_t* response, uint32_t response_limit, uint32_t& response_size) {
    response_size = 0U;
    last_response_code = 0xFFFFFFFFU;
    if (!transport_ready || !response || response_limit < tpm2_protocol::command_header_bytes ||
        response_limit > response_capacity || !request_locality() || !command_ready_state() ||
        !write_fifo(command, command_size)) {
        return false;
    }
    write8(register_status, status_go);
    if (!wait_status(static_cast<uint8_t>(status_valid | status_data_available)) ||
        !read_fifo_bytes(response, tpm2_protocol::command_header_bytes)) {
        (void)command_ready_state();
        return false;
    }
    tpm2_protocol::ResponseHeader header{};
    const uint32_t declared_size = (static_cast<uint32_t>(response[2]) << 24U) |
                                   (static_cast<uint32_t>(response[3]) << 16U) |
                                   (static_cast<uint32_t>(response[4]) << 8U) |
                                   static_cast<uint32_t>(response[5]);
    if (declared_size < tpm2_protocol::command_header_bytes || declared_size > response_limit ||
        !read_fifo_bytes(response + tpm2_protocol::command_header_bytes,
                         declared_size - tpm2_protocol::command_header_bytes) ||
        !tpm2_protocol::parse_response_header(response, declared_size, header) ||
        header.size != declared_size) {
        (void)command_ready_state();
        return false;
    }
    response_size = declared_size;
    last_response_code = header.code;
    if ((read32(register_status) & status_data_available) != 0U) {
        (void)command_ready_state();
        return false;
    }
    return command_ready_state();
}

bool execute_command(bool (*builder)(uint8_t*, uint32_t, uint32_t&),
                     uint32_t& response_size) {
    uint32_t command_size = 0U;
    if (!builder || !builder(command_buffer, sizeof(command_buffer), command_size)) return false;
    return transmit(command_buffer, command_size, response_buffer, sizeof(response_buffer), response_size);
}

constexpr uint32_t nv_written_attribute = 0x20000000U;

tpm2_protocol::PublicValidation parse_counter_public_runtime(
        uint32_t response_size, tpm2_protocol::CounterPublic& public_area,
        uint32_t& response_code) {
    tpm2_protocol::PublicValidation validation = tpm2_protocol::parse_counter_public(
        response_buffer, response_size, public_area, response_code);
    if (validation != tpm2_protocol::PublicValidation::wrong_attributes ||
        response_size < 22U || public_area.attributes !=
            (tpm2_protocol::nv_attributes | nv_written_attribute)) {
        return validation;
    }

    const uint8_t saved[4] = {response_buffer[18U], response_buffer[19U],
                              response_buffer[20U], response_buffer[21U]};
    response_buffer[18U] = static_cast<uint8_t>(tpm2_protocol::nv_attributes >> 24U);
    response_buffer[19U] = static_cast<uint8_t>(tpm2_protocol::nv_attributes >> 16U);
    response_buffer[20U] = static_cast<uint8_t>(tpm2_protocol::nv_attributes >> 8U);
    response_buffer[21U] = static_cast<uint8_t>(tpm2_protocol::nv_attributes);
    validation = tpm2_protocol::parse_counter_public(
        response_buffer, response_size, public_area, response_code);
    response_buffer[18U] = saved[0];
    response_buffer[19U] = saved[1];
    response_buffer[20U] = saved[2];
    response_buffer[21U] = saved[3];
    if (validation == tpm2_protocol::PublicValidation::okay) {
        public_area.attributes = tpm2_protocol::nv_attributes | nv_written_attribute;
    }
    return validation;
}

bool refresh_counter(bool publish_marker) {
    counter_provisioned = false;
    counter_public_valid = false;
    counter_generation = 0U;
    uint32_t response_size = 0U;
    if (!execute_command(tpm2_protocol::build_nv_read_public, response_size)) return false;
    tpm2_protocol::CounterPublic public_area{};
    uint32_t response_code = 0U;
    const tpm2_protocol::PublicValidation validation =
        parse_counter_public_runtime(response_size, public_area, response_code);
    if (validation == tpm2_protocol::PublicValidation::response_error &&
        tpm2_protocol::missing_nv_index_code(response_code)) {
        if (publish_marker) {
            serial::write("TPM2_NV_COUNTER_UNPROVISIONED index=");
            serial_hex32(tpm2_protocol::nv_index);
            serial::write("\r\n");
        }
        return true;
    }
    if (validation != tpm2_protocol::PublicValidation::okay) {
        serial::write("TPM2_NV_COUNTER_REJECTED reason=");
        serial::line(tpm2_protocol::public_validation_name(validation));
        return false;
    }
    counter_public_valid = true;

    if (!execute_command(tpm2_protocol::build_nv_read_counter, response_size)) return false;
    if (!tpm2_protocol::parse_counter_value(response_buffer, response_size,
                                             counter_generation, response_code)) {
        if (counter_public_valid && response_code == 0x0000014AU) {
            if (publish_marker) {
                serial::write("TPM2_NV_COUNTER_DEFINED_UNINITIALIZED index=");
                serial_hex32(tpm2_protocol::nv_index);
                serial::write("\r\n");
            }
            return true;
        }
        return false;
    }
    counter_provisioned = true;
    if (publish_marker) {
        serial::write("TPM2_NV_COUNTER_READY generation=");
        serial_u64(counter_generation);
        serial::write(" index=");
        serial_hex32(tpm2_protocol::nv_index);
        serial::write("\r\n");
    }
    return true;
}

bool execute_increment() {
    if (!counter_public_valid) return false;
    uint32_t response_size = 0U;
    if (!execute_command(tpm2_protocol::build_nv_increment, response_size)) return false;
    uint32_t response_code = 0U;
    return tpm2_protocol::response_success(response_buffer, response_size, response_code);
}

bool increment_internal() {
    if (!counter_provisioned || !counter_public_valid || counter_generation == ~static_cast<uint64_t>(0U)) return false;
    const uint64_t before = counter_generation;
    if (!execute_increment() || !refresh_counter(false) || counter_generation != before + 1U) return false;
    return true;
}

bool provision_counter() {
    if (!transport_ready || !refresh_counter(false) || counter_provisioned) return false;
    uint32_t response_size = 0U;
    uint32_t response_code = 0U;
    if (!counter_public_valid) {
        if (!execute_command(tpm2_protocol::build_nv_define_counter, response_size) ||
            !tpm2_protocol::response_success(response_buffer, response_size, response_code) ||
            !refresh_counter(false) || !counter_public_valid || counter_provisioned) {
            return false;
        }
    }
    if (!execute_increment() || !refresh_counter(false) || !counter_provisioned ||
        counter_generation != 1U) {
        return false;
    }
    serial::write("TPM2_NV_COUNTER_PROVISIONED generation=");
    serial_u64(counter_generation);
    serial::write(" index=");
    serial_hex32(tpm2_protocol::nv_index);
    serial::write("\r\n");
    return true;
}

bool increment_counter() {
    if (!refresh_counter(false) || !increment_internal()) return false;
    serial::write("TPM2_NV_COUNTER_INCREMENTED generation=");
    serial_u64(counter_generation);
    serial::write("\r\n");
    return true;
}

bool self_test() {
    const bool okay = transport_ready && refresh_counter(false) &&
                      (!counter_provisioned || counter_public_valid);
    if (okay) {
        serial::write("TPM2_SELFTEST_OK generation=");
        serial_u64(counter_generation);
        serial::write("\r\n");
    } else {
        serial::write("TPM2_SELFTEST_FAILED rc=");
        serial_hex32(last_response_code);
        serial::write("\r\n");
    }
    return okay;
}

void print_status() {
    console::write("  Transport     ");
    console::line(transport_ready ? "TIS MMIO / locality 0 ready" : "unavailable");
    console::write("  DID_VID       "); console::hex(did_vid); console::put('\n');
    console::write("  Revision      "); console::unsigned_dec(revision); console::put('\n');
    console::write("  NV index      "); console::hex(tpm2_protocol::nv_index); console::put('\n');
    console::write("  Counter       ");
    if (counter_provisioned) { console_u64(counter_generation); console::put('\n'); }
    else console::line("not provisioned");
    serial::write("TPM2_STATUS transport=");
    serial::write(transport_ready ? "ready" : "unavailable");
    serial::write(" counter=");
    if (counter_provisioned) serial_u64(counter_generation);
    else serial::write("unprovisioned");
    serial::write("\r\n");
}

void init() {
    transport_ready = false;
    counter_provisioned = false;
    counter_public_valid = false;
    counter_generation = 0U;
    if (!paging::map_tpm_tis()) {
        serial::line("TPM2_TIS_MAPPING_FAILED");
        return;
    }
    did_vid = read32(register_did_vid);
    revision = read8(register_revision);
    const uint16_t vendor = static_cast<uint16_t>(did_vid & 0xFFFFU);
    if (did_vid == 0U || did_vid == 0xFFFFFFFFU || vendor == 0U || vendor == 0xFFFFU) {
        serial::line("TPM2_TIS_UNAVAILABLE");
        return;
    }
    transport_ready = true;
    if (!request_locality()) {
        transport_ready = false;
        serial::line("TPM2_TIS_LOCALITY_FAILED");
        return;
    }
    uint32_t response_size = 0U;
    if (!execute_command(tpm2_protocol::build_startup, response_size)) {
        transport_ready = false;
        serial::line("TPM2_STARTUP_TRANSPORT_FAILED");
        return;
    }
    uint32_t response_code = 0U;
    tpm2_protocol::ResponseHeader header{};
    if (!tpm2_protocol::parse_response_header(response_buffer, response_size, header)) {
        transport_ready = false;
        serial::line("TPM2_STARTUP_RESPONSE_FAILED");
        return;
    }
    response_code = header.code;
    if (response_code != tpm2_protocol::rc_success && response_code != tpm2_protocol::rc_initialize) {
        transport_ready = false;
        serial::write("TPM2_STARTUP_REJECTED rc=");
        serial_hex32(response_code);
        serial::write("\r\n");
        return;
    }
    serial::write("TPM2_TIS_READY didvid=");
    serial_hex32(did_vid);
    serial::write(" revision=");
    serial_u64(revision);
    serial::write(" locality=0\r\n");
    if (!refresh_counter(true)) {
        serial::write("TPM2_NV_COUNTER_INIT_FAILED rc=");
        serial_hex32(last_response_code);
        serial::write("\r\n");
    }
}

} // namespace tpm2
