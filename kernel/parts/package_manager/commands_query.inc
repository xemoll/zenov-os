void list() {
    console::line("  NAME                             ACTIVE          PREVIOUS");
    console::line("  -------------------------------- --------------- ---------------");
    uint32_t shown = 0;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        const PackageRecord& record = database.records[i];
        if (!record.used) continue;
        console::write("  ");
        console::write(record.name);
        uint32_t width = static_cast<uint32_t>(string_length(record.name));
        while (width++ < 33U) console::put(' ');
        console::write(record.active.version);
        width = static_cast<uint32_t>(string_length(record.active.version));
        while (width++ < 16U) console::put(' ');
        console::line(record.previous.valid ? record.previous.version : "-");
        ++shown;
    }
    if (!shown) console::line("  <no installed packages>");
    serial::line("ZENPKG_LIST_OK");
}

void info(const char* name) {
    const int32_t index = find_record(name);
    if (index < 0) {
        console::warning("PKG_NOT_INSTALLED");
        serial::line("ZENPKG_NOT_INSTALLED");
        return;
    }
    const PackageRecord& record = database.records[index];
    console::write("  Name          ");
    console::line(record.name);
    print_reference("Active", record.active);
    print_reference("Previous", record.previous);
    console::write("  DB generation ");
    console::unsigned_dec(database.generation);
    console::put('\n');
    serial::write("ZENPKG_INFO name=");
    serial::write(record.name);
    serial::write(" version=");
    serial::line(record.active.version);
}

void status() {
    const bool repository_has_targets = package_repository::target_at(0U) != nullptr;
    const bool repository_paths_protected = security_policy::repository_metadata_path("/repo/timestamp.zrm");
    console::write("  State         ");
    console::line(initialized ? "ready" : "unavailable");
    console::write("  Installed     ");
    console::unsigned_dec(database.count);
    console::put('\n');
    console::write("  Authorized    ");
    console::unsigned_dec(authorized_package_count());
    console::line(" signed targets");
    console::write("  Repository    ");
    console::line(repository_has_targets ? "verified signed target set" : "no verified targets");
    console::write("  Metadata path ");
    console::line(repository_paths_protected ? "protected" : "policy error");
    console::line("  Integrity     canonical package + package/payload SHA-256");
    console::line("  Syscalls      signed least-privilege masks and exact scopes");
    console::line("  Transaction   immutable payload + atomic active-reference commit");
    console::line("  Operations    search, plan, install, upgrade, repair, rollback and remove");
    console::line("  Transport     offline; network download remains disabled");
    serial::line("ZENPKG_STATUS_OK");
}

bool verify(const char* path) {
    native_package::VerifiedPackage package{};
    uint32_t package_size = 0;
    const native_package::Error error = native_package::verify_file(
        path, package_buffer, storage::file_capacity(), package, package_size);
    if (error != native_package::Error::none) {
        console::write("Package verification failed: ");
        console::line(native_package::error_text(error));
        serial::line("ZENPKG_VERIFY_REJECTED");
        return false;
    }
    const bool authorized = authorized_package(package, package_size);
    console::write("  Name          ");
    console::line(package.manifest.name);
    console::write("  Version       ");
    console::line(package.manifest.version);
    console::write("  Entrypoint    ");
    console::line(package.manifest.entrypoint);
    console::write("  Package bytes ");
    console::unsigned_dec(package_size);
    console::put('\n');
    console::write("  Payload bytes ");
    console::unsigned_dec(package.payload_size);
    console::put('\n');
    console::write("  Authorization ");
    console::line(authorized ? "signed target" : "not authorized");
    console::success("PKG_VERIFY_OK");
    serial::line(authorized ? "ZENPKG_VERIFY_AUTHORIZED" : "ZENPKG_VERIFY_UNAUTHORIZED");
    serial::line("ZENPKG_VERIFY_OK");
    return true;
}

void search(const char* query) {
    package_repository::print_search(query);
    serial::line("ZENPKG_SEARCH_OK");
}

void plan_one(const char* name) {
    const package_repository::Target* latest = package_repository::latest_target(name);
    const int32_t index = find_record(name);
    if (!latest) {
        console::write("  ");
        console::write(name);
        console::line("  unavailable in signed repository");
        serial::line("ZENPKG_PLAN_UNAVAILABLE");
        return;
    }
    if (index < 0) {
        console::write("  install ");
        console::write(latest->name);
        console::put('@');
        console::line(latest->version);
        serial::line("ZENPKG_PLAN_INSTALL");
        return;
    }
    const int order = version_compare(latest->version, database.records[index].active.version);
    console::write("  ");
    console::write(name);
    console::write("  ");
    if (order > 0) {
        console::write(database.records[index].active.version);
        console::write(" -> ");
        console::line(latest->version);
        serial::line("ZENPKG_PLAN_UPGRADE");
    } else if (order == 0) {
        console::line("current");
        serial::line("ZENPKG_PLAN_CURRENT");
    } else {
        console::line("installed version is newer than repository");
        serial::line("ZENPKG_PLAN_REPOSITORY_BEHIND");
    }
}

void plan(char* name) {
    lower_in_place(name);
    if (*name && !string_equal(name, "all")) {
        plan_one(name);
    } else {
        uint32_t shown = 0;
        for (uint32_t i = 0; i < maximum_packages; ++i) {
            if (!database.records[i].used) continue;
            plan_one(database.records[i].name);
            ++shown;
        }
        if (!shown) console::line("  <no installed packages>");
    }
    serial::line("ZENPKG_PLAN_OK");
}
