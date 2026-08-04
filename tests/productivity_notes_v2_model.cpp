#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t capacity = 2048U;
constexpr std::size_t history_capacity = 8U;
constexpr std::size_t edit_payload_capacity = 4U;

enum class EditKind : std::uint8_t { insert, erase };

struct Edit {
    EditKind kind{};
    std::uint16_t position{};
    std::uint16_t cursor_before{};
    std::uint16_t cursor_after{};
    std::uint8_t count{};
    std::array<char, edit_payload_capacity> bytes{};
    bool dirty_before{};
    bool dirty_after{};
};
static_assert(sizeof(Edit) <= 16U);

struct Editor {
    std::array<char, capacity> text{};
    std::uint16_t size{};
    std::uint16_t cursor{};
    std::uint16_t preferred_column{};
    bool preferred_valid{};
    bool dirty{};
    std::array<Edit, history_capacity> undo{};
    std::array<Edit, history_capacity> redo{};
    std::uint8_t undo_count{};
    std::uint8_t redo_count{};

    explicit Editor(std::string_view initial) {
        if (initial.size() >= capacity) fail("initial text exceeds capacity");
        std::copy(initial.begin(), initial.end(), text.begin());
        size = static_cast<std::uint16_t>(initial.size());
        cursor = size;
        text[size] = 0;
    }

    [[noreturn]] static void fail(const char* message) {
        std::cerr << "productivity_notes_v2_model: " << message << '\n';
        std::exit(1);
    }

    std::string value() const { return std::string(text.data(), size); }

    static void push(std::array<Edit, history_capacity>& stack, std::uint8_t& count, const Edit& edit) {
        if (count == history_capacity) {
            std::move(stack.begin() + 1, stack.end(), stack.begin());
            count = static_cast<std::uint8_t>(history_capacity - 1U);
        }
        stack[count++] = edit;
    }

    bool bytes_match(std::uint16_t position, const char* bytes, std::uint8_t count) const {
        if (!bytes || !count || position > size || count > static_cast<std::uint16_t>(size - position)) return false;
        return std::equal(bytes, bytes + count, text.begin() + position);
    }

    bool raw_insert(std::uint16_t position, const char* bytes, std::uint8_t count) {
        if (!bytes || !count || count > edit_payload_capacity || position > size ||
            static_cast<std::size_t>(size) + count >= capacity) return false;
        const std::size_t tail = static_cast<std::size_t>(size - position);
        std::memmove(text.data() + position + count, text.data() + position, tail + 1U);
        std::memcpy(text.data() + position, bytes, count);
        size = static_cast<std::uint16_t>(size + count);
        text[size] = 0;
        return true;
    }

    bool raw_erase(std::uint16_t position, const char* expected, std::uint8_t count) {
        if (!count || count > edit_payload_capacity || position > size || count > static_cast<std::uint16_t>(size - position)) return false;
        if (expected && !bytes_match(position, expected, count)) return false;
        const std::size_t tail = static_cast<std::size_t>(size - position - count);
        std::memmove(text.data() + position, text.data() + position + count, tail + 1U);
        size = static_cast<std::uint16_t>(size - count);
        text[size] = 0;
        return true;
    }

    void finish(const Edit& edit) {
        push(undo, undo_count, edit);
        redo_count = 0U;
        cursor = std::min(edit.cursor_after, size);
        dirty = edit.dirty_after;
        preferred_valid = false;
    }

    std::uint16_t line_start(std::uint16_t offset) const {
        std::size_t position = std::min<std::size_t>(offset, size);
        while (position > 0U && text[position - 1U] != '\n') --position;
        return static_cast<std::uint16_t>(position);
    }

    std::uint16_t line_end(std::uint16_t offset) const {
        std::size_t position = std::min<std::size_t>(offset, size);
        while (position < size && text[position] != '\n') ++position;
        return static_cast<std::uint16_t>(position);
    }

