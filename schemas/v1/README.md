# RIFT Local Protocol v1 schemas

Estes arquivos usam JSON Schema Draft 2020-12 e são normativos para estruturas recebidas pelos SDKs do RIFT Connection Manager.

## Schemas

- `common.schema.json`: tipos compartilhados.
- `session.schema.json`: resposta de `GET /session`.
- `game-session.schema.json`: sessão multiplayer.
- `join-context.schema.json`: contexto persistente de entrada.
- `invite.schema.json`: convite de jogo.
- `error.schema.json`: erro estável do protocolo local.

Implementações devem aceitar propriedades futuras nos modelos extensíveis marcados com `additionalProperties: true`. O envelope de erro e o contexto de entrada são estritos para detectar respostas inválidas.

