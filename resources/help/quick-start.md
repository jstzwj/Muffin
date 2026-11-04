# Quick Start

Welcome to **Muffin**, a fast, native Markdown editor built with C++ and Qt.

## Create or open a document

- Choose **File → New** for a blank document, or **File → Open** to open an existing `.md` file.
- **File → Open Folder** opens a folder in the sidebar so you can browse and switch files.
- **File → Reopen Closed File** (`Ctrl+Shift+T`, `⌘+Shift+T` on macOS) reopens the most recently closed file, including its editing mode, selection and reading position. Each window remembers up to 32 closed files until it exits. Reopening reads the latest disk content; discarded unsaved changes are not restored.
- **Preferences → Files → Open files in tabs by default** is enabled by default. Files opened from the file manager or command line use tabs in the existing window; turn it off to open a new window instead. **Open in New Window** and the command-line `--new-window` option explicitly request a separate window.

## Two editing modes

- **Render mode** (the default) shows a live, formatted page — type Markdown, see the result.
- **Source mode** shows the raw Markdown text. Toggle it from the **View** menu.

## Write your first document

Type Markdown and it is formatted instantly. A few essentials:

| You type | You get |
| --- | --- |
| `# Heading` | A level‑1 heading |
| `**bold**` / `*italic*` | **bold** / *italic* |
| `- item` | A bullet list |
| `` `code` `` | Inline `code` |
| `[link](https://example.com)` | A clickable link |

Insert tables, math (`$E=mc^2$`), footnotes, and fenced code blocks — Muffin renders all of GitHub‑Flavored Markdown.

## Search scope

**Preferences → Search** configures folder content search and `Ctrl+P` Quick Open independently of file opening protection. Set the allowed extensions, ignored files and folders, hidden-file visibility, and whether to read `.gitignore` and `.ignore` in the search root and its subfolders. Changes apply to the next search.

Write one ignore rule per line, such as `build/`, `*.bak`, or `/drafts/`. Rules support `*`, `?`, `**`, character classes, and `!` exceptions. `.ignore` takes priority over `.gitignore`; preference rules take priority over both. An ignored directory is not traversed, so its parent directories must be allowed before an exception inside it can take effect. Use `*` in the extensions field to search all file types, or leave it empty to search no files. Binary content and unsupported encodings are still skipped.

Content search defaults to 100 MiB per file, with an additional 32 MiB limit for regular expressions. Both are configurable up to 512 MiB. File size does not limit Quick Open; folder replacement retains a separate 8 MiB limit. **Search details** explains ignored items and files skipped because of size, permissions, or encoding.

## Export presets and repeat export

In **Preferences → Export**, use **New** to create a named preset based on PDF, HTML, an image, a Pandoc format or a **Custom Command**. Each preset has independent options. You can duplicate, rename, reorder or remove presets; the original format defaults remain available. Run a preset from **File → Export → Export Presets**.

After a successful export, **Export with Previous Settings** uses the same settings and lets you choose another destination. **Export and Overwrite Previous** writes directly to the last successful destination. Both export the current document content, including unsaved edits. The last successful settings and resolved theme are retained separately for each open tab until it closes. Failure or cancellation preserves the existing output and the previous successful export record. These commands can also be assigned shortcuts in **Preferences → Keyboard Shortcuts**.

For a custom command, set the executable, arguments and output extension (without a leading dot). The command must create a single file at `${outputPath}`. Variables in arguments:

| Variable | Value |
| --- | --- |
| `${inputPath}` | Temporary UTF-8 Markdown copy, including unsaved edits |
| `${outputPath}` | Temporary output path; Muffin commits it to the chosen destination after success |
| `${currentPath}` | Current document path, empty for an unnamed document |
| `${currentFolder}` | Document folder, or the home directory for an unnamed document |
| `${fileName}` | Document name without its extension |

For example, with Pandoc selected as the executable: `"${inputPath}" -o "${outputPath}" -t docx`. Quote arguments containing spaces. Variables are passed literally and no shell is started automatically. Custom commands export one file; use a Pandoc preset for built-in format options such as templates and a table of contents.

## Tips

- In live editing, paste browser or office HTML to keep headings, emphasis, lists, links and tables. Spreadsheet cells copied as tab-separated text become a Markdown table. Use **Edit → Paste as Plain Text** to keep only text; code blocks and source mode always paste literal text.
- Copying in live editing provides HTML for rich editors and Markdown for Muffin. **Preferences → Editor** controls the plain-text representation sent to plain-text apps.
- Use **Preferences → Keyboard Shortcuts** to search commands, record a key combination, resolve conflicts or restore defaults. Changes apply immediately and are saved for future sessions.
- Use **Edit → Lock Editing** (default `F12`) when reading or proofreading to prevent accidental changes. Selection, copying, finding and switching tabs remain available; run the command again to unlock. Choose per-tab or whole-window locking in **Preferences → Editor → Locked Editing**, and customize the shortcut in **Keyboard Shortcuts**.
- In **Preferences → Export → PDF**, configure the header, footer, author and page numbers. Templates support `${title}`, `${author}`, `${fileName}`, `${date}`, `${pageNo}` and `${pageCount}`.
- Press the formatting and paragraph entries in the menus to style text without remembering syntax.
- **Edit → Find / Replace** searches the current document.
- Pick a look under **Theme** — Muffin ships light, dark and "Whitey" themes.

For the full syntax reference, see the [Markdown Reference](help:markdown-reference).

To see who makes Muffin possible, read the [Acknowledgements](help:acknowledgements).
