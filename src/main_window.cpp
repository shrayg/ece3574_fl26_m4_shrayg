#include "aiws/gui/main_window.hpp"

#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/resettable.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiws::gui {

namespace {

QString countText(std::size_t count, const QString& word) {
    return QString("%1 %2%3").arg(count).arg(word).arg(count == 1 ? "" : "s");
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("AI Workspace");
    resize(900, 900);

    auto* central = new QWidget(this);
    setCentralWidget(central);

    // header
    auto* header = new QFrame(central);
    header->setObjectName("header");
    header->setStyleSheet("#header { background-color: #861F41; }"
                          "#header QLabel { color: white; }");

    auto* appTitle = new QLabel("AI Workspace", header);
    appTitle->setStyleSheet("font-size: 22px; font-weight: bold;");
    auto* appSubtitle = new QLabel("Document Search and Context Explorer", header);
    appSubtitle->setStyleSheet("font-style: italic;");

    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(14, 8, 14, 8);
    headerLayout->setSpacing(2);
    headerLayout->addWidget(appTitle);
    headerLayout->addWidget(appSubtitle);

    // main components
    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ = new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);

    auto* bottomRow = new QHBoxLayout;
    bottomRow->addWidget(term_inspector_widget_, 1);
    bottomRow->addWidget(context_widget_, 1);

    // status, reset, and exit
    auto* statusTitle = new QLabel("Current status:", central);
    statusTitle->setStyleSheet("font-weight: bold;");
    status_label_ = new QLabel(central);

    reset_button_ = new QPushButton("Reset", central);
    reset_button_->setStyleSheet(
        "QPushButton { background-color: #E5A823; color: #1A1A1A; font-weight: bold;"
        " border-radius: 4px; padding: 6px 22px; }"
        "QPushButton:pressed { background-color: #C98F14; }");

    exit_button_ = new QPushButton("Exit", central);
    exit_button_->setStyleSheet(
        "QPushButton { background-color: #2F6DB5; color: white; font-weight: bold;"
        " border-radius: 4px; padding: 6px 22px; }"
        "QPushButton:pressed { background-color: #23548C; }");

    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget(statusTitle);
    statusRow->addWidget(status_label_, 1);
    statusRow->addWidget(reset_button_);
    statusRow->addWidget(exit_button_);

    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->addWidget(header);
    mainLayout->addWidget(workspace_widget_, 3);
    mainLayout->addWidget(search_widget_, 4);
    mainLayout->addLayout(bottomRow, 2);
    mainLayout->addLayout(statusRow);

    connect(workspace_widget_, &WorkspaceWidget::add_document_requested,
            this, &MainWindow::add_document);
    connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested,
            this, &MainWindow::build_corpus);
    connect(workspace_widget_, &WorkspaceWidget::document_selected,
            this, &MainWindow::document_selected);

    connect(search_widget_, &SearchWidget::search_requested,
            this, &MainWindow::search_requested);
    connect(search_widget_, &SearchWidget::query_changed,
            this, &MainWindow::query_changed);
    connect(search_widget_, &SearchWidget::source_document_requested,
            this, &MainWindow::source_document_requested);
    // the inspector only needs to remember the chunk, so this skips MainWindow
    connect(search_widget_, &SearchWidget::selected_chunk_changed,
            term_inspector_widget_, &TermInspectorWidget::set_selected_chunk);

    connect(term_inspector_widget_, &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);
    connect(context_widget_, &ContextWidget::build_context_requested,
            this, &MainWindow::build_context_requested);

    connect(reset_button_, &QPushButton::clicked,
            this, &MainWindow::reset_application);
    connect(exit_button_, &QPushButton::clicked,
            this, &MainWindow::exit_application);

    status_label_->setText("Ready");
    update_action_states();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Add Document", QString(), "Text files (*.txt);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    const QString documentId = info.absoluteFilePath();
    const QString title = info.fileName();

    for (const Document& document : workspace_.documents()) {
        if (document.id() == documentId.toStdString()) {
            status_label_->setText(title + " is already in the workspace.");
            return;
        }
    }

    QFile file(documentId);
    if (!file.open(QIODevice::ReadOnly)) {
        status_label_->setText("Could not open " + title + ": " + file.errorString());
        return;
    }
    const QString text = QString::fromUtf8(file.readAll());

    // backend first: adding the list row can select it, which reads workspace_
    workspace_.add_document(
        Document(documentId.toStdString(), title.toStdString(), text.toStdString()));
    workspace_widget_->add_document(title);
    invalidate_corpus();

    status_label_->setText("Added " + title + ".");
}

