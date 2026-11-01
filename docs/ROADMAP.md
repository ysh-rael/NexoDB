# Roadmap sugerido

Ordem recomendada de implementacao, cada etapa desbloqueando a proxima:

1. **core/storage** — gerenciador de paginas + cache LRU + leitura/escrita
   de documentos serializados (sem indices ainda). Habilita testes de
   `Insert`/`Read`/`Update`/`Remove` diretos no C++.
2. **core/document** — serializacao binaria eficiente do `Document`
   (formato interno tipo-BSON) usada pelo storage.
3. **core/query** (basico) — parser de filtros simples (`$eq`, `$gt`,
   `$lt`, `$in`) e de updates (`$set`, `$unset`, `$inc`) + varredura
   completa (sem indice). Habilita `find`/`findOne`/`updateOne`/`deleteOne`
   ponta a ponta via Node-API.
4. **src/napi** — ligar `QueryEngine` real aos metodos hoje stub em
   `database_binding.cpp`.
5. **core/index** — B-Tree + uso pelo `QueryEngine` para evitar varredura
   completa quando houver indice aplicavel.
6. **core/transaction** — locking para suportar multiplas conexoes
   simultaneas com seguranca.
7. **core/aggregation** — estagios do `aggregate()`.
8. **core/io/json** e **core/io/sql** — import/export.
9. **core/backup** — backup/restore consistentes.
10. **benchmark/** — habilitar os cenarios hoje marcados como TODO e
    estabelecer uma baseline de desempenho no CI.

Cada etapa deve vir acompanhada dos testes correspondentes em
`test/cpp` e/ou `test/js`.
