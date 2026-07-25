struct FsResultState {
    uint32_t requests;
    uint32_t completed;
    uint32_t failed;
    uint32_t recovery_pending;
    FsResult last_result;
    FsResult last_failure;
};

FsResultState zfs_result_state{
    0U,
    0U,
    0U,
    0U,
    fs_success(FsOperation::none),
    fs_success(FsOperation::none)
};

FsResult fs_record_result(const FsResult& result) {
    ++zfs_result_state.requests;
    zfs_result_state.last_result = result;
    if (result.ok()) {
        ++zfs_result_state.completed;
        if (result.status == FsStatus::recovery_pending) ++zfs_result_state.recovery_pending;
    } else {
        ++zfs_result_state.failed;
        zfs_result_state.last_failure = result;
    }
    return result;
}

constexpr bool fs_result_counters_valid(const FsResultState& state) {
    return state.requests == state.completed + state.failed &&
           state.recovery_pending <= state.completed;
}

const FsResult& fs_last_result() { return zfs_result_state.last_result; }
const FsResult& fs_last_failure() { return zfs_result_state.last_failure; }
