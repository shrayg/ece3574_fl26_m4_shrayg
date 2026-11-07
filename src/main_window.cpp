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
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiws::gui {

namespace {

constexpr int kPollIntervalMs = 20;

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
    status_label_->setObjectName("statusLabel");

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

    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(kPollIntervalMs);
    connect(pollTimer_, &QTimer::timeout, this, &MainWindow::pollBuild);

    status_label_->setText("Ready");
    update_action_states();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

void MainWindow::set_build_delay_ms(int ms_per_document) {
    buildDelayMs_ = std::max(0, ms_per_document);
}

void MainWindow::set_core_factory(BuildJob::CoreFactory factory) noexcept {
    coreFactory_ = factory;
}

void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Add Document", QString(), "Text files (*.txt);;All files (*)");
    if (path.isEmpty()) {
        return;
    }
    add_document_file(path);
}

bool MainWindow::add_document_file(const QString& path) {
    const QFileInfo info(path);
    const QString documentId = info.absoluteFilePath();
    const QString title = info.fileName();

    for (const Document& document : workspace_.documents()) {
        if (document.id() == documentId.toStdString()) {
            status_label_->setText(title + " is already in the workspace.");
            return false;
        }
    }

    QFile file(documentId);
    if (!file.open(QIODevice::ReadOnly)) {
        status_label_->setText("Could not open " + title + ": " + file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        status_label_->setText("Could not read " + title + ": " + file.errorString());
        return false;
    }
    const QString text = QString::fromUtf8(bytes);

    // backend first: adding the list row can select it, which reads workspace_
    workspace_.add_document(
        Document(documentId.toStdString(), title.toStdString(), text.toStdString()));
    workspace_widget_->add_document(title);
    invalidate_corpus();

    status_label_->setText("Added " + title + ".");
    return true;
}

void MainWindow::remote_add_requested(const QString& path) {
    add_document_file(path);
}

void MainWindow::build_corpus() {
    if (workspace_.documents().empty() || buildJob_ != nullptr) {
        return;
    }
    startBuild();
}

void MainWindow::startBuild() {
    try {
        auto job = std::make_unique<BuildJob>(workspaceVersion_);
        job->start(workspace_, buildDelayMs_, coreFactory_);
        buildJob_ = std::move(job);
    } catch (const std::exception& error) {
        setCorpusState(CorpusState::Failed);
        status_label_->setText(QString("Build failed: ") + error.what());
        return;
    }

    processingCore_.reset();
    clear_processing_outputs();
    setCorpusState(CorpusState::Building);
    status_label_->setText("Building corpus...");
    pollTimer_->start();
}

void MainWindow::pollBuild() {
    if (buildJob_ == nullptr) {
        pollTimer_->stop();
        return;
    }
    if (buildJob_->isDone()) {
        finishBuild();
    }
}

void MainWindow::finishBuild() {
    pollTimer_->stop();
    const std::unique_ptr<BuildJob> job = std::move(buildJob_);

    std::unique_ptr<ProcessingCore> core;
    try {
        core = job->takeResult();
    } catch (const std::exception& error) {
#ifdef AIWS_FAULT_LOSE_FAILURE
        return;
#endif
        if (job->version() == workspaceVersion_) {
            setCorpusState(CorpusState::Failed);
            status_label_->setText(QString("Build failed: ") + error.what());
        } else {
            update_action_states();
        }
        return;
    }

    bool isStale = job->version() != workspaceVersion_;
#ifdef AIWS_FAULT_ACCEPT_STALE
    isStale = false;
#endif
    if (core == nullptr || isStale) {
        // the workspace changed while this was building, state was already updated then
        update_action_states();
        return;
    }

    processingCore_ = std::move(core);
    setCorpusState(CorpusState::Current);
    status_label_->setText("Corpus built.");
}

void MainWindow::setCorpusState(CorpusState state) {
    corpusState_ = state;
    switch (state) {
    case CorpusState::NotBuilt:
        workspace_widget_->set_corpus_status("Not built");
        break;
    case CorpusState::Building:
        workspace_widget_->set_corpus_status("Building...");
        break;
    case CorpusState::Current:
        workspace_widget_->set_corpus_status(
            countText(processingCore_->chunk_count(), "chunk"));
        break;
    case CorpusState::OutOfDate:
        workspace_widget_->set_corpus_status("Out of date");
        break;
    case CorpusState::Failed:
        workspace_widget_->set_corpus_status("Build failed");
        break;
    }
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
    if (corpusState_ != CorpusState::Current || query.trimmed().isEmpty()) {
        return;
    }

    std::vector<SearchResult> results;
    try {
        results = processingCore_->search(query.toStdString(), k);
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
    if (corpusState_ != CorpusState::Current || !search_widget_->has_query()
        || token_budget <= 0) {
        return;
    }

    std::vector<ContextItem> items;
    try {
        items = processingCore_->build_context(search_widget_->query().toStdString(),
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
    if (corpusState_ != CorpusState::Current || term.trimmed().isEmpty()) {
        return;
    }

    try {
        const std::string termText = term.toStdString();
        const std::size_t documentFrequency = processingCore_->document_frequency(termText);
        const bool hasChunk = !chunk_id.isEmpty();
        const std::size_t chunkFrequency =
            hasChunk ? processingCore_->term_frequency(termText, chunk_id.toStdString()) : 0;
        term_inspector_widget_->set_statistics(documentFrequency, hasChunk, chunkFrequency);
        status_label_->setText("Inspected \"" + term + "\".");
    } catch (const std::invalid_argument&) {
        term_inspector_widget_->clear_statistics();
        status_label_->setText("Inspect takes a single term, like \"retrieval\".");
    }
}

void MainWindow::reset_application() {
    // a running build keeps going until it notices the cancel, pollBuild drops it later
    ++workspaceVersion_;
    if (buildJob_ != nullptr) {
        buildJob_->cancel();
    }
    workspace_.clear();
    processingCore_.reset();

    const std::array<Resettable*, 4> parts{
        workspace_widget_, search_widget_, term_inspector_widget_, context_widget_};
    for (Resettable* part : parts) {
        part->reset();
    }

    setCorpusState(CorpusState::NotBuilt);
    status_label_->setText("Application reset.");
}

void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
}

void MainWindow::update_action_states() {
    const bool hasDocuments = !workspace_.documents().empty();
    const bool isCurrent = corpusState_ == CorpusState::Current;
    const bool canQuery = isCurrent && search_widget_->has_query();

    workspace_widget_->set_build_corpus_enabled(hasDocuments && buildJob_ == nullptr);
    search_widget_->set_search_enabled(canQuery);
    context_widget_->set_build_context_enabled(canQuery);
    term_inspector_widget_->set_inspection_enabled(isCurrent);
}

void MainWindow::invalidate_corpus() {
    ++workspaceVersion_;
    if (buildJob_ != nullptr) {
        buildJob_->cancel();
    }
    processingCore_.reset();
    clear_processing_outputs();

    if (corpusState_ == CorpusState::NotBuilt) {
        update_action_states();
    } else {
        setCorpusState(CorpusState::OutOfDate);
    }
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

}  // namespace aiws::gui
