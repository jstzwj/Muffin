#include "export/ExportPreset.h"
#include "export/ExportOptions.h"
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>

namespace muffin {

QString exportExtension(ExportFormat format) {
  switch (format) {
  case ExportFormat::Pdf:
    return ".pdf";
  case ExportFormat::Html:
  case ExportFormat::HtmlPlain:
    return ".html";
  case ExportFormat::Image:
    return ".png";
  case ExportFormat::Docx:
    return ".docx";
  case ExportFormat::Odt:
    return ".odt";
  case ExportFormat::Rtf:
    return ".rtf";
  case ExportFormat::Epub:
    return ".epub";
  case ExportFormat::Latex:
    return ".tex";
  case ExportFormat::MediaWiki:
    return ".wiki";
  case ExportFormat::Rst:
    return ".rst";
  case ExportFormat::Textile:
    return ".textile";
  case ExportFormat::Opml:
    return ".opml";
  case ExportFormat::Command:
    return ".txt";
  }
  return {};
}

QVariantMap defaultExportValues(ExportFormat format) {
  QVariantMap values{{"theme", "current"},
                     {"fontSize", 16},
                     {"paper", "A4"},
                     {"width", format == ExportFormat::Image ? 960 : 210},
                     {"height", 297},
                     {"margin", 15},
                     {"scale", 1},
                     {"wrap", "none"},
                     {"tocDepth", 3},
                     {"extension", "txt"},
                     {"openDirectory", QSettings().value("export/openAfterExport", false)}};
  return values;
}

ExportPreset builtInExportPreset(ExportFormat format) {
  ExportPreset preset;
  preset.format = format;
  preset.options = defaultExportValues(format);
  QSettings settings;
  settings.beginGroup(exportSettingsPrefix(format));
  for (const auto& key : settings.childKeys())
    preset.options.insert(key, settings.value(key));
  return preset;
}

QVector<ExportPreset> exportPresets() {
  QVector<ExportPreset> result;
  QSet<QString> ids;
  for (const auto& entry : QSettings().value("export/presets").toList()) {
    const auto map = entry.toMap();
    const int format = map.value("format", -1).toInt();
    const auto id = map.value("id").toString();
    const auto name = map.value("name").toString().trimmed();
    if (id.isEmpty() || ids.contains(id) || name.isEmpty() || format < 0 || format > int(ExportFormat::Command))
      continue;
    ids.insert(id);
    result.append({id, name, ExportFormat(format), map.value("options").toMap()});
    if (result.size() == 64)
      break;
  }
  return result;
}

void saveExportPresets(const QVector<ExportPreset>& presets) {
  QVariantList entries;
  for (const auto& preset : presets)
    entries.append(QVariantMap{
        {"id", preset.id}, {"name", preset.name}, {"format", int(preset.format)}, {"options", preset.options}});
  QSettings().setValue("export/presets", entries);
}

QString exportPresetExtension(const ExportPreset& preset) {
  if (preset.format != ExportFormat::Command)
    return exportExtension(preset.format);
  const auto extension = preset.options.value("extension", "txt").toString().trimmed();
  static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,15}$"));
  return valid.match(extension).hasMatch() ? "." + extension : QString();
}

bool exportCommandArguments(const ExportPreset& preset, const QVariantMap& variables, QString* program,
                            QStringList* arguments) {
  *program = preset.options.value("program").toString().trimmed();
  const auto text = preset.options.value("arguments").toString();
  if (program->isEmpty() || program->contains("${") || !text.contains("${outputPath}") ||
      exportPresetExtension(preset).isEmpty())
    return false;
  *arguments = QProcess::splitCommand(text);
  static const QRegularExpression variable(QStringLiteral("\\$\\{([A-Za-z]+)\\}"));
  for (auto& argument : *arguments) {
    QString expanded;
    qsizetype cursor = 0;
    auto matches = variable.globalMatch(argument);
    while (matches.hasNext()) {
      const auto match = matches.next();
      if (!variables.contains(match.captured(1)))
        return false;
      expanded += argument.mid(cursor, match.capturedStart() - cursor) + variables.value(match.captured(1)).toString();
      cursor = match.capturedEnd();
    }
    argument = expanded + argument.mid(cursor);
  }
  return true;
}

} // namespace muffin
