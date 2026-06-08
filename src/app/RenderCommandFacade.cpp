#include "app/RenderCommandFacade.h"

#include "editor/EditorController.h"

#include <QLoggingCategory>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(renderCommandLog, "muffin.renderCommands", QtWarningMsg)

}  // namespace

RenderCommandFacade::RenderCommandFacade(EditorController& editorController, SourceModeFn sourceMode)
    : editorController_(editorController), sourceMode_(std::move(sourceMode)) {}

bool RenderCommandFacade::canRun() const {
  return !sourceMode_ || !sourceMode_();
}

bool RenderCommandFacade::runCommand(const char* commandName, const std::function<bool()>& command) const {
  if (!canRun()) {
    return false;
  }
  if (command()) {
    return true;
  }
  qCWarning(renderCommandLog).nospace() << "Render command failed: " << commandName;
  return false;
}

bool RenderCommandFacade::hasCurrentTableCell() const {
  return editorController_.tableController().currentCell().isValid();
}

bool RenderCommandFacade::isOnEditableParagraphBlock() const {
  return editorController_.paragraphController().isOnEditableBlock();
}

int RenderCommandFacade::currentHeadingLevel() const {
  return editorController_.paragraphController().currentHeadingLevel();
}

bool RenderCommandFacade::isInCodeBlock() const {
  return editorController_.codeFenceController().currentCodeFenceId().isValid();
}

bool RenderCommandFacade::isEditingCodeBlock() const {
  return editorController_.codeFenceController().isEditing();
}

bool RenderCommandFacade::isInHtmlBlock() const {
  return editorController_.htmlLiteral().currentBlockId().isValid();
}

bool RenderCommandFacade::isEditingHtmlBlock() const {
  return editorController_.htmlLiteral().isEditing();
}

bool RenderCommandFacade::isInMathBlock() const {
  return editorController_.mathLiteral().currentBlockId().isValid();
}

bool RenderCommandFacade::isEditingMathBlock() const {
  return editorController_.mathLiteral().isEditing();
}

bool RenderCommandFacade::insertTable(int rows, int columns) {
  return runCommand("insertTable", [&] { return editorController_.tableController().insertTable(rows, columns); });
}

bool RenderCommandFacade::resizeCurrentTable(int rows, int columns) {
  return runCommand("resizeCurrentTable", [&] { return editorController_.tableController().resizeCurrentTable(rows, columns); });
}

bool RenderCommandFacade::insertRowBefore() {
  return runCommand("insertRowBefore", [&] { return editorController_.tableController().insertRowBefore(); });
}

bool RenderCommandFacade::insertRowAfter() {
  return runCommand("insertRowAfter", [&] { return editorController_.tableController().insertRowAfter(); });
}

bool RenderCommandFacade::deleteCurrentRow() {
  return runCommand("deleteCurrentRow", [&] { return editorController_.tableController().deleteCurrentRow(); });
}

bool RenderCommandFacade::moveCurrentRowUp() {
  return runCommand("moveCurrentRowUp", [&] { return editorController_.tableController().moveCurrentRowUp(); });
}

bool RenderCommandFacade::moveCurrentRowDown() {
  return runCommand("moveCurrentRowDown", [&] { return editorController_.tableController().moveCurrentRowDown(); });
}

bool RenderCommandFacade::insertColumnBefore() {
  return runCommand("insertColumnBefore", [&] { return editorController_.tableController().insertColumnBefore(); });
}

bool RenderCommandFacade::insertColumnAfter() {
  return runCommand("insertColumnAfter", [&] { return editorController_.tableController().insertColumnAfter(); });
}

bool RenderCommandFacade::deleteCurrentColumn() {
  return runCommand("deleteCurrentColumn", [&] { return editorController_.tableController().deleteCurrentColumn(); });
}

bool RenderCommandFacade::moveCurrentColumnLeft() {
  return runCommand("moveCurrentColumnLeft", [&] { return editorController_.tableController().moveCurrentColumnLeft(); });
}

bool RenderCommandFacade::moveCurrentColumnRight() {
  return runCommand("moveCurrentColumnRight", [&] { return editorController_.tableController().moveCurrentColumnRight(); });
}

bool RenderCommandFacade::setCurrentColumnAlignment(TableAlignment alignment) {
  return runCommand("setCurrentColumnAlignment", [&] { return editorController_.tableController().setCurrentColumnAlignment(alignment); });
}

bool RenderCommandFacade::copyCurrentTable() {
  return runCommand("copyCurrentTable", [&] { return editorController_.tableController().copyCurrentTable(); });
}

bool RenderCommandFacade::formatCurrentTableSource() {
  return runCommand("formatCurrentTableSource", [&] { return editorController_.tableController().formatCurrentTableSource(); });
}

