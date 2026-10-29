#include "aiws/gui/term_inspector_widget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {

const QString kNoValue = QStringLiteral("—");

}  // namespace

TermInspectorWidget::TermInspectorWidget(QWidget* parent) : QWidget(parent) {
    auto* panel = new QFrame(this);
    panel->setFrameShape(QFrame::StyledPanel);

    auto* title = new QLabel("Term Inspector:", panel);
    title->setStyleSheet("font-weight: bold;");

    term_edit_ = new QLineEdit(panel);
    term_edit_->setPlaceholderText("Enter a term");

    inspect_button_ = new QPushButton("Inspect", panel);

    document_frequency_value_ = new QLabel(kNoValue, panel);
    document_frequency_value_->setStyleSheet("font-weight: bold;");
    chunk_frequency_value_ = new QLabel(kNoValue, panel);
    chunk_frequency_value_->setStyleSheet("font-weight: bold;");

    auto* termRow = new QHBoxLayout;
    termRow->addWidget(new QLabel("Term:", panel));
    termRow->addWidget(term_edit_, 1);
    termRow->addWidget(inspect_button_);

    auto* statsRow = new QHBoxLayout;
    statsRow->addWidget(new QLabel("Document Frequency:", panel));
    statsRow->addWidget(document_frequency_value_);
    statsRow->addStretch(1);
    statsRow->addWidget(new QLabel("Selected Chunk TF:", panel));
    statsRow->addWidget(chunk_frequency_value_);

    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->addWidget(title);
    panelLayout->addLayout(termRow);
    panelLayout->addLayout(statsRow);
    panelLayout->addStretch(1);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(panel);

    connect(inspect_button_, &QPushButton::clicked,
            this, &TermInspectorWidget::request_inspection);

    set_inspection_enabled(false);
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    // the old TF was for a different chunk, so stop showing it
    if (chunk_id != selected_chunk_id_) {
        chunk_frequency_value_->setText(kNoValue);
    }
    selected_chunk_id_ = chunk_id;
}

void TermInspectorWidget::set_statistics(std::size_t document_frequency,
                                         bool has_chunk_frequency,
                                         std::size_t chunk_frequency) {
    document_frequency_value_->setText(QString::number(document_frequency));
    if (has_chunk_frequency) {
        chunk_frequency_value_->setText(QString::number(chunk_frequency));
    } else {
        chunk_frequency_value_->setText(kNoValue);
    }
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText(kNoValue);
    chunk_frequency_value_->setText(kNoValue);
}

void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    set_inspection_enabled(false);
}

void TermInspectorWidget::request_inspection() {
    const QString term = term_edit_->text().trimmed();
    if (term.isEmpty()) {
        return;
    }
    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui
