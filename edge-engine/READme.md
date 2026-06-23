# edge-engine

Mini shell (interpretador de comandos) em **C**, voltado para contextos de **IIoT / edge computing**.
Funciona como um orquestrador de borda leve, sem dependências além da libc (POSIX), que executa
pipelines de comandos nos modos interativo e batch.

## Recursos

- Execução de comandos externos via `fork`/`exec`
- Pipelines UNIX (`cmd1 | cmd2 | ...`)
- Redirecionamentos de entrada/saída (`<`, `>`)
- Operadores especiais (ver abaixo)
- Execução em background (`&`) com tabela de jobs, reporte de conclusão e built-in `jobs`
- Expansão de wildcards (glob)
- Comandos internos: `cd`, `pwd`, `echo`, `exit`, `fim`, `jobs`
- Tratamento de sinais (`SIGCHLD`, `SIGINT`, `SIGQUIT`)

## Operadores especiais

| Operador        | Descrição                                                                       |
|-----------------|---------------------------------------------------------------------------------|
| `\|`            | Pipe UNIX padrão (`pipe()`) entre comandos                                       |
| `cmd <= fila`   | Produtor: drena a **fila de mensagens POSIX** nomeada e injeta no stdin do pipeline |
| `cmd => fila`   | Consumidor: publica o stdout do pipeline como mensagens na **fila POSIX** nomeada   |
| `&`             | Executa o comando em background                                                  |

Os operadores `<=`/`=>` usam **filas de mensagens POSIX** (`mq_open`/`mq_send`/`mq_receive`),
um mecanismo de IPC por troca de mensagens distinto do `pipe()` usado pelo `|`. As filas são
nomeadas (ex.: `telemetry_raw` → `/telemetry_raw`), persistem no kernel entre comandos e até
entre execuções, permitindo desacoplamento temporal entre produtor e consumidor. O produtor
remove a fila (`mq_unlink`) após drená-la, evitando desperdício de recursos do SO. Também é
aceito o atalho `<= fila comando`. Exemplos:

```sh
echo "ERROR: sensor" => log_sensor      # publica na fila /log_sensor
grep ERROR <= log_sensor                # drena a fila e filtra
cat <= origem => destino                # ponte direta fila -> fila
```

## Estrutura

```
include/edge_engine.h   Header global (structs Command/Pipeline, constantes)
src/main.c              Loop principal, sinais, leitura de linha, modos interativo/batch
src/parser/            Tokenização e parsing (pipes, <=, =>, redirecionamentos)
src/executor/          fork/exec, setup de pipes, wildcards, produtor/consumidor
src/builtins/          Comandos internos
src/utils/             Wrappers seguros (safe_close, safe_dup2, strdup_safe)
tests/                 Scripts batch de teste
```

## Build

```sh
make          # compila o projeto (gera bin/edge-engine)
make debug    # compila com flags de debug (-DDEBUG -O0)
make clean    # remove objetos e binário
make tree     # mostra a estrutura do projeto
make help     # lista os alvos disponíveis
```

## Uso

```sh
./bin/edge-engine            # modo interativo
./bin/edge-engine script.sh  # modo batch (executa comandos de um arquivo)
```
