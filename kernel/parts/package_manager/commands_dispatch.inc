bool contains_folded(const char* text, const char* query) {
    if (!text || !query || !*query) return false;
    for (const char* start = text; *start; ++start) {
        const char* left = start;
        const char* right = query;
        while (*left && *right && ascii_lower(*left) == ascii_lower(*right)) {
            ++left;
            ++right;
        }
        if (!*right) return true;
    }
    return false;
}

bool split_subcommand(char* input, char*& action, char*& remainder) {
    if (!input) return false;
    while (is_space(*input)) ++input;
    if (!*input) return false;
    action = input;
    while (*input && !is_space(*input)) ++input;
    if (*input) *input++ = 0;
    while (is_space(*input)) ++input;
    remainder = input;
    lower_in_place(action);
    return true;
}

bool command_token_equal(const char* line, const char* expected) {
    if (!line) return false;
    while (is_space(*line)) ++line;
    while (*expected && *line && !is_space(*line)) {
        if (ascii_lower(*line++) != *expected++) return false;
    }
    return !*expected && (!*line || is_space(*line));
}

void repository_command(char* input) {
    char* action = nullptr;
    char* arguments = nullptr;
    if (!split_subcommand(input, action, arguments)) {
        console::line("Usage: pkg repo status|targets|search|check|refresh");
        return;
    }
    if (string_equal(action, "status")) {
        package_repository::print_status();
    } else if (string_equal(action, "targets")) {
        package_repository::print_targets();
    } else if (string_equal(action, "search")) {
        if (*arguments) package_repository::print_search(arguments);
        else console::warning("Usage: pkg repo search <query>");
    } else if (string_equal(action, "check")) {
        if (package_repository::check()) console::success("PKG_REPOSITORY_CHECK_OK");
        else console::warning("Package repository check rejected.");
    } else if (string_equal(action, "refresh")) {
        if (package_repository::refresh()) console::success("PKG_REPOSITORY_REFRESH_OK");
        else console::warning("Package repository refresh rejected.");
    } else {
        console::warning("Unknown pkg repo action.");
    }
}

void cache_command(char* input) {
    char* action = nullptr;
    char* arguments = nullptr;
    if (!split_subcommand(input, action, arguments)) {
        console::line("Usage: pkg cache status|verify|clean");
        return;
    }
    (void)arguments;
    if (string_equal(action, "status")) {
        cache_status();
    } else if (string_equal(action, "verify")) {
        if (cache_verify()) console::success("PKG_CACHE_VERIFY_OK");
        else console::warning("Package cache verification failed.");
    } else if (string_equal(action, "clean")) {
        if (cache_clean()) console::success("PKG_CACHE_CLEAN_OK");
        else console::warning("Package cache cleanup failed.");
    } else {
        console::warning("Unknown pkg cache action.");
    }
}

void transport_command(char* input) {
    char* action = nullptr;
    char* arguments = nullptr;
    if (!split_subcommand(input, action, arguments)) {
        console::line("Usage: pkg transport status|step <name>|resume <name>|cancel");
        return;
    }
    if (string_equal(action, "status")) {
        transport_status();
    } else if (string_equal(action, "step")) {
        lower_in_place(arguments);
        if (!*arguments) {
            console::warning("Usage: pkg transport step <name>");
            return;
        }
        const TransportOutcome outcome = fetch_latest_step(arguments);
        if (outcome == TransportOutcome::pending) console::success("PKG_TRANSPORT_STEP_PENDING");
        else if (outcome == TransportOutcome::complete) console::success("PKG_TRANSPORT_STEP_COMPLETE");
        else console::warning("Transport step failed.");
    } else if (string_equal(action, "resume")) {
        lower_in_place(arguments);
        if (!*arguments) console::warning("Usage: pkg transport resume <name>");
        else if (fetch_latest(arguments)) console::success("PKG_TRANSPORT_RESUME_OK");
        else console::warning("Transport resume failed.");
    } else if (string_equal(action, "cancel")) {
        if (transport_cancel()) console::success("PKG_TRANSPORT_CANCEL_OK");
        else console::warning("Transport cancellation failed.");
    } else {
        console::warning("Unknown pkg transport action.");
    }
}

void command(char* input) {
    char* action = nullptr;
    char* arguments = nullptr;
    if (!split_subcommand(input, action, arguments)) {
        console::line("Usage: pkg status|list|search|plan|verify|fetch|install|upgrade|repair|policy|info|rollback|remove|run|cache|transport|repo");
        return;
    }
    if (string_equal(action, "status")) {
        status();
    } else if (string_equal(action, "list")) {
        list();
    } else if (string_equal(action, "search")) {
        if (*arguments) search(arguments);
        else console::warning("Usage: pkg search <query>");
    } else if (string_equal(action, "plan")) {
        plan(arguments);
    } else if (string_equal(action, "verify")) {
        if (*arguments) verify(arguments);
        else console::warning("Usage: pkg verify <package.zpk>");
    } else if (string_equal(action, "fetch")) {
        lower_in_place(arguments);
        if (*arguments) fetch_latest(arguments);
        else console::warning("Usage: pkg fetch <name>");
    } else if (string_equal(action, "install")) {
        if (!*arguments) {
            console::warning("Usage: pkg install <name|package.zpk>");
        } else if (arguments[0] == '/' || contains_folded(arguments, ".zpk")) {
            install(arguments);
        } else {
            lower_in_place(arguments);
            install_latest(arguments);
        }
    } else if (string_equal(action, "upgrade")) {
        upgrade(arguments);
    } else if (string_equal(action, "repair")) {
        repair(arguments);
    } else if (string_equal(action, "policy")) {
        lower_in_place(arguments);
        if (*arguments) package_repository::print_policy(arguments);
        else console::warning("Usage: pkg policy <name>");
    } else if (string_equal(action, "info")) {
        lower_in_place(arguments);
        if (*arguments) info(arguments);
        else console::warning("Usage: pkg info <name>");
    } else if (string_equal(action, "rollback")) {
        lower_in_place(arguments);
        if (*arguments) rollback(arguments);
        else console::warning("Usage: pkg rollback <name>");
    } else if (string_equal(action, "remove")) {
        lower_in_place(arguments);
        if (*arguments) remove_package(arguments);
        else console::warning("Usage: pkg remove <name>");
    } else if (string_equal(action, "run")) {
        run_package(arguments);
    } else if (string_equal(action, "cache")) {
        cache_command(arguments);
    } else if (string_equal(action, "transport")) {
        transport_command(arguments);
    } else if (string_equal(action, "repo")) {
        repository_command(arguments);
    } else {
        console::warning("Unknown pkg action.");
    }
}

bool dispatch_line(char* line) {
    if (!command_token_equal(line, "pkg")) return false;
    while (is_space(*line)) ++line;
    while (*line && !is_space(*line)) ++line;
    while (is_space(*line)) ++line;
    command(line);
    return true;
}