bool RenderCommandFacade::deleteCurrentTable() {
  return runCommand("deleteCurrentTable", [&] { return editorController_.tableController().deleteCurrentTable(); });
}

bool RenderCommandFacade::insertFrontMatter(FrontMatterFormat format) {
  return runCommand("insertFrontMatter", [&] { return editorController_.insertFrontMatter(format); });
}

bool RenderCommandFacade::setHeadingLevel(int level) {
  return runCommand("setHeadingLevel", [&] { return editorController_.paragraphController().setHeadingLevel(level); });
}

bool RenderCommandFacade::promoteHeading() {
  return runCommand("promoteHeading", [&] { return editorController_.paragraphController().promoteHeading(); });
}

bool RenderCommandFacade::demoteHeading() {
  return runCommand("demoteHeading", [&] { return editorController_.paragraphController().demoteHeading(); });
}

bool RenderCommandFacade::toggleFormulaBlock() {
  return runCommand("toggleFormulaBlock", [&] { return editorController_.paragraphController().toggleFormulaBlock(); });
}

bool RenderCommandFacade::toggleCodeBlock() {
  return runCommand("toggleCodeBlock", [&] { return editorController_.paragraphController().toggleCodeBlock(); });
}

bool RenderCommandFacade::insertParagraphBefore() {
  return runCommand("insertParagraphBefore", [&] { return editorController_.paragraphController().insertParagraphBefore(); });
}

bool RenderCommandFacade::insertParagraphAfter() {
  return runCommand("insertParagraphAfter", [&] { return editorController_.paragraphController().insertParagraphAfter(); });
}

bool RenderCommandFacade::insertLinkReference() {
  return runCommand("insertLinkReference", [&] { return editorController_.paragraphController().insertLinkReference(); });
}

bool RenderCommandFacade::insertFootnoteDefinition() {
  return runCommand("insertFootnoteDefinition", [&] { return editorController_.paragraphController().insertFootnoteDefinition(); });
}

bool RenderCommandFacade::insertHorizontalRule() {
  return runCommand("insertHorizontalRule", [&] { return editorController_.paragraphController().insertHorizontalRule(); });
}

bool RenderCommandFacade::toggleQuote() {
  return runCommand("toggleQuote", [&] { return editorController_.paragraphController().toggleQuote(); });
}

bool RenderCommandFacade::convertToOrderedList() {
  return runCommand("convertToOrderedList", [&] { return editorController_.paragraphController().convertToOrderedList(); });
}

bool RenderCommandFacade::convertToUnorderedList() {
  return runCommand("convertToUnorderedList", [&] { return editorController_.paragraphController().convertToUnorderedList(); });
}

bool RenderCommandFacade::convertToTaskList() {
  return runCommand("convertToTaskList", [&] { return editorController_.paragraphController().convertToTaskList(); });
}

bool RenderCommandFacade::enterCodeEditMode() {
  return runCommand("enterCodeEditMode", [&] { return editorController_.codeFenceController().enterEditMode(); });
}

bool RenderCommandFacade::exitCodeEditMode() {
  return runCommand("exitCodeEditMode", [&] { return editorController_.codeFenceController().exitEditMode(); });
}

bool RenderCommandFacade::setCodeLanguage(QString language) {
  return runCommand("setCodeLanguage", [&] { return editorController_.codeFenceController().setLanguage(std::move(language)); });
}

bool RenderCommandFacade::setCodeLanguageFor(NodeId codeId, QString language) {
  return runCommand("setCodeLanguageFor", [&] { return editorController_.codeFenceController().setLanguageFor(codeId, std::move(language)); });
}

bool RenderCommandFacade::enterHtmlEditMode() {
  return runCommand("enterHtmlEditMode", [&] { return editorController_.htmlLiteral().enterEditMode(); });
}

bool RenderCommandFacade::exitHtmlEditMode() {
  return runCommand("exitHtmlEditMode", [&] { return editorController_.htmlLiteral().exitEditMode(); });
}

bool RenderCommandFacade::setHtmlSource(QString html) {
  return runCommand("setHtmlSource", [&] { return editorController_.htmlLiteral().setContent(std::move(html)); });
}

bool RenderCommandFacade::enterMathEditMode() {
  return runCommand("enterMathEditMode", [&] { return editorController_.mathLiteral().enterEditMode(); });
}

bool RenderCommandFacade::exitMathEditMode() {
  return runCommand("exitMathEditMode", [&] { return editorController_.mathLiteral().exitEditMode(); });
}

bool RenderCommandFacade::setMathTex(QString tex) {
  return runCommand("setMathTex", [&] { return editorController_.mathLiteral().setContent(std::move(tex)); });
}

}  // namespace muffin
