#include "aiws/processing_core.hpp"

#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/corpus_index.hpp"
#include "aiws/retrieval_engine.hpp"
#include "aiws/text_processor.hpp"

#include <iterator>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace aiws {

struct ProcessingCore::Impl {
    std::vector<Chunk> chunks;
    CorpusIndex index;
    std::unique_ptr<ChunkingStrategy> chunking;
    std::unique_ptr<RetrievalStrategy> retrieval;
    std::unique_ptr<ContextStrategy> context;
};

ProcessingCore::ProcessingCore()
    : ProcessingCore(
          std::make_unique<Chunker>(
              ChunkingPolicy{kMaxChunkTokens, kChunkOverlap,
                             kParagraphPreferenceWindow}),
          std::make_unique<RetrievalEngine>(),
          std::make_unique<ContextBuilder>()) {}

ProcessingCore::ProcessingCore(std::unique_ptr<ChunkingStrategy> chunking,
                               std::unique_ptr<RetrievalStrategy> retrieval,
                               std::unique_ptr<ContextStrategy> context) {
    if (!chunking || !retrieval || !context) {
        throw std::invalid_argument("strategies must not be null");
    }

    impl_ = std::make_unique<Impl>();
    impl_->chunking = std::move(chunking);
    impl_->retrieval = std::move(retrieval);
    impl_->context = std::move(context);
}

ProcessingCore::~ProcessingCore() = default;
ProcessingCore::ProcessingCore(ProcessingCore&&) noexcept = default;
ProcessingCore& ProcessingCore::operator=(ProcessingCore&&) noexcept = default;

std::string ProcessingCore::normalize(const std::string& text) {
    return TextProcessor::normalize(text);
}

void ProcessingCore::rebuild(const Workspace& workspace) {
    std::unordered_set<std::string> documentIds;
    std::vector<Chunk> nextChunks;
    for (std::size_t documentOrder = 0;
         documentOrder < workspace.documents().size(); ++documentOrder) {
        const auto& document = workspace.documents()[documentOrder];
        if (!documentIds.insert(document.id()).second) {
            throw std::invalid_argument("duplicate document id: " +
                                        document.id());
        }
        auto producedChunks =
            impl_->chunking->chunk(document, documentOrder);
        nextChunks.insert(nextChunks.end(),
                          std::make_move_iterator(producedChunks.begin()),
                          std::make_move_iterator(producedChunks.end()));
    }
    CorpusIndex nextIndex(nextChunks);
    impl_->chunks = std::move(nextChunks);
    impl_->index = std::move(nextIndex);
}

const std::vector<Chunk>& ProcessingCore::chunks() const noexcept { return impl_->chunks; }
std::size_t ProcessingCore::chunk_count() const noexcept { return impl_->chunks.size(); }

std::size_t ProcessingCore::document_frequency(const std::string& term) const {
    const auto terms = TextProcessor::terms(term);
    if (terms.empty()) return 0;
    if (terms.size() != 1) throw std::invalid_argument("term must normalize to one token");
    return impl_->index.document_frequency(terms.front());
}

std::size_t ProcessingCore::term_frequency(const std::string& term,
                                           const std::string& chunkId) const {
    const auto terms = TextProcessor::terms(term);
    if (terms.empty()) return 0;
    if (terms.size() != 1) throw std::invalid_argument("term must normalize to one token");
    return impl_->index.term_frequency(terms.front(), chunkId);
}

std::vector<SearchResult> ProcessingCore::search(const std::string& query, int k) const {
    return impl_->retrieval->search(query, k, impl_->chunks, impl_->index);
}

std::vector<ContextItem> ProcessingCore::build_context(const std::string& query,
                                                       int k,
                                                       std::size_t tokenBudget) const {
    return impl_->context->build(search(query, k), tokenBudget);
}

}  // namespace aiws
