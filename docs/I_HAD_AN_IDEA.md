 # NexoDB

**Nome:** NexoDB
**Descrição:** Banco de dados documental embarcado, desenvolvido em C++, com integração nativa ao Node.js via Node-API.
**Linguagem:** C++
**Plataforma:** Node.js via Node-API (windows, linux, macOS) (x64, arm64) (Node.js 24+) 
**Tipo:** Banco de dados documental embarcado baseado em MongoDB
**Licença:** MIT
**Repositório:** https://github.com/yshrael/NexoDB
**Documentação:** https://github.com/yshrael/NexoDB#readme
**Status:** Em desenvolvimento
**Contato:** https://github.com/yshrael
---
## Resumo
Criar um banco de dados documental embarcado, semelhante ao sqlite, mas baseado em MongoDB, com integração nativa ao Node.js via Node-API (Permite utilizar atraves do c++, nao sendo uso exclusivo do Node.js).

O objetivo é fornecer uma solução leve e eficiente que nao tenha um custo constante de RAM para armazenamento e consulta de documentos JSON, com a facilidade de uso do MongoDB e a integração direta com aplicações Node.js, de modo que as chamadas sejam semenhantes ao uso do MongoDB.  atraves do mongoose. A aplicacao deve ser capaz de importar e exportar documentos JSON de forma eficiente e simples, aalem de permitir importar e exportar bancos SQL. 

Precisa ter os seguintes metodos do mongoose e conseguir replicar o comportamento e assinatura para chamada:
* findOne
* findById
* find
* findOneAndUpdate
* findByIdAndUpdate
* count
* countDocuments
* updateOne
* updateMany
* replaceOne
* deleteOne
* findOneAndRemove
* deleteMany
* remove
* findOneAndDelete
* findOneAndReplace
* aggregate

Ao instanciar um objeto do DB, deve ser possivel especificar o caminho do arquivo de armazenamento, bem como configurar opções de cache e desempenho, garantindo que o banco de dados funcione de forma eficiente sem consumir memória desnecessária. Alem disso conseguir receber multiplas conexões simultâneas e gerenciar transações de forma segura e eficiente. O banco de dados deve fornecer mecanismos de bloqueio e controle de concorrência para evitar conflitos e garantir a integridade dos dados durante operações simultâneas.

O sistema deve também fornecer mecanismos de backup e restauração, permitindo que os dados sejam salvos periodicamente e restaurados em caso de perda ou corrupção, assegurando a continuidade das operações e a integridade dos dados.

## Sobre o projeto e pastas
O projeto deve er documentado, tendo comentarios em linha sobre o que cada coisa faz, alem de obrigatorimente dividir o código em pastas e módulos claros, como. esta proibido conrtuir modulos muito grande e complexos no mesmo arquivo. divida em responsabilidades e servicos. 

## Sobre os testes e benchmarks 
O projeto deve ter um suite de testes que provem seu funiconamento alem de poder avaliar o seu desempenho em diferentes cenários de uso.

Permitir simular diferentes senarios de carga e avaliar como o banco se comporta, como ele utiliza o recurso de memória, CPU e I/O, identificando possíveis gargalos e pontos de otimização. 


## Sobre o github
O projeto deve ter pastas de configuracoes do github que permitam a integração com ações automatizadas, como CI/CD, testes automatizados e verificação de qualidade do código. Além disso, deve incluir arquivos de configuração para linting, formatação e outras boas práticas de desenvolvimento, garantindo consistência e qualidade no código do projeto.