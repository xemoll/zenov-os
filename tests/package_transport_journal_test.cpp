#include <cstdint>
#include <cstring>
#include <iostream>

using std::uint8_t;
using std::uint32_t;

constexpr uint32_t maximum_package_bytes = 64U * 1024U;

namespace storage {
uint32_t fnv1a(const uint8_t* data, std::size_t size) {
    uint32_t hash = 2166136261U;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

bool bytes_equal(const char* left, const char* right, std::size_t count) {
    return std::memcmp(left, right, count) == 0;
}
} // namespace storage

#include "../kernel/parts/package_manager/transport_format.inc"

namespace {
TransportJournal valid_journal() {
    TransportJournal journal{};
    const char magic[8] = {'Z','P','T','R','N','1',0,0};
    std::memcpy(journal.magic, magic, sizeof(magic));
    journal.schema = 1U;
    journal.generation = 7U;
    journal.phase = transport_phase_downloading;
    journal.offset = 512U;
    journal.expected_length = 647U;
    journal.provider = transport_provider_offline;
    std::strcpy(journal.name, "hello-native");
    std::strcpy(journal.version, "0.2.0");
    std::strcpy(journal.source_path, "/data/packages/hello-native-0.2.0.zpk");
    std::strcpy(journal.partial_path, "/var/cache/zp/0123456789abcdef01234567.part");
    for (uint32_t i = 0; i < 32U; ++i) {
        journal.package_digest[i] = static_cast<uint8_t>(i);
        journal.prefix_digest[i] = static_cast<uint8_t>(31U - i);
    }
    journal.checksum = transport_journal_checksum(journal);
    return journal;
}

bool rejected_after_rechecksum(TransportJournal journal) {
    journal.checksum = transport_journal_checksum(journal);
    return !transport_journal_valid(journal);
}

int fail(const char* message) {
    std::cerr << "package-transport-journal-test: " << message << '\n';
    return 1;
}
} // namespace

int main() {
    const TransportJournal baseline = valid_journal();
    if (!transport_journal_valid(baseline)) return fail("valid journal rejected");
    if (sizeof(TransportJournal) != 248U) return fail("journal size changed");

    TransportJournal corrupted = baseline;
    corrupted.prefix_digest[5] ^= 0x80U;
    if (transport_journal_valid(corrupted)) return fail("checksum corruption accepted");

    TransportJournal bad_offset = baseline;
    bad_offset.offset = bad_offset.expected_length + 1U;
    if (!rejected_after_rechecksum(bad_offset)) return fail("oversized offset accepted");

    TransportJournal bad_ready = baseline;
    bad_ready.phase = transport_phase_ready;
    if (!rejected_after_rechecksum(bad_ready)) return fail("incomplete ready journal accepted");

    TransportJournal bad_provider = baseline;
    bad_provider.provider = 99U;
    if (!rejected_after_rechecksum(bad_provider)) return fail("unknown provider accepted");

    TransportJournal bad_generation = baseline;
    bad_generation.generation = 0U;
    if (!rejected_after_rechecksum(bad_generation)) return fail("zero generation accepted");

    TransportJournal oversized = baseline;
    oversized.expected_length = maximum_package_bytes + 1U;
    oversized.offset = 0U;
    if (!rejected_after_rechecksum(oversized)) return fail("oversized target accepted");

    TransportJournal unterminated = baseline;
    std::memset(unterminated.name, 'x', sizeof(unterminated.name));
    if (!rejected_after_rechecksum(unterminated)) return fail("unterminated name accepted");

    TransportJournal failed = baseline;
    failed.phase = transport_phase_failed;
    failed.checksum = transport_journal_checksum(failed);
    if (!transport_journal_valid(failed)) return fail("failed state is not recoverable");

    std::cout << "ZENPKG_TRANSPORT_JOURNAL_TEST_OK schema=1 bytes=" << sizeof(TransportJournal)
              << " negative-cases=7\n";
    return 0;
}