    std::uint16_t column() const { return static_cast<std::uint16_t>(cursor - line_start(cursor)); }

    bool insert(std::string_view bytes) {
        if (bytes.empty() || bytes.size() > edit_payload_capacity || static_cast<std::size_t>(size) + bytes.size() >= capacity) return false;
        Edit edit{};
        edit.kind = EditKind::insert;
        edit.position = cursor;
        edit.cursor_before = cursor;
        edit.cursor_after = static_cast<std::uint16_t>(cursor + bytes.size());
        edit.count = static_cast<std::uint8_t>(bytes.size());
        std::copy(bytes.begin(), bytes.end(), edit.bytes.begin());
        edit.dirty_before = dirty;
        edit.dirty_after = true;
        if (!raw_insert(edit.position, edit.bytes.data(), edit.count)) return false;
        finish(edit);
        return true;
    }

    bool backspace() {
        if (!cursor || !size) return false;
        Edit edit{};
        edit.kind = EditKind::erase;
        edit.position = static_cast<std::uint16_t>(cursor - 1U);
        edit.cursor_before = cursor;
        edit.cursor_after = edit.position;
        edit.count = 1U;
        edit.bytes[0] = text[edit.position];
        edit.dirty_before = dirty;
        edit.dirty_after = true;
        if (!raw_erase(edit.position, edit.bytes.data(), edit.count)) return false;
        finish(edit);
        return true;
    }

    bool erase_forward() {
        if (cursor >= size) return false;
        Edit edit{};
        edit.kind = EditKind::erase;
        edit.position = cursor;
        edit.cursor_before = cursor;
        edit.cursor_after = cursor;
        edit.count = 1U;
        edit.bytes[0] = text[edit.position];
        edit.dirty_before = dirty;
        edit.dirty_after = true;
        if (!raw_erase(edit.position, edit.bytes.data(), edit.count)) return false;
        finish(edit);
        return true;
    }

    void home() { cursor = line_start(cursor); preferred_valid = false; }
    void end() { cursor = line_end(cursor); preferred_valid = false; }
    void left() { if (cursor) --cursor; preferred_valid = false; }
    void right() { if (cursor < size) ++cursor; preferred_valid = false; }

    void vertical(bool down) {
        const std::uint16_t current_start = line_start(cursor);
        if (!preferred_valid) {
            preferred_column = static_cast<std::uint16_t>(cursor - current_start);
            preferred_valid = true;
        }
        std::uint16_t target_start = current_start;
        if (down) {
            const std::uint16_t current_end = line_end(cursor);
            if (current_end >= size) return;
            target_start = static_cast<std::uint16_t>(current_end + 1U);
        } else {
            if (!current_start) return;
            target_start = line_start(static_cast<std::uint16_t>(current_start - 1U));
        }
        const std::uint16_t target_end = line_end(target_start);
        const std::uint16_t target_length = static_cast<std::uint16_t>(target_end - target_start);
        cursor = static_cast<std::uint16_t>(target_start + std::min(preferred_column, target_length));
    }

    bool undo_once() {
        if (!undo_count) return false;
        const Edit edit = undo[undo_count - 1U];
        const bool applied = edit.kind == EditKind::insert
            ? raw_erase(edit.position, edit.bytes.data(), edit.count)
            : raw_insert(edit.position, edit.bytes.data(), edit.count);
        if (!applied) return false;
        --undo_count;
        push(redo, redo_count, edit);
        cursor = std::min(edit.cursor_before, size);
        dirty = edit.dirty_before;
        preferred_valid = false;
        return true;
    }

    bool redo_once() {
        if (!redo_count) return false;
        const Edit edit = redo[redo_count - 1U];
        const bool applied = edit.kind == EditKind::insert
            ? raw_insert(edit.position, edit.bytes.data(), edit.count)
            : raw_erase(edit.position, edit.bytes.data(), edit.count);
        if (!applied) return false;
        --redo_count;
        push(undo, undo_count, edit);
        cursor = std::min(edit.cursor_after, size);
        dirty = edit.dirty_after;
        preferred_valid = false;
        return true;
    }
};