void MainWindow::build_corpus() {
    if (workspace_.documents().empty()) {
        return;
    }

    try {
        processing_core_.rebuild(workspace_);
    } catch (const std::exception& error) {
        status_label_->setText(QString("Build failed: ") + error.what());
        return;
    }

    corpus_current_ = true;
    clear_processing_outputs();
    workspace_widget_->set_corpus_status(countText(processing_core_.chunk_count(), "chunk"));
    status_label_->setText("Corpus built.");
    update_action_states();
}

void MainWindow::document_selected(int row) {
    const auto& documents = workspace_.documents();
    if (row < 0 || static_cast<std::size_t>(row) >= documents.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }

    const Document& document = documents[static_cast<std::size_t>(row)];
    workspace_widget_->show_selected_document(QString::fromStdString(document.title()),
                                              QString::fromStdString(document.text()));
}

void MainWindow::search_requested(const QString& query, int k) {
    if (!corpus_current_ || query.trimmed().isEmpty()) {
        return;
    }

    std::vector<SearchResult> results;
    try {
        results = processing_core_.search(query.toStdString(), k);
    } catch (const std::exception& error) {
        status_label_->setText(QString("Search failed: ") + error.what());
        return;
    }

    const std::size_t resultCount = results.size();
    search_widget_->set_results(std::move(results));
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();

    status_label_->setText("Search returned " + countText(resultCount, "result") + ".");
    update_action_states();
}

void MainWindow::query_changed(const QString&) {
    update_action_states();
}

void MainWindow::build_context_requested(int token_budget) {
    if (!corpus_current_ || !search_widget_->has_query() || token_budget <= 0) {
        return;
    }

    std::vector<ContextItem> items;
    try {
        items = processing_core_.build_context(search_widget_->query().toStdString(),
                                               search_widget_->result_count(),
                                               static_cast<std::size_t>(token_budget));
    } catch (const std::exception& error) {
        status_label_->setText(QString("Build context failed: ") + error.what());
        return;
    }

    context_widget_->set_context(items);
    status_label_->setText("Built context with " + countText(items.size(), "item") + ".");
}

void MainWindow::source_document_requested(const QString& document_id) {
    const std::string targetId = document_id.toStdString();
    const auto& documents = workspace_.documents();
    for (std::size_t row = 0; row < documents.size(); ++row) {
        if (documents[row].id() == targetId) {
            workspace_widget_->select_document(static_cast<int>(row));
            status_label_->setText("Showing source: "
                                   + QString::fromStdString(documents[row].title()) + ".");
            return;
        }
    }
    status_label_->setText("Source document is not in the workspace.");
}

void MainWindow::term_inspection_requested(const QString& term,
                                           const QString& chunk_id) {
    if (!corpus_current_ || term.trimmed().isEmpty()) {
        return;
    }

    try {
        const std::string termText = term.toStdString();
        const std::size_t documentFrequency = processing_core_.document_frequency(termText);
        const bool hasChunk = !chunk_id.isEmpty();
        const std::size_t chunkFrequency =
            hasChunk ? processing_core_.term_frequency(termText, chunk_id.toStdString()) : 0;
        term_inspector_widget_->set_statistics(documentFrequency, hasChunk, chunkFrequency);
        status_label_->setText("Inspected \"" + term + "\".");
    } catch (const std::invalid_argument&) {
        term_inspector_widget_->clear_statistics();
        status_label_->setText("Inspect takes a single term, like \"retrieval\".");
    }
}

void MainWindow::reset_application() {
    workspace_.clear();
    processing_core_.rebuild(workspace_);
    corpus_current_ = false;

    const std::array<Resettable*, 4> parts{
        workspace_widget_, search_widget_, term_inspector_widget_, context_widget_};
    for (Resettable* part : parts) {
        part->reset();
    }

    status_label_->setText("Application reset.");
    update_action_states();
}

void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
}

void MainWindow::update_action_states() {
    const bool hasDocuments = !workspace_.documents().empty();
    const bool canQuery = corpus_current_ && search_widget_->has_query();

    workspace_widget_->set_build_corpus_enabled(hasDocuments);
    search_widget_->set_search_enabled(canQuery);
    context_widget_->set_build_context_enabled(canQuery);
    term_inspector_widget_->set_inspection_enabled(corpus_current_);
}

void MainWindow::invalidate_corpus() {
    if (corpus_current_) {
        workspace_widget_->set_corpus_status("Out of date");
    }
    corpus_current_ = false;
    clear_processing_outputs();
    update_action_states();
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

}  // namespace aiws::gui
