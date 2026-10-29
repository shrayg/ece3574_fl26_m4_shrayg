#include "aiws/gui/workspace_widget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {

const QString kNotBuilt = QStringLiteral("Not built");
const QString kSelectedTitle = QStringLiteral("Selected Document -");

}  // namespace

WorkspaceWidget::WorkspaceWidget(QWidget* parent) : QWidget(parent) {
    // left side: document list and corpus controls
    auto* workspacePanel = new QFrame(this);
    workspacePanel->setFrameShape(QFrame::StyledPanel);

    auto* workspaceTitle = new QLabel("Workspace", workspacePanel);
    workspaceTitle->setStyleSheet("font-weight: bold;");

    add_document_button_ = new QPushButton("Add Document", workspacePanel);
    build_corpus_button_ = new QPushButton("Build Corpus", workspacePanel);

    document_list_ = new QListWidget(workspacePanel);
    document_list_->setSelectionMode(QAbstractItemView::SingleSelection);

    corpus_status_label_ = new QLabel(kNotBuilt, workspacePanel);
    corpus_status_label_->setStyleSheet("font-weight: bold;");

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(workspaceTitle);
    buttonRow->addWidget(add_document_button_);
    buttonRow->addWidget(build_corpus_button_);
    buttonRow->addStretch(1);

    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget(new QLabel("Corpus:", workspacePanel));
    statusRow->addWidget(corpus_status_label_);
    statusRow->addStretch(1);

    auto* workspaceLayout = new QVBoxLayout(workspacePanel);
    workspaceLayout->addLayout(buttonRow);
    workspaceLayout->addWidget(document_list_, 1);
    workspaceLayout->addLayout(statusRow);

    // right side: the selected document
    auto* documentPanel = new QFrame(this);
    documentPanel->setFrameShape(QFrame::StyledPanel);

    document_title_label_ = new QLabel(kSelectedTitle, documentPanel);
    document_title_label_->setStyleSheet("font-weight: bold;");
    document_title_label_->setWordWrap(true);

    document_text_ = new QPlainTextEdit(documentPanel);
    document_text_->setReadOnly(true);
    document_text_->setPlaceholderText("Select a document to view its contents.");

    auto* documentLayout = new QVBoxLayout(documentPanel);
    documentLayout->addWidget(document_title_label_);
    documentLayout->addWidget(document_text_, 1);

    auto* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(workspacePanel, 1);
    outerLayout->addWidget(documentPanel, 1);

    connect(add_document_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::add_document_requested);
    connect(build_corpus_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::build_corpus_requested);
    connect(document_list_, &QListWidget::currentRowChanged,
            this, &WorkspaceWidget::document_selected);

    set_build_corpus_enabled(false);
}

void WorkspaceWidget::add_document(const QString& title) {
    document_list_->addItem(title);
}

void WorkspaceWidget::select_document(int row) {
    if (row < 0 || row >= document_list_->count()) {
        return;
    }
    document_list_->setCurrentRow(row);
    document_list_->scrollToItem(document_list_->item(row));
}

void WorkspaceWidget::show_selected_document(const QString& title,
                                              const QString& text) {
    document_title_label_->setText(kSelectedTitle + " " + title);
    document_text_->setPlainText(text);
}

void WorkspaceWidget::clear_selected_document() {
    document_title_label_->setText(kSelectedTitle);
    document_text_->clear();
}

void WorkspaceWidget::set_corpus_status(const QString& text) {
    corpus_status_label_->setText(text);
}

void WorkspaceWidget::set_build_corpus_enabled(bool enabled) {
    build_corpus_button_->setEnabled(enabled);
}

void WorkspaceWidget::reset() {
    document_list_->clear();
    clear_selected_document();
    set_corpus_status(kNotBuilt);
    set_build_corpus_enabled(false);
}

}  // namespace aiws::gui
