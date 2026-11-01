# NexoDB

**Nome:** NexoDB
**Descrição:** Banco de dados documental embarcado, desenvolvido em C++, com integração nativa ao Node.js via Node-API.
**Linguagem:** C++
**Plataforma:** Node.js via Node-API (Windows, Linux, macOS) (x64, arm64) (Node.js 24+)
**Tipo:** Banco de dados documental embarcado baseado em MongoDB
**Licença:** MIT
**Repositório:** https://github.com/yshrael/NexoDB
**Documentação:** https://github.com/yshrael/NexoDB#readme
**Status:** Em desenvolvimento (fase de scaffold — ver `docs/ROADMAP.md`)
**Contato:** https://github.com/yshrael

---

## Resumo

Banco de dados documental embarcado, semelhante ao SQLite, mas baseado no
modelo do MongoDB, com integração nativa ao Node.js via Node-API (o core em
C++ pode ser usado fora do Node.js também, não é uso exclusivo dele).

O objetivo é fornecer uma solução leve e eficiente, sem custo constante de
RAM para armazenamento e consulta de documentos JSON, com a facilidade de
uso do MongoDB/Mongoose e integração direta com aplicações Node.js — de
modo que as chamadas sejam semelhantes ao uso do Mongoose. A aplicação deve
ser capaz de importar e exportar documentos JSON de forma eficiente e
simples, além de permitir importar e exportar bancos SQL.

### Métodos do Mongoose replicados (assinatura e comportamento)

`findOne` · `findById` · `find` · `findOneAndUpdate` · `findByIdAndUpdate` ·
`count` · `countDocuments` · `updateOne` · `updateMany` · `replaceOne` ·
`deleteOne` · `findOneAndRemove` · `deleteMany` · `remove` ·
`findOneAndDelete` · `findOneAndReplace` · `aggregate`

Ao instanciar a conexão, é possível especificar o caminho do arquivo de
armazenamento, além de opções de cache e desempenho, garantindo
funcionamento eficiente sem consumo desnecessário de memória. O banco
suporta múltiplas conexões simultâneas, com mecanismos de bloqueio e
controle de concorrência para transações seguras.

O sistema também fornece mecanismos de backup e restauração, permitindo
continuidade das operações e integridade dos dados em caso de falha.

## Instalação

```bash
npm install nexodb
```

> Requer toolchain de compilação C++ (CMake 3.20+, compilador com suporte a
> C++20) além do Node.js 24+, pois o pacote compila um addon nativo durante
> a instalação (`cmake-js`).

## Uso básico

```js
const { connect, Schema, model } = require('nexodb');

const conn = connect('./dados/meubanco.nxdb', { cacheSizeMb: 128 });

const UserSchema = new Schema({ name: String, age: Number });
UserSchema.index({ email: 1 }, { unique: true });

const User = model('User', UserSchema, conn);

const ada = await User.create({ name: 'Ada', age: 30 });
const found = await User.findOne({ name: 'Ada' });
await User.updateOne({ _id: ada._id }, { $set: { age: 31 } });
await User.deleteOne({ _id: ada._id });

await conn.close();
```

## Estrutura do projeto

Ver [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) para a divisão completa
de responsabilidades entre `include/`, `src/core/*`, `src/napi/` e `lib/`.

```
include/nexodb/   Headers públicos do core
src/core/         Implementação do core em C++ (storage, índices, transações,
                  consultas, agregação, import/export, backup)
src/napi/         Binding Node-API (classe Database)
lib/              API JavaScript estilo Mongoose (Schema, Model, Query...)
test/             Testes C++ (GoogleTest) e JS (node:test)
benchmark/        Cenários de carga e coleta de métricas (tempo, memória)
docs/             Arquitetura e roadmap
.github/          Workflows de CI/CD, templates de issue/PR
```

## Desenvolvimento

```bash
npm install          # instala deps e compila o addon nativo
npm run build         # recompila o addon (Release)
npm run build:debug   # recompila o addon (Debug)
npm test              # roda os testes JS
npm run test:cpp      # roda os testes C++ (requer -DNEXODB_BUILD_TESTS=ON)
npm run lint           # lint do código JS
npm run format:cpp     # formata o código C++ com clang-format
npm run bench           # roda os cenários de benchmark em benchmark/scenarios
```

## Roadmap

Ver [`docs/ROADMAP.md`](docs/ROADMAP.md) para a ordem sugerida de
implementação do core (storage → query básico → binding → índices →
transações → agregação → import/export → backup).

## Licença

MIT — ver [`LICENSE`](LICENSE).
