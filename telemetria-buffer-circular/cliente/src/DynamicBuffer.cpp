#include "DynamicBuffer.hpp"
#include <stdexcept>

DynamicBuffer::DynamicBuffer(size_t windowSize, DynMode mode)
    : mode_(mode), windowSize_(windowSize ? windowSize : 1) {}

DynamicBuffer::~DynamicBuffer() {
    delete[] raw_;
}

void DynamicBuffer::insert(const Sample& s) {
    if (mode_ == DynMode::SHIFT) {
        // ANTI-PADRAO A: ao encher a janela, remove o primeiro elemento
        // deslocando TODOS os demais -> custo Theta(janela) por insercao.
        data_.push_back(s);
        if (data_.size() > windowSize_) {
            data_.erase(data_.begin()); // <-- movimentacao de memoria em massa
        }
    } else {
        // ANTI-PADRAO B: "realloc" manual a cada amostra. Aloca um novo bloco
        // com +1 posicao, copia tudo e libera o antigo. Custo O(n) por insercao,
        // crescimento ILIMITADO e N alocacoes (contadas pelo Profiler via new[]).
        Sample* novo = new Sample[rawSize_ + 1];
        for (size_t i = 0; i < rawSize_; ++i) novo[i] = raw_[i];
        novo[rawSize_] = s;
        delete[] raw_;
        raw_ = novo;
        ++rawSize_;
    }
}

size_t DynamicBuffer::size() const {
    return (mode_ == DynMode::SHIFT) ? data_.size() : rawSize_;
}

Sample DynamicBuffer::latest() const {
    if (mode_ == DynMode::SHIFT) {
        if (data_.empty()) throw std::runtime_error("DynamicBuffer vazio");
        return data_.back();
    }
    if (rawSize_ == 0) throw std::runtime_error("DynamicBuffer vazio");
    return raw_[rawSize_ - 1];
}
