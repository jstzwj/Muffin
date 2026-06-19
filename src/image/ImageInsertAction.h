#pragma once

namespace muffin {

// The "When Inserting Images" preference (Image preferences page, Card 1). The
// integer stored under QSettings key `image/insertAction` is cast directly to
// this enum, so the combobox item order must match these enumerator values.
enum class ImageInsertAction {
  None = 0,                  // 无特殊操作 — use the source path as-is.
  CopyToCurrentFolder = 1,   // 复制图片到当前文件夹 (./)
  CopyToAssets = 2,          // 复制图片到 ./assets 文件夹
  CopyToFilenameAssets = 3,  // 复制图片到 ./<filename>.assets 文件夹
  Upload = 4,                // 上传图片 — hand the file to the upload service.
  CopyToCustomFolder = 5,    // 复制到指定路径 — image/customFolder.
};

}  // namespace muffin
