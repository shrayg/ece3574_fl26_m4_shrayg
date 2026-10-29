#include "aiws/gui/search_result_model.hpp"

#include <QString>

#include <utility>

namespace aiws::gui {

namespace {

enum Column {
    RankColumn,
    DocumentColumn,
    ChunkColumn,
    ScoreColumn,
    TextColumn,
    ColumnCount
};

}  // namespace

SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int SearchResultModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(results_.size());
}

int SearchResultModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) {
        return {};
    }

    const SearchResult* result = result_at(index.row());
    if (result == nullptr) {
        return {};
    }

    switch (index.column()) {
    case RankColumn:
        return index.row() + 1;
    case DocumentColumn:
        return QString::fromStdString(result->document_id);
    case ChunkColumn:
        return static_cast<qulonglong>(result->chunk_sequence);
    case ScoreColumn:
        return QString::number(result->score, 'f', 6);
    case TextColumn:
        return QString::fromStdString(result->text);
    default:
        return {};
    }
}

QVariant SearchResultModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case RankColumn:
        return QString("Rank");
    case DocumentColumn:
        return QString("Document");
    case ChunkColumn:
        return QString("Chunk");
    case ScoreColumn:
        return QString("Score");
    case TextColumn:
        return QString("Text");
    default:
        return {};
    }
}

void SearchResultModel::set_results(std::vector<SearchResult> results) {
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
}

void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
}

const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    if (row < 0 || row >= static_cast<int>(results_.size())) {
        return nullptr;
    }
    return &results_[static_cast<std::size_t>(row)];
}

}  // namespace aiws::gui
