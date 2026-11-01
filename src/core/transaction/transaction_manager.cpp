// =============================================================================
// transaction_manager.cpp
// TODO(core/transaction): implementar two-phase locking com tabela de locks
// em memoria, deteccao de deadlock por timeout e, na proxima iteracao,
// um log de undo/redo para suportar rollback apos crash.
// =============================================================================
#include "nexodb/transaction_manager.hpp"

namespace nexodb::transaction {

// Implementacao concreta fica aqui. Mantido como stub ate o storage engine
// (core/storage) estar funcional, pois transacoes dependem dele.

}  // namespace nexodb::transaction
