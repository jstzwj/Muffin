#pragma once

#include "document/MarkdownTypes.h"
#include "document/NodeId.h"

#include <QString>

#include <functional>

namespace muffin {

struct CursorFormatState {
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool code = false;
  bool strikethrough = false;
  bool inlineMath = false;
};

class EditorController;

class RenderCommandFacade final {
public:
  using SourceModeFn = std::function<bool()>;

  RenderCommandFacade(EditorController& editorController, SourceModeFn sourceMode);

  bool hasCurrentTableCell() const;
  bool isOnEditableParagraphBlock() const;
  bool canInsertParagraphAround() const;
  bool isInlineFormatEnabled() const;
  int currentHeadingLevel() const;
  bool isInCodeBlock() const;
  bool isEditingCodeBlock() const;
  bool isInHtmlBlock() const;
  bool isEditingHtmlBlock() const;
  bool isInMathBlock() const;
  bool isEditingMathBlock() const;

  bool insertTable(int rows = 2, int columns = 2);
  bool resizeCurrentTable(int rows, int columns);
  bool insertRowBefore();
  bool insertRowAfter();
  bool deleteCurrentRow();
  bool moveCurrentRowUp();
  bool moveCurrentRowDown();
  bool insertColumnBefore();
  bool insertColumnAfter();
  bool deleteCurrentColumn();
  bool moveCurrentColumnLeft();
  bool moveCurrentColumnRight();
  bool setCurrentColumnAlignment(TableAlignment alignment);
  bool copyCurrentTable();
  bool formatCurrentTableSource();
  bool deleteCurrentTable();

  bool insertFrontMatter(FrontMatterFormat format);
  bool setHeadingLevel(int level);
  bool promoteHeading();
  bool demoteHeading();
  bool toggleFormulaBlock();
  bool toggleCodeBlock();
  bool insertParagraphBefore();
  bool insertParagraphAfter();
  bool insertLinkReference();
  bool insertFootnoteDefinition();
  bool insertHorizontalRule();
  bool toggleQuote();
  bool convertToOrderedList();
  bool convertToUnorderedList();
  bool convertToTaskList();

  bool enterCodeEditMode();
  bool exitCodeEditMode();
  bool setCodeLanguage(QString language);
  bool setCodeLanguageFor(NodeId codeId, QString language);

  bool enterHtmlEditMode();
  bool exitHtmlEditMode();
  bool setHtmlSource(QString html);

  bool enterMathEditMode();
  bool exitMathEditMode();
  bool setMathTex(QString tex);

  bool insertImage();
  bool isOnImage() const;
  QString imageSrcAtCursor() const;
  bool imageSourceRangeAtCursor(qsizetype& outStart, qsizetype& outEnd) const;

  CursorFormatState currentInlineFormats() const;

private:
  bool canRun() const;
  bool runCommand(const char* commandName, const std::function<bool()>& command) const;

  EditorController& editorController_;
  SourceModeFn sourceMode_;
};

}  // namespace muffin
