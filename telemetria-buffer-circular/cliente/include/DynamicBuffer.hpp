#pragma once
#include "Sample.hpp"
#include <vector>
#include <cstddef>

// Sub-variantes do anti-padrao da Vertente 1.
//   SHIFT   - mantem uma janela deslocando elementos (erase no inicio): O(janela)
//   REALLOC - "realloc" manual a cada amostra (cresce 1 em 1): O(n) + N alocacoes
enum class DynMode { SHIFT, REALLOC };

// ============================================================================
// Vertente 1 — DynamicBuffer (ANTI-PADRAO) — O(n)
// ----------------------------------------------------------------------------
// Reproduz explicitamente o problema do enunciado. NAO otimizar: o objetivo e
// demonstrar o custo O(n) por insercao e a explosao de alocacoes.
// ============================================================================
class DynamicBuffer {
public:
    explicit DynamicBuffer(size_t windowSize, DynMode mode = DynMode::SHIFT);
    ~DynamicBuffer();

    void insert(const Sample& s);             // O(n) — vide as duas variantes

    const std::vector<Sample>& window() const { return data_; } // valido p/ SHIFT
    size_t size() const;
    Sample latest() const;
    DynMode mode() const { return mode_; }

private:
    DynMode mode_;
    size_t  windowSize_;

    // Variante SHIFT
    std::vector<Sample> data_;

    // Variante REALLOC (ponteiro cru que cresce sem limite — fragmentacao)
    Sample* raw_     = nullptr;
    size_t  rawSize_ = 0;
};
