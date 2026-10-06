# RIFT Connection Manager

O **RIFT Connection Manager (RCM)** é a camada de integração entre jogos e o ecossistema do BREAK™ | RIFT.

O RCM fornece identidade autenticada, game tickets, presença, sessões multiplayer, descoberta de partidas, contexto de conexão e convites. O jogo integra conceitos do RIFT sem implementar autenticação, acesso ao backend, comunicação com o launcher ou regras sociais.

```text
RIFT Backend
      ↕
RIFT Launcher
      ↕ RIFT Local Protocol
RIFT Connection Manager SDK
      ↕
Jogo
```

## Estado

O projeto está na fase de definição do **RIFT Local Protocol v1**. A primeira implementação será o **RIFT Connection Manager — Unreal Engine SDK**.

Nenhuma API do SDK é considerada estável enquanto o protocolo v1 estiver marcado como draft.

## Implementações planejadas

| Implementação | Estado |
|---|---|
| Unreal Engine SDK | Planejada |
| Unity SDK | Planejada |
| C++ SDK | Planejada |
| C# SDK | Planejada |

Os SDKs são versionados por engine ou linguagem. Jogos não recebem forks próprios do RCM; cada jogo escolhe uma versão compatível do SDK.

## Estrutura

```text
docs/protocol/   Contratos versionados do protocolo local
schemas/         Esquemas JSON normativos
sdks/            Implementações por engine ou linguagem
examples/        Projetos mínimos de integração
tests/protocol/  Testes de compatibilidade do contrato
```

## Protocolo

O contrato em elaboração está em [docs/protocol/RIFT_LOCAL_PROTOCOL_V1.md](docs/protocol/RIFT_LOCAL_PROTOCOL_V1.md).

## Princípio de integração

> O jogo integra conceitos; nunca integra infraestrutura.

Na Unreal, a meta é copiar ou instalar o plugin, ativá-lo, obter `URiftConnectionSubsystem`, conectar os eventos necessários e usar operações como `HostGame`, `FindGames`, `JoinGame` e `InviteFriend`.

O networking real continua sob responsabilidade do jogo e da engine.
