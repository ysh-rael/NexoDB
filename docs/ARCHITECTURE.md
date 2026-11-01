# Arquitetura do NexoDB

Este documento descreve a divisao de responsabilidades entre os modulos do
projeto. A regra geral: **cada pasta e um servico com uma unica
responsabilidade**, sem arquivos grandes/multirresponsabilidade.

```
include/nexodb/        Headers publicos do core (contratos entre modulos)
src/core/
  common/               Status/Result, Logger — utilitarios sem dependencias
  document/             Modelo de documento (Document, Value, ObjectId)
  storage/              Persistencia em disco: paginas, cache, WAL
  index/                Indices B-Tree sobre campos de documentos
  transaction/          Locking e controle de concorrencia/transacoes
  query/                Parser de filtros/updates + execucao de find/update
  aggregation/          Pipeline de agregacao ($match, $group, $sort, ...)
  io/json/              Import/export em JSON/NDJSON
  io/sql/               Import/export de/para dumps SQL
  backup/               Backup e restauracao do arquivo de banco
src/napi/               Binding Node-API (classe `Database` exposta ao JS)
lib/                     API JS estilo Mongoose (Schema, Model, Query, ...)
test/cpp, test/js        Testes unitarios/integracao
benchmark/               Cenarios de carga e script de execucao/relatorio
```

## Fluxo de uma chamada, ex. `User.findOne({ name: "Ada" })`

1. **lib/Model.js** monta um objeto `Query` (lib/Query.js) com o filtro e
   opcoes (sort/limit/skip/select).
2. Ao ser resolvida (`await`/`.exec()`), a `Query` chama o metodo
   correspondente na instancia nativa (`connection._native.findOne(...)`).
3. **src/napi/database_binding.cpp** converte os argumentos JS para os tipos
   C++ (`query::Filter`, `query::QueryOptions`) e chama o `QueryEngine`.
4. **src/core/query/query_engine.cpp** decide se usa um indice
   (`core/index`) ou faz varredura completa via `core/storage`.
5. **src/core/storage/storage_engine.cpp** le as paginas necessarias do
   arquivo `.nxdb` (usando o cache antes de ir ao disco) e devolve o
   `Document`.
6. O resultado volta pela mesma cadeia, sendo convertido de volta para um
   objeto JS em `database_binding.cpp` e embrulhado em `lib/Document.js`.

## Por que sem custo constante de RAM

O `StorageEngine` nunca carrega a colecao inteira em memoria: ele mantem um
cache de paginas com tamanho configuravel (`cacheSizeMb`) e usa uma politica
de evicao (LRU, planejada) para paginas frias. Isso o diferencia de
estruturas puramente in-memory e aproxima o comportamento do SQLite.

## Concorrencia

Multiplas conexoes (processos/threads) podem abrir o mesmo arquivo. O
`TransactionManager` garante isolamento via locking em nivel de
documento/colecao, com timeout para evitar deadlocks. Uma evolucao futura
natural e migrar para MVCC para reduzir contencao entre leitores e
escritores.

## Status atual

O projeto esta na fase de **esqueleto/scaffold**: a estrutura de pastas, as
interfaces publicas (`include/nexodb/*.hpp`), o binding Node-API e a API
JS estilo Mongoose ja existem e compilam, mas a maior parte da logica do
core ainda esta marcada com `TODO`. Ver `docs/ROADMAP.md` para a ordem
sugerida de implementacao.