void require(bool condition, const char* message) {
    if (!condition) Editor::fail(message);
}

} // namespace

int main() {
    Editor editor("alpha\ngama\ndeltaX");
    require(editor.backspace(), "backspace rejected");
    require(editor.value() == "alpha\ngama\ndelta", "backspace removed wrong byte");

    editor.home();
    editor.right();
    editor.right();
    require(editor.erase_forward(), "forward delete rejected");
    require(editor.value() == "alpha\ngama\ndeta", "forward delete removed wrong byte");
    require(editor.insert("l"), "middle insert rejected");
    require(editor.value() == "alpha\ngama\ndelta", "middle insert restored wrong text");

    editor.end();
    editor.vertical(false);
    require(editor.column() == 4U, "vertical move did not clamp preferred column");
    require(editor.insert("m"), "gamma insertion rejected");
    require(editor.value() == "alpha\ngamam\ndelta", "vertical insertion mismatch");
    editor.left();
    require(editor.erase_forward(), "delete after horizontal move rejected");
    require(editor.value() == "alpha\ngama\ndelta", "delete did not restore line");

    require(editor.insert("!"), "history fixture insert rejected");
    const std::string edited = editor.value();
    require(editor.undo_once(), "undo rejected");
    require(editor.value() == "alpha\ngama\ndelta", "undo restored wrong operation");
    require(!editor.dirty || editor.undo_count != 0U, "dirty state was not restored");
    require(editor.redo_once(), "redo rejected");
    require(editor.value() == edited && editor.dirty, "redo restored wrong operation");

    Editor preferred("123456\nx\nabcdef");
    preferred.home();
    preferred.right();
    preferred.right();
    preferred.right();
    preferred.right();
    preferred.right();
    preferred.vertical(false);
    require(preferred.column() == 1U, "short-line clamp failed");
    preferred.vertical(false);
    require(preferred.column() == 5U, "preferred column was not retained");

    Editor bounded(std::string(capacity - 2U, 'x'));
    require(bounded.insert("y"), "last legal byte rejected");
    const std::uint16_t before = bounded.size;
    require(!bounded.insert("z"), "capacity overflow accepted");
    require(!bounded.insert("large"), "oversized history payload accepted");
    require(bounded.size == before && bounded.text[bounded.size] == 0, "capacity rejection mutated buffer");

    Editor history("");
    for (int index = 0; index < 12; ++index) require(history.insert("x"), "history insert failed");
    require(history.undo_count == history_capacity, "history capacity not bounded");
    for (std::size_t index = 0; index < history_capacity; ++index) require(history.undo_once(), "bounded undo failed");
    require(!history.undo_once(), "undo exceeded retained history");

    Editor invalidation("");
    require(invalidation.insert("a"), "redo invalidation seed failed");
    require(invalidation.undo_once(), "redo invalidation undo failed");
    require(invalidation.redo_count == 1U, "redo entry missing");
    require(invalidation.insert("b"), "replacement edit failed");
    require(invalidation.redo_count == 0U && !invalidation.redo_once(), "new edit did not invalidate redo");

    Editor stale("abc");
    require(stale.insert("x"), "stale history seed failed");
    stale.text[3] = 'y';
    const std::string stale_before = stale.value();
    require(!stale.undo_once(), "mismatched history bytes were accepted");
    require(stale.value() == stale_before && stale.undo_count == 1U, "failed history validation mutated state");

    std::cout << "PRODUCTIVITY_NOTES_V2_MODEL_OK cursor=insert+backspace+delete navigation=vertical+home+end history=undo8+redo8+oplog bounds=2048 static=compact\n";
    return 0;
}
