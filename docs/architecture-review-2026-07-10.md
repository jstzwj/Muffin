# Muffin Architecture and Reliability Review

Date: 2026-07-10

## Executive Summary

Muffin's parser, document model, projection, editing, and rendering layers are unusually well tested for a desktop Markdown editor. The main weaknesses are at application workflow boundaries: document switching, crash recovery, filesystem side effects, file-format preservation, and distributable packaging.

The findings below are ordered by risk. High-risk items can lose recovery data, break document resources, miss external changes, or publish unusable artifacts. Medium-risk items violate user-visible contracts or leave application state inconsistent.

## High-Risk Findings

### 1. Recovery deletes drafts whose source file is missing

`DraftRecovery::pruneOrphaned()` deletes complete drafts when their source file was deleted or moved. `MainWindow::restoreDraft()` nevertheless contains an explicit path for restoring exactly such a draft as an untitled document. The test suite currently codifies deletion as the expected behavior.

Impact: a crash draft can be permanently lost after an external delete, sync conflict, or folder move.

Root fix: retain every complete draft until the user restores or explicitly discards it. Pruning should remove only incomplete or structurally invalid records.

### 2. Untitled drafts collide across windows

Every untitled document uses the fixed key `untitled`, while Muffin supports multiple windows. A snapshot or cleanup in one window can overwrite or remove another window's recovery state.

Impact: unsaved content from one window can silently replace another window's draft.

Root fix: give each document instance a stable recovery UUID and use it as the storage identity independently of its source path.

### 3. Move All Images moves files without updating Markdown

The command discards the destination returned by `moveImageTo()`, then tries to resolve each old path after the old file has already moved. Resolution fails and the Markdown references are skipped.

Impact: the command reports success while leaving every moved image reference broken.

Root fix: build and validate an old-path to new-path plan first, execute unique moves transactionally, rewrite references from the recorded mapping, and roll back filesystem moves if any step fails.

### 4. External file monitoring can stop after an atomic replace

Files are saved with `QSaveFile`, while `QFileSystemWatcher` is added only when the path changes. Atomic save tools replace the watched file, which can remove the watch. Neither the watcher callback nor the save/reload baseline path reinstalls it.

Impact: after the first save or atomic external edit, later external changes may no longer be detected.

Root fix: re-arm the file watch after every baseline update and after every change notification; also watch the containing directory to survive replace/rename operations.

### 5. Deleting the active modified file resolves unsaved state too late

The active file is moved to trash before unsaved changes are resolved. `newFile()` can then fail because Save targets a file that no longer exists, but the result is ignored and undo history is still cleared.

Impact: the disk file is gone, the stale document can remain active, and its undo history is lost.

Root fix: resolve Save/Discard/Cancel before filesystem deletion, abort deletion on Cancel or failed Save, then reset the session only after deletion succeeds.

### 6. Known sanitizer failures do not block CI

The CI workflow documents nondeterministic heap corruption but marks the Linux ASan job `continue-on-error`.

Impact: a known memory-safety signal cannot prevent merge or release.

Root fix: make the sanitizer job blocking after stabilizing its test environment and keep only demonstrably unrelated platform leaks suppressed.

### 7. Unix release archives are not reliably self-contained

Linux deployment relies on `TARGET_RUNTIME_DLLS`, which does not collect ELF shared libraries. The macOS tar archive is created before `macdeployqt`; only the later DMG path deploys frameworks.

Impact: published Linux and macOS tar archives can pass CI while failing on a clean user machine.

Root fix: collect ELF dependencies with CMake runtime dependency APIs or a platform deployment tool, run `macdeployqt` before every macOS archive, and launch each packaged artifact in an isolated smoke test.

## Medium-Risk Findings

### 8. Encoding and line-ending metadata are not part of document state

Default open decodes with UTF-8 replacement semantics and every save writes UTF-8. The preference labeled "Default line break for new files" is applied to every saved file.

Impact: legacy encodings can be irreversibly corrupted and existing files can receive whole-file line-ending churn.

Root fix: detect and store encoding, BOM, original line ending, and trailing-newline state in the document session. Apply defaults only to new documents and preserve metadata for existing files unless the user explicitly converts it.

### 9. Open-file discard is committed before a target is acquired

The current draft is cleared before the file picker completes and before the target can be read. If the picker is canceled or the read fails, the current document remains dirty but its existing draft has been removed; revision deduplication can prevent it from being recreated.

Root fix: acquire and validate the target first, then resolve the current document, and only mark it discarded when the switch is guaranteed to commit.

### 10. Parent-folder rename and delete do not update the active session

Sidebar operations compare only exact paths. Renaming or deleting a directory containing the active file leaves the session pointing at the old descendant path.

Root fix: detect path ancestry and remap or detach the active document for directory operations.

### 11. Update-check request origin is stale global state

An automatic check sets `userInitiated_` to false and manual checks never restore it. Every window subscribes to the same singleton response, which can also create duplicate dialogs.

Root fix: carry request origin and requesting window in each check operation instead of mutable singleton state, and record successful-check time only after a valid response.

### 12. Regex find is advertised but not implemented

The find UI performs literal `indexOf`, `lastIndexOf`, and `replace` operations. It has no regex mode, case option, result count, or rendered-match highlight despite the README claim.

Root fix: centralize matching in a tested search service used by both editor backends, with explicit literal/regex and case modes, zero-length match handling, replacement expansion, and stable match ranges.

## Recommended Implementation Order

1. Recovery identity and retention, then document-switch cleanup timing.
2. Transactional image moves, active-file deletion ordering, and durable file watching.
3. Release dependency deployment and blocking sanitizer coverage.
4. File-format metadata and directory ancestry synchronization.
5. Request-scoped update checks and a shared find/replace engine.

## Verification Baseline

Before changes, the Conan Release build succeeded and all 107 registered tests passed on Windows. This confirms that the main gaps are cross-component workflow contracts rather than the existing parser/render unit paths.
