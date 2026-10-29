#include "aiws/gui/context_widget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QStringList>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {

constexpr int kMinBudget = 50;
constexpr int kMaxBudget = 1000;
constexpr int kBudgetStep = 50;
constexpr int kStartBudget = 300;

}  // namespace

ContextWidget::ContextWidget(QWidget* parent) : QWidget(parent) {
    auto* panel = new QFrame(this);
    panel->setFrameShape(QFrame::StyledPanel);

    auto* title = new QLabel("Context:", panel);
    title->setStyleSheet("font-weight: bold;");

    context_budget_slider_ = new QSlider(Qt::Horizontal, panel);
    context_budget_slider_->setRange(kMinBudget, kMaxBudget);
    context_budget_slider_->setSingleStep(kBudgetStep);
    context_budget_slider_->setPageStep(kBudgetStep);
    context_budget_slider_->setTickInterval(kBudgetStep);

    budget_value_label_ = new QLabel(panel);
    budget_value_label_->setMinimumWidth(40);
    budget_value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    build_context_button_ = new QPushButton("Build Context", panel);

    context_text_ = new QPlainTextEdit(panel);
    context_text_->setReadOnly(true);
    context_text_->setPlaceholderText("Built context will appear here.");

    auto* budgetRow = new QHBoxLayout;
    budgetRow->addWidget(new QLabel("Token budget:", panel));
    budgetRow->addWidget(context_budget_slider_, 1);
    budgetRow->addWidget(budget_value_label_);
    budgetRow->addWidget(build_context_button_);

    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->addWidget(title);
    panelLayout->addLayout(budgetRow);
    panelLayout->addWidget(context_text_, 1);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(panel);

    connect(context_budget_slider_, &QSlider::valueChanged,
            this, &ContextWidget::budget_changed);
    connect(build_context_button_, &QPushButton::clicked,
            this, &ContextWidget::request_context);

    context_budget_slider_->setValue(kStartBudget);
    budget_changed(context_budget_slider_->value());
    set_build_context_enabled(false);
}

int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
}

void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
}

void ContextWidget::set_context(const std::vector<ContextItem>& items) {
    if (items.empty()) {
        context_text_->setPlainText("No context was returned for this query.");
        return;
    }

    QStringList blocks;
    for (const ContextItem& item : items) {
        QStringList lines;
        lines << "Document: " + QString::fromStdString(item.document_id);
        lines << "Chunk: " + QString::number(item.chunk_sequence);
        lines << "Tokens: " + QString::number(item.token_count);
        lines << "Score: " + QString::number(item.score, 'f', 6);
        lines << QString("Truncated: ") + (item.truncated ? "yes" : "no");
        lines << "Text: " + QString::fromStdString(item.text);
        blocks << lines.join('\n');
    }
    context_text_->setPlainText(blocks.join("\n\n"));
}

void ContextWidget::clear_context() {
    context_text_->clear();
}

void ContextWidget::reset() {
    context_budget_slider_->setValue(kStartBudget);
    clear_context();
    set_build_context_enabled(false);
}

void ContextWidget::request_context() {
    emit build_context_requested(token_budget());
}

void ContextWidget::budget_changed(int value) {
    // dragging can land between steps, so snap back to a multiple of 50
    const int snapped = (value + kBudgetStep / 2) / kBudgetStep * kBudgetStep;
    if (snapped != value) {
        context_budget_slider_->setValue(snapped);
        return;
    }
    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui
