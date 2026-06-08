#pragma once
#include <vector>
#include <cstddef>
#include <stdexcept>
#include <algorithm>

// ============================================================================
// Vertente 2 — RingBuffer (Buffer Circular) — O(1)
// ----------------------------------------------------------------------------
// Requisito do enunciado: classe propria, com indices head/tail, operacoes
// O(1) e SEM funcoes de movimentacao de memoria em massa.
//
// REGRA DE OURO (verificavel): este arquivo NAO contem erase / insert(begin) /
// memmove / realloc. O avanco de indices e feito SOMENTE por aritmetica
// modular: idx = (idx + 1) % capacity.
//
// Alocacao: UMA unica vez, no construtor (data_ com capacidade fixa). Nunca
// realoca durante a operacao.
//
// Politica de overflow: SOBRESCREVER O MAIS ANTIGO (telemetria prioriza dados
// recentes). O trade-off esta documentado no relatorio.
// ============================================================================
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : data_(capacity), capacity_(capacity) {
        if (capacity == 0)
            throw std::invalid_argument("RingBuffer: capacidade deve ser > 0");
    }

    // O(1). Retorna true se inseriu em espaco livre; false se sobrescreveu o
    // elemento mais antigo (buffer cheio).
    bool push(const T& item) {
        bool overwrote = false;
        if (count_ == capacity_) {
            tail_ = (tail_ + 1) % capacity_; // descarta o mais antigo
            overwrote = true;
            ++overwrites_;
        } else {
            ++count_;
        }
        data_[head_] = item;
        head_ = (head_ + 1) % capacity_;
        return !overwrote;
    }

    // O(1). Remove o elemento mais antigo.
    bool pop(T& out) {
        if (count_ == 0) return false;
        out = data_[tail_];
        tail_ = (tail_ + 1) % capacity_;
        --count_;
        return true;
    }

    // O(k) — dreno em lote (k = min(size, maxItems)). Anexa em `out`.
    size_t popBatch(std::vector<T>& out, size_t maxItems) {
        size_t n = std::min(count_, maxItems);
        for (size_t i = 0; i < n; ++i) {
            out.push_back(data_[tail_]);
            tail_ = (tail_ + 1) % capacity_;
        }
        count_ -= n;
        return n;
    }

    size_t size()     const { return count_; }
    size_t capacity() const { return capacity_; }
    bool   empty()    const { return count_ == 0; }
    bool   full()     const { return count_ == capacity_; }
    size_t overwrites() const { return overwrites_; } // diagnostico de saturacao

private:
    std::vector<T> data_;     // capacidade fixa, alocada UMA vez no construtor
    size_t capacity_;
    size_t head_  = 0;        // proxima posicao de escrita
    size_t tail_  = 0;        // proxima posicao de leitura
    size_t count_ = 0;        // elementos atualmente armazenados
    size_t overwrites_ = 0;   // quantas vezes sobrescreveu o mais antigo
};
