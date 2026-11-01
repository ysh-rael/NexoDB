# NexoDB - Changelog

Todas as mudanças notáveis neste projeto serão documentadas neste arquivo.

O formato é baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.0.0/),
e este projeto adere ao [Versionamento Semântico](https://semver.org/lang/pt-BR/).

---

## [0.1.0-dev] - 2026-11-01

### 📦 Sobre o Projeto

**NexoDB** é um banco de dados documental embarcado (estilo SQLite), desenvolvido em **C++20**, com integração nativa ao **Node.js via Node-API (N-API)**. O objetivo é fornecer uma solução leve e eficiente para armazenamento e consulta de documentos JSON, com API compatível com **Mongoose**, sem custo constante de RAM.

- **Nome:** NexoDB
- **Descrição:** Banco de dados documental embarcado baseado no modelo MongoDB
- **Linguagem:** C++20 (core) + JavaScript (API pública)
- **Plataforma:** Node.js 24+ via Node-API (Windows, Linux, macOS | x64, arm64)
- **Licença:** MIT
- **Repositório:** https://github.com/yshrael/NexoDB
- **Status:** Em desenvolvimento (fase de scaffold — ver `docs/ROADMAP.md`)

---

### ✨ Funcionalidades Implementadas (Estrutura/Scaffold)

#### 🏗️ Core C++ (include/nexodb/ + src/core/)

| Módulo | Arquivo(s) | Descrição | Status |
|--------|------------|-----------|--------|
| **Common** | `common.hpp`, `logger.cpp` | Status/Result types, Logger com níveis (DEBUG/INFO/WARN/ERROR) | ✅ Implementado |
| **Document** | `document.hpp` | Modelo de documento (Document, Value, ObjectId, serialização binária tipo-BSON) | 📝 Interface definida |
| **Storage Engine** | `storage_engine.hpp` | Persistência em disco: páginas, cache LRU configurável, WAL | 📝 Interface definida |
| **Index Manager** | `index_manager.hpp` | Índices secundários B-Tree sobre campos de documentos | 📝 Interface definida |
| **Transaction Manager** | `transaction_manager.hpp` | Controle de concorrência, locking (shared/exclusive), detecção de deadlock | 📝 Interface definida |
| **Query Engine** | `query_engine.hpp` | Parser de filtros (`$eq`, `$gt`, `$lt`, `$in`, etc.) e updates (`$set`, `$unset`, `$inc`) | 📝 Interface definida |
| **Aggregation Pipeline** | `aggregation_pipeline.hpp` | Pipeline de agregação (`$match`, `$group`, `$sort`, `$project`, `$limit`) | 📝 Interface definida |
| **JSON I/O** | `json_io.hpp` | Import/Export JSON/NDJSON (JsonImporter, JsonExporter) | 📝 Interface definida |
| **SQL I/O** | `sql_io.hpp` | Import/Export de/para dumps SQL (SqlImporter, SqlExporter) | 📝 Interface definida |
| **Backup Manager** | `backup_manager.hpp` | Backup/restauração consistente (hot backup com WAL) | 📝 Interface definida |

#### 🔗 Binding Node-API (src/napi/)

| Arquivo | Descrição | Status |
|---------|-----------|--------|
| `module.cpp` | Ponto de entrada do addon nativo (`nexodb_native.node`) | ✅ Implementado |
| `database_binding.hpp/cpp` | Classe `Database` exposta ao JS (métodos CRUD, índices, backup, agregação) | 📝 Stub/Interface |

#### 📚 API JavaScript Estilo Mongoose (lib/)

| Arquivo | Classe/Função | Métodos Implementados | Status |
|---------|---------------|----------------------|--------|
| `index.js` | `connect()`, `Connection`, `Schema`, `model`, `models` | Entry point público | ✅ Implementado |
| `Connection.js` | `Connection` | `constructor(options)`, `backup()`, `restore()`, `close()` | ✅ Implementado |
| `Schema.js` | `Schema` | `constructor(definition, options)`, `index(fields, opts)`, `get indexes()` | ✅ Implementado |
| `Model.js` | `Model` | Ver seção "Métodos do Model" abaixo | ✅ Scaffold completo |
| `Query.js` | `Query` | Chainable query builder (filter, sort, limit, skip, select, lean, exec) | ✅ Scaffold completo |
| `Document.js` | `NexoDocument` | Wrapper de documento com `_id`, `toObject()`, `save()`, `remove()` | ✅ Scaffold completo |
| `ModelRegistry.js` | `model()`, `models` | Registro por conexão (WeakMap) | ✅ Implementado |
| `NativeBinding.js` | `loadNative()` | Carregamento isolado do addon `.node` (Release/Debug) | ✅ Implementado |

---

### 🔧 Métodos do Model (API Mongoose-Compatible)

Todos os métodos abaixo têm **assinatura e comportamento idênticos ao Mongoose** (conforme `lib/Model.js`):

#### 📖 Leitura
| Método | Assinatura | Descrição |
|--------|------------|-----------|
| `find` | `find(filter = {})` | Retorna Query para busca múltipla |
| `findOne` | `findOne(filter = {})` | Retorna Query para busca única |
| `findById` | `findById(id)` | Busca por `_id` |
| `count` | `count(filter = {})` | Contagem estimada (usa metadados) |
| `countDocuments` | `countDocuments(filter = {})` | Contagem exata (varre documentos) |

#### ✏️ Atualização
| Método | Assinatura | Descrição |
|--------|------------|-----------|
| `findOneAndUpdate` | `findOneAndUpdate(filter, update, options = {})` | Atualiza e retorna documento |
| `findByIdAndUpdate` | `findByIdAndUpdate(id, update, options = {})` | Atualiza por ID e retorna |
| `updateOne` | `updateOne(filter, update)` | Atualiza primeiro que match |
| `updateMany` | `updateMany(filter, update)` | Atualiza todos que match |
| `replaceOne` | `replaceOne(filter, replacement)` | Substitui documento inteiro |

#### 🗑️ Remoção
| Método | Assinatura | Descrição |
|--------|------------|-----------|
| `deleteOne` | `deleteOne(filter)` | Remove primeiro que match |
| `findOneAndRemove` | `findOneAndRemove(filter)` | Remove e retorna documento |
| `deleteMany` | `deleteMany(filter)` | Remove todos que match |
| `remove` | `remove(filter)` | Alias de `deleteMany` (legado) |
| `findOneAndDelete` | `findOneAndDelete(filter)` | Remove e retorna (novo) |
| `findOneAndReplace` | `findOneAndReplace(filter, replacement)` | Substitui e retorna |

#### 📊 Agregação
| Método | Assinatura | Descrição |
|--------|------------|-----------|
| `aggregate` | `aggregate(pipeline = [])` | Pipeline de agregação MongoDB-style |

#### ➕ Criação
| Método | Assinatura | Descrição |
|--------|------------|-----------|
| `create` | `create(data)` | Insere novo documento (async) |

---

### 🛠️ Ferramentas de Desenvolvimento

| Comando | Descrição |
|---------|-----------|
| `npm install` | Instala dependências e compila addon nativo (cmake-js) |
| `npm run build` | Recompila addon (Release) |
| `npm run build:debug` | Recompila addon (Debug) |
| `npm test` | Executa testes JS (node:test) |
| `npm run test:cpp` | Executa testes C++ (GoogleTest, requer `-DNEXODB_BUILD_TESTS=ON`) |
| `npm run bench` | Executa benchmarks (`benchmark/run-all.js`) |
| `npm run lint` | Lint código JS (ESLint) |
| `npm run format:cpp` | Formata código C++ (clang-format) |

---

### 📁 Estrutura do Projeto

```
include/nexodb/       Headers públicos do core (contratos entre módulos)
src/core/
  common/             Status/Result, Logger — utilitários sem dependências
  document/           Modelo de documento (Document, Value, ObjectId)
  storage/            Persistência: páginas, cache LRU, WAL
  index/              Índices B-Tree sobre campos
  transaction/        Locking e controle de concorrência/transações
  query/              Parser de filtros/updates + execução
  aggregation/        Pipeline de agregação ($match, $group, $sort...)
  io/json/            Import/export JSON/NDJSON
  io/sql/             Import/export SQL
  backup/             Backup e restauração
src/napi/             Binding Node-API (classe `Database` exposta ao JS)
lib/                  API JS estilo Mongoose (Schema, Model, Query...)
test/cpp/             Testes C++ (GoogleTest)
test/js/              Testes JS (node:test)
benchmark/            Cenários de carga e coleta de métricas
docs/                 Arquitetura e roadmap
```

---

### 🎯 Próximos Passos (ver `docs/ROADMAP.md`)

1. **core/storage** — Gerenciador de páginas + cache LRU + leitura/escrita de documentos
2. **core/document** — Serialização binária eficiente (formato tipo-BSON)
3. **core/query** (básico) — Parser de filtros/updates + varredura completa
4. **src/napi** — Ligar `QueryEngine` real aos métodos stub no binding
5. **core/index** — B-Tree + uso pelo `QueryEngine`
6. **core/transaction** — Locking para múltiplas conexões simultâneas
7. **core/aggregation** — Estágios do `aggregate()`
8. **core/io/json** e **core/io/sql** — Import/export funcionais
9. **core/backup** — Backup/restore consistentes
10. **benchmark/** — Habilitar cenários e estabelecer baseline no CI

---

### 📝 Notas da Versão

> **Importante:** Esta versão `0.1.0-dev` é um **scaffold completo** — toda a estrutura de pastas, interfaces públicas (`include/nexodb/*.hpp`), binding Node-API e API JavaScript estilo Mongoose já existem e compilam. No entanto, a **maior parte da lógica do core ainda está marcada com `TODO`** nos arquivos `.cpp` correspondentes. O projeto está pronto para implementação incremental seguindo o roadmap.

---

## [Unreleased]

### Planejado
- Implementação do Storage Engine (páginas, cache LRU, WAL)
- Serialização binária de documentos (formato tipo-BSON)
- Query Engine com parser de filtros e updates
- Integração completa Node-API ↔ Core C++
- Índices B-Tree funcionais
- Sistema de transações e locking
- Pipeline de agregação
- Import/Export JSON e SQL
- Backup/Restore consistente
- Testes de integração completos
- Benchmarks automatizados no CI

---

**Legenda:**
- ✅ = Implementado e funcional
- 📝 = Interface/estrutura definida (implementation pendente)
- 🚧 = Em andamento
- ❌ = Não iniciado