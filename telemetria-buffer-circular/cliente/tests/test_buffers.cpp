// Testes unitarios minimos (sem dependencias externas).
// Cobrem: push/pop, wrap-around, overflow (sobrescrita), FIFO, popBatch
// e a janela correta da Vertente 1 (SHIFT/REALLOC).
#include "RingBuffer.hpp"
#include "DynamicBuffer.hpp"
#include "Sample.hpp"

#include <cstdio>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, msg)                                                      \
    do {                                                                       \
        if (!(cond)) { std::printf("  [FALHA] %s\n", msg); ++g_fail; }         \
        else         { std::printf("  [ok]    %s\n", msg); }                   \
    } while (0)

static void test_ring_fifo() {
    std::printf("RingBuffer: push/pop FIFO\n");
    RingBuffer<int> rb(4);
    CHECK(rb.empty(), "comeca vazio");
    rb.push(1); rb.push(2); rb.push(3);
    CHECK(rb.size() == 3, "size==3 apos 3 pushes");
    int v;
    CHECK(rb.pop(v) && v == 1, "pop devolve 1 (mais antigo)");
    CHECK(rb.pop(v) && v == 2, "pop devolve 2");
    CHECK(rb.size() == 1, "size==1");
}

static void test_ring_wrap() {
    std::printf("RingBuffer: wrap-around\n");
    RingBuffer<int> rb(3);
    rb.push(1); rb.push(2); rb.push(3); // cheio
    int v; rb.pop(v);                    // remove 1 -> tail avanca
    rb.push(4);                          // escreve em posicao reaproveitada
    CHECK(rb.pop(v) && v == 2, "FIFO mantido apos wrap (2)");
    CHECK(rb.pop(v) && v == 3, "FIFO mantido apos wrap (3)");
    CHECK(rb.pop(v) && v == 4, "FIFO mantido apos wrap (4)");
    CHECK(rb.empty(), "vazio ao final");
}

static void test_ring_overflow() {
    std::printf("RingBuffer: overflow sobrescreve o mais antigo\n");
    RingBuffer<int> rb(3);
    rb.push(1); rb.push(2); rb.push(3);
    bool ok = rb.push(4); // cheio -> sobrescreve o 1
    CHECK(!ok, "push em buffer cheio retorna false");
    CHECK(rb.overwrites() == 1, "contou 1 sobrescrita");
    CHECK(rb.size() == 3, "size permanece na capacidade");
    int v;
    CHECK(rb.pop(v) && v == 2, "mais antigo agora e 2 (1 foi descartado)");
}

static void test_ring_batch() {
    std::printf("RingBuffer: popBatch\n");
    RingBuffer<int> rb(8);
    for (int i = 0; i < 6; ++i) rb.push(i);
    std::vector<int> out;
    size_t n = rb.popBatch(out, 4);
    CHECK(n == 4, "drenou 4 itens");
    CHECK(out.size() == 4 && out[0] == 0 && out[3] == 3, "ordem FIFO no lote");
    CHECK(rb.size() == 2, "restam 2");
    n = rb.popBatch(out, 10);
    CHECK(n == 2, "drenou os 2 restantes (maxItems > size)");
}

static void test_dyn_shift() {
    std::printf("DynamicBuffer SHIFT: janela deslizante\n");
    DynamicBuffer db(3, DynMode::SHIFT);
    for (uint64_t i = 0; i < 5; ++i) {
        Sample s{i, double(i), i};
        db.insert(s);
    }
    CHECK(db.size() == 3, "janela limitada a 3");
    const auto& w = db.window();
    CHECK(w.front().seq == 2 && w.back().seq == 4, "janela contem [2,3,4]");
}

static void test_dyn_realloc() {
    std::printf("DynamicBuffer REALLOC: cresce sem limite\n");
    DynamicBuffer db(3, DynMode::REALLOC);
    for (uint64_t i = 0; i < 10; ++i) {
        Sample s{i, double(i), i};
        db.insert(s);
    }
    CHECK(db.size() == 10, "mantem TODAS as amostras (anti-padrao)");
    CHECK(db.latest().seq == 9, "ultima amostra e a 9");
}

int main() {
    std::printf("==== Testes de buffers ====\n");
    test_ring_fifo();
    test_ring_wrap();
    test_ring_overflow();
    test_ring_batch();
    test_dyn_shift();
    test_dyn_realloc();
    std::printf("===========================\n");
    if (g_fail == 0) { std::printf("TODOS OS TESTES PASSARAM\n"); return 0; }
    std::printf("%d TESTE(S) FALHARAM\n", g_fail);
    return 1;
}
