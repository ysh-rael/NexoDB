// =============================================================================
// transaction_manager.hpp
// Controle de concorrencia e transacoes. Garante isolamento entre multiplas
// conexoes/threads que acessam o mesmo arquivo de banco simultaneamente,
// usando bloqueios em nivel de documento/colecao (e, futuramente, MVCC para
// reduzir contencao de leitura).
// =============================================================================
#pragma once

#include <cstdint>

#include "nexodb/common.hpp"

namespace nexodb::transaction {

enum class LockMode { kShared, kExclusive };

using TransactionId = uint64_t;

// Interface do gerenciador de transacoes/locks. A implementacao concreta
// decide a politica de bloqueio (ex.: two-phase locking) e a estrategia de
// deteccao/resolucao de deadlock (ex.: timeout + wait-for graph).
class TransactionManager {
 public:
  virtual ~TransactionManager() = default;

  virtual TransactionId Begin() = 0;
  virtual common::Status Commit(TransactionId tx) = 0;
  virtual common::Status Rollback(TransactionId tx) = 0;

  // Bloqueia um recurso (documento ou colecao) identificado por resource_id
  // no modo solicitado, respeitando um timeout para evitar deadlock.
  virtual common::Status Lock(TransactionId tx, uint64_t resource_id,
                               LockMode mode) = 0;
};

}  // namespace nexodb::transaction
