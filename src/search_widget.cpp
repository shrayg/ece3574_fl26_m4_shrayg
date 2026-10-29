#include "aiws/gui/search_widget.hpp"

#include "aiws/gui/search_result_model.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace aiws::gui {

namespace {

constexpr int kMinResults = 1;
constexpr int kMaxResults = 100;
constexpr int kStartResults = 3;

int selectedRow(const QTableView* view) {
    const QModelIndexList rows = view->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return -1;
    }
    return rows.first().row();
}

}  // namespace

SearchWidget::SearchWidget(QWidget* parent) : QWidget(parent) {
    auto* panel = new QFrame(this);
    panel->setFrameShape(QFrame::StyledPanel);

    auto* queryTitle = new QLabel("Query:", panel);
    queryTitle->setStyleSheet("font-weight: bold;");

    query_edit_ = new QLineEdit(panel);
    query_edit_->setPlaceholderText("Search the current corpus");

    result_count_spin_ = new QSpinBox(panel);
    result_count_spin_->setRange(kMinResults, kMaxResults);
    result_count_spin_->setValue(kStartResults);

    search_button_ = new QPushButton("Search", panel);

    result_model_ = new SearchResultModel(this);
    results_view_ = new QTableView(panel);
    results_view_->setModel(result_model_);
    results_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_view_->verticalHeader()->hide();
    results_view_->setColumnWidth(0, 50);
    results_view_->setColumnWidth(1, 180);
    results_view_->setColumnWidth(2, 60);
    results_view_->setColumnWidth(3, 90);
    results_view_->horizontalHeader()->setStretchLastSection(true);

    auto* resultTitle = new QLabel("Selected Result:", panel);
    resultTitle->setStyleSheet("font-weight: bold;");

    view_source_button_ = new QPushButton("View Source", panel);

    selected_result_text_ = new QPlainTextEdit(panel);
    selected_result_text_->setReadOnly(true);
    selected_result_text_->setPlaceholderText("Select a result to inspect its details.");

    auto* queryRow = new QHBoxLayout;
    queryRow->addWidget(queryTitle);
    queryRow->addWidget(query_edit_, 1);
    queryRow->addWidget(new QLabel("Results:", panel));
    queryRow->addWidget(result_count_spin_);
    queryRow->addWidget(search_button_);

    auto* resultRow = new QHBoxLayout;
    resultRow->addWidget(resultTitle);
    resultRow->addStretch(1);
    resultRow->addWidget(view_source_button_);

    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->addLayout(queryRow);
    panelLayout->addWidget(results_view_, 3);
    panelLayout->addLayout(resultRow);
    panelLayout->addWidget(selected_result_text_, 2);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(panel);

    connect(search_button_, &QPushButton::clicked,
            this, &SearchWidget::request_search);
    connect(query_edit_, &QLineEdit::textChanged,
            this, &SearchWidget::query_changed);
    // selectionModel() only exists after setModel()
    connect(results_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SearchWidget::show_selected_result);
    connect(view_source_button_, &QPushButton::clicked,
            this, &SearchWidget::request_source_document);

    set_search_enabled(false);
    view_source_button_->setEnabled(false);
}

QString SearchWidget::query() const {
    return query_edit_->text();
}

int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

bool SearchWidget::has_query() const {
    return !query().trimmed().isEmpty();
}

void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);
}

void SearchWidget::set_results(std::vector<SearchResult> results) {
    clear_results();
    result_model_->set_results(std::move(results));
}

void SearchWidget::clear_results() {
    results_view_->clearSelection();
    result_model_->clear();
    selected_result_text_->clear();
    view_source_button_->setEnabled(false);
    emit selected_chunk_changed(QString());
}

void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(kStartResults);
    clear_results();
    set_search_enabled(false);
}

void SearchWidget::request_search() {
    emit search_requested(query(), result_count());
}

void SearchWidget::show_selected_result() {
    const int row = selectedRow(results_view_);
    const SearchResult* result = result_model_->result_at(row);
    if (result == nullptr) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    QStringList lines;
    lines << "Rank: " + QString::number(row + 1);
    lines << "Document: " + QString::fromStdString(result->document_id);
    lines << "Chunk: " + QString::number(result->chunk_sequence)
                 + " (" + QString::fromStdString(result->chunk_id) + ")";
    lines << "Score: " + QString::number(result->score, 'f', 6);
    lines << "Matched terms: " + QString::number(result->matched_terms);
    lines << "";
    lines << QString::fromStdString(result->text);
    selected_result_text_->setPlainText(lines.join('\n'));

    view_source_button_->setEnabled(true);
    emit selected_chunk_changed(QString::fromStdString(result->chunk_id));
}

void SearchWidget::request_source_document() {
    const SearchResult* result = result_model_->result_at(selectedRow(results_view_));
    if (result == nullptr) {
        return;
    }
    emit source_document_requested(QString::fromStdString(result->document_id));
}

}  // namespace aiws::gui
